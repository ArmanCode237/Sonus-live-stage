#include "app/StateHub.h"
#include <algorithm>
#include <chrono>

namespace {
// A client that accumulates this many unsent ordered frames is too slow: its
// queue is replaced by a fresh snapshot instead of growing without bound.
constexpr size_t kMaxOrderedBacklog = 64;
}

std::vector<std::string> StateHub::Client::take(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, timeout, [this] { return closed_ || !ordered_.empty() || !latestState_.empty(); });
    std::vector<std::string> out(std::make_move_iterator(ordered_.begin()), std::make_move_iterator(ordered_.end()));
    ordered_.clear();
    if (!latestState_.empty()) { out.push_back(std::move(latestState_)); latestState_.clear(); }
    return out;
}

void StateHub::Client::close() {
    closed_ = true;
    cv_.notify_all();
}

StateHub::StateHub(Provider liveState, Provider params)
    : liveState_(std::move(liveState)), params_(std::move(params)) {}

StateHub::~StateHub() { stop(); }

void StateHub::start() {
    if (running_.exchange(true)) return;
    worker_ = std::thread([this] { run(); });
}

void StateHub::stop() {
    if (!running_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& c : clients_) c->close();
    clients_.clear();
}

size_t StateHub::clientCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return clients_.size();
}

void StateHub::pushOrdered(Client& client, std::string frame) {
    {
        std::lock_guard<std::mutex> lock(client.mutex_);
        client.ordered_.push_back(std::move(frame));
    }
    client.cv_.notify_one();
}

void StateHub::queueSnapshotLocked(Client& client) {
    if (lastParams_.is_null()) lastParams_ = params_();
    json frame = {{"t", "snapshot"}, {"seq", seq_}, {"state", liveState_()}, {"params", lastParams_}};
    {
        std::lock_guard<std::mutex> lock(client.mutex_);
        client.ordered_.clear();       // older patches are superseded by the snapshot
        client.latestState_.clear();
        client.ordered_.push_back(frame.dump());
    }
    client.cv_.notify_one();
}

std::shared_ptr<StateHub::Client> StateHub::attach() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (clients_.size() >= kMaxClients) return nullptr;
    auto client = std::make_shared<Client>();
    queueSnapshotLocked(*client);
    clients_.push_back(client);
    return client;
}

void StateHub::detach(const std::shared_ptr<Client>& client) {
    if (!client) return;
    client->close();
    std::lock_guard<std::mutex> lock(mutex_);
    clients_.erase(std::remove(clients_.begin(), clients_.end(), client), clients_.end());
}

void StateHub::resync(const std::shared_ptr<Client>& client) {
    if (!client) return;
    std::lock_guard<std::mutex> lock(mutex_);
    queueSnapshotLocked(*client);
}

void StateHub::run() {
    using clock = std::chrono::steady_clock;
    auto next = clock::now();
    unsigned tick = 0;
    while (running_) {
        next += std::chrono::milliseconds(kFramePeriodMs);
        std::this_thread::sleep_until(next);
        if (clock::now() - next > std::chrono::milliseconds(250)) next = clock::now(); // resume after a stall

        std::lock_guard<std::mutex> lock(mutex_);
        if (clients_.empty()) { lastParams_ = json(); continue; } // idle: no work at all

        // Params: diff the edit buffer against the last published one (15 Hz).
        if ((tick++ & 1u) == 0) {
            json now = params_();
            if (lastParams_.is_null()) lastParams_ = now;
            else {
                json ops = json::diff(lastParams_, now);
                if (!ops.empty()) {
                    ++seq_;
                    lastParams_ = std::move(now);
                    const std::string frame = json{{"t", "patch"}, {"seq", seq_}, {"ops", std::move(ops)}}.dump();
                    for (auto& c : clients_) {
                        bool overflow;
                        { std::lock_guard<std::mutex> cl(c->mutex_); overflow = c->ordered_.size() >= kMaxOrderedBacklog; }
                        if (overflow) queueSnapshotLocked(*c);
                        else pushOrdered(*c, frame);
                    }
                }
            }
        }

        // Live state (meters, keys, voices, program): newest frame wins.
        json state = liveState_();
        state["t"] = "state";
        state["seq"] = seq_;
        const std::string frame = state.dump();
        for (auto& c : clients_) {
            { std::lock_guard<std::mutex> cl(c->mutex_); c->latestState_ = frame; }
            c->cv_.notify_one();
        }
    }
}
