#include "wallpaper/prepared_load.h"

#include <atomic>
#include <future>
#include <thread>

#include "test_util.h"

int main() {
    using State = PreparedLoad::State;

    std::promise<bool> delayed_promise;
    PreparedLoad delayed(delayed_promise.get_future());
    CHECK(delayed.state == State::Preparing);
    CHECK(!delayed.pollPreparation());
    CHECK(delayed.state == State::Preparing);
    delayed_promise.set_value(true);
    CHECK(delayed.pollPreparation());
    CHECK(delayed.state == State::Graphics);
    CHECK(!delayed.canCommit());
    delayed.markReady();
    CHECK(delayed.state == State::Ready);
    CHECK(delayed.canCommit());

    std::promise<bool> failed_promise;
    PreparedLoad failed(failed_promise.get_future());
    failed_promise.set_value(false);
    CHECK(!failed.pollPreparation());
    CHECK(failed.state == State::Failed);
    CHECK(!failed.canCommit());

    std::promise<bool> cancelled_promise;
    PreparedLoad cancelled(cancelled_promise.get_future());
    cancelled.cancel();
    CHECK(cancelled.cancelled.load());
    cancelled_promise.set_value(true);
    CHECK(!cancelled.pollPreparation());
    CHECK(cancelled.state == State::Cancelled);
    CHECK(!cancelled.preparation.valid());
    CHECK(!cancelled.canCommit());

    std::promise<bool> old_promise;
    std::promise<bool> newest_promise;
    PreparedLoad old_load(old_promise.get_future());
    PreparedLoad newest(newest_promise.get_future());
    old_load.cancel();
    old_promise.set_value(true);
    newest_promise.set_value(true);
    CHECK(!old_load.pollPreparation());
    CHECK(newest.pollPreparation());
    newest.markReady();
    CHECK(!old_load.canCommit());
    CHECK(newest.canCommit());

    std::promise<bool> shutdown_promise;
    PreparedLoad shutdown(shutdown_promise.get_future());
    std::atomic<bool> drain_started{false};
    std::atomic<bool> drain_finished{false};
    std::thread drainer([&] {
        drain_started.store(true, std::memory_order_release);
        shutdown.drain();
        drain_finished.store(true, std::memory_order_release);
    });
    while (!drain_started.load(std::memory_order_acquire)) std::this_thread::yield();
    CHECK(!drain_finished.load(std::memory_order_acquire));
    shutdown_promise.set_value(true);
    drainer.join();
    CHECK(drain_finished.load());
    CHECK(shutdown.state == State::Graphics);
    CHECK(!shutdown.preparation.valid());

    return test::finish("prepared load checks");
}
