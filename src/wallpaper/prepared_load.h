#ifndef WALLPAPER_PREPARED_LOAD_H
#define WALLPAPER_PREPARED_LOAD_H

#include <atomic>
#include <chrono>
#include <future>
#include <utility>

// State changes happen on the coordinator thread; workers may only read `cancelled`.
struct PreparedLoad {
    enum class State { Preparing, Graphics, Ready, Failed, Cancelled };

    explicit PreparedLoad(std::future<bool> worker) : preparation(std::move(worker)) {}
    PreparedLoad(PreparedLoad&& other) noexcept
        : state(other.state),
          cancelled(other.cancelled.load(std::memory_order_acquire)),
          preparation(std::move(other.preparation)) {
        other.state = State::Cancelled;
        other.cancelled.store(true, std::memory_order_release);
    }
    PreparedLoad& operator=(PreparedLoad&& other) noexcept {
        if (this == &other) return *this;
        state = other.state;
        cancelled.store(other.cancelled.load(std::memory_order_acquire), std::memory_order_release);
        preparation = std::move(other.preparation);
        other.state = State::Cancelled;
        other.cancelled.store(true, std::memory_order_release);
        return *this;
    }
    PreparedLoad(const PreparedLoad&) = delete;
    PreparedLoad& operator=(const PreparedLoad&) = delete;

    State state = State::Preparing;
    std::atomic<bool> cancelled{false};
    std::future<bool> preparation;

    // Nonblocking: never calls get() until the future reports ready.
    bool pollPreparation() {
        if (preparation.valid() && preparation.wait_for(std::chrono::seconds(0)) == std::future_status::ready &&
            (state == State::Preparing || state == State::Cancelled)) {
            consumePreparation();
        } else if (state == State::Preparing && !preparation.valid()) {
            state = cancelled.load(std::memory_order_acquire) ? State::Cancelled : State::Failed;
        }
        return state == State::Graphics;
    }

    void cancel() {
        cancelled.store(true, std::memory_order_release);
        if (state != State::Failed) state = State::Cancelled;
    }

    void markReady() {
        if (state == State::Graphics && !cancelled.load(std::memory_order_acquire)) state = State::Ready;
    }

    bool canCommit() const {
        return state == State::Ready && !cancelled.load(std::memory_order_acquire);
    }

    // Shutdown path: wait for worker completion and consume its result exactly once.
    void drain() {
        if (preparation.valid()) {
            preparation.wait();
            if (state == State::Preparing || state == State::Cancelled) consumePreparation();
        }
        if (cancelled.load(std::memory_order_acquire) && state != State::Ready && state != State::Failed)
            state = State::Cancelled;
    }

   private:
    void consumePreparation() {
        bool success = false;
        try {
            success = preparation.get();
        } catch (...) {
            state = cancelled.load(std::memory_order_acquire) ? State::Cancelled : State::Failed;
            return;
        }
        if (cancelled.load(std::memory_order_acquire))
            state = State::Cancelled;
        else
            state = success ? State::Graphics : State::Failed;
    }
};

#endif  // WALLPAPER_PREPARED_LOAD_H
