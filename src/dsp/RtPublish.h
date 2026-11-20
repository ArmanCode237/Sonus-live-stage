#pragma once
// Lock-free hand-off of immutable / audio-owned objects to the real-time thread.
//
// libstdc++ implements std::atomic_load(shared_ptr) with a mutex taken from a
// global pool. A UI / HTTP thread holding that mutex (or simply being preempted
// while holding it) can stall the audio callback: a classic source of sporadic
// dropouts. Here the audio thread only loads a raw pointer. Replaced objects are
// "retired" with the current audio epoch and destroyed on a control thread once
// the audio thread has completed two more callbacks (RCU-style grace period), so
// no destructor or free() ever runs inside the callback.
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace SonusDSP {

/** Counts completed audio callbacks. advance() is called by the audio thread at the
    end of every render; control threads read it to know when retired data is free. */
class AudioEpoch {
    std::atomic<uint64_t> count_{0};
public:
    void advance() noexcept { count_.fetch_add(1, std::memory_order_acq_rel); }
    uint64_t now() const noexcept { return count_.load(std::memory_order_acquire); }
    /** RAII: advances the epoch when a render function returns (any path). */
    struct Scope {
        AudioEpoch& epoch;
        ~Scope() { epoch.advance(); }
    };
};

/** A shared object published to the audio thread without locks.
    Control threads: publish() / owner() / collect(). Audio thread: audio() only. */
template <class T>
class RtPublished {
public:
    explicit RtPublished(const AudioEpoch& epoch) : epoch_(epoch) {}
    RtPublished(const RtPublished&) = delete;
    RtPublished& operator=(const RtPublished&) = delete;

    /** Audio thread: the current object (or nullptr). Valid until the callback returns. */
    const T* audio() const noexcept { return raw_.load(std::memory_order_acquire); }

    /** Control threads: shared ownership of the current object. */
    std::shared_ptr<const T> owner() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return owner_;
    }
    /** Control threads: replace the object; the old one is freed after the grace period. */
    void publish(std::shared_ptr<const T> next) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (owner_) retired_.push_back({std::move(owner_), epoch_.now()});
        owner_ = std::move(next);
        raw_.store(owner_.get(), std::memory_order_release);
        collectLocked();
    }
    /** Control threads: free retired objects the audio thread can no longer see. */
    void collect() {
        std::lock_guard<std::mutex> lock(mutex_);
        collectLocked();
    }

private:
    struct Retired { std::shared_ptr<const T> object; uint64_t epoch; };
    void collectLocked() {
        const uint64_t now = epoch_.now();
        retired_.erase(std::remove_if(retired_.begin(), retired_.end(),
            [now](const Retired& r) { return now >= r.epoch + 2; }), retired_.end());
    }
    const AudioEpoch& epoch_;
    std::atomic<const T*> raw_{nullptr};
    std::shared_ptr<const T> owner_;
    std::vector<Retired> retired_;
    mutable std::mutex mutex_;
};

} // namespace SonusDSP
