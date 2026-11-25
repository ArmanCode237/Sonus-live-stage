#pragma once
// Real-time state publisher for UI clients (WebSocket /ws).
//
// Runs on its own thread and only calls the two providers it is given; it never
// touches the audio callback. Frames (JSON text):
//   {"t":"snapshot","seq":N,"state":{...},"params":{...}}  on connect / resync
//   {"t":"patch","seq":N,"ops":[RFC 6902]}                   when params change
//   {"t":"state",...}                                        ~30 Hz live state
// Patches are numbered; a client that sees a gap asks for a resync. Live-state
// frames are "latest wins" (a slow client only gets the newest one), patches are
// never dropped silently: if a client falls too far behind it gets a snapshot.
#include <nlohmann/json.hpp>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class StateHub {
public:
    using json = nlohmann::json;
    using Provider = std::function<json()>;

    /** Each connection holds one HTTP worker thread, so clients are capped. */
    static constexpr size_t kMaxClients = 4;
    /** Live-state frame period (30 Hz); params are diffed every other frame. */
    static constexpr int kFramePeriodMs = 33;

    class Client {
    public:
        /** Frames to send, in order (live state last). Waits up to timeout for data. */
        std::vector<std::string> take(std::chrono::milliseconds timeout);
        void close();
        bool closed() const { return closed_.load(); }
    private:
        friend class StateHub;
        std::mutex mutex_;
        std::condition_variable cv_;
        std::deque<std::string> ordered_;   // snapshot / patch, never reordered
        std::string latestState_;           // newest live-state frame only
        std::atomic<bool> closed_{false};
    };

    StateHub(Provider liveState, Provider params);
    ~StateHub();
    StateHub(const StateHub&) = delete;
    StateHub& operator=(const StateHub&) = delete;

    void start();
    void stop();

    /** Registers a client and queues a full snapshot. nullptr when full. */
    std::shared_ptr<Client> attach();
    void detach(const std::shared_ptr<Client>& client);
    /** Re-sends a full snapshot to one client (it detected a sequence gap). */
    void resync(const std::shared_ptr<Client>& client);

    size_t clientCount() const;

private:
    void run();
    void queueSnapshotLocked(Client& client);   // requires mutex_
    static void pushOrdered(Client& client, std::string frame);

    Provider liveState_;
    Provider params_;
    std::atomic<bool> running_{false};
    std::thread worker_;

    mutable std::mutex mutex_;                  // guards everything below
    std::vector<std::shared_ptr<Client>> clients_;
    json lastParams_;
    uint64_t seq_ = 0;
};
