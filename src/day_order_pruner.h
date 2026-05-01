#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

// Drives a periodic "good-for-day" pruning callback on a background thread.
//
// The pruner deliberately does not own the OrderBook or know how pruning
// is performed - callers wire it up with two callbacks:
//   - on_fire:   invoked on the worker thread at each scheduled fire time.
//                Must be thread-safe (e.g. an OrderBook method that locks).
//   - next_fire: pure function returning the next fire time_point given now.
//                Use nextDailyCutoff for the standard end-of-day schedule,
//                or any other function for testing / alternative schedules.
//
// The destructor signals shutdown and joins the worker thread before
// returning.
class GoodForDayPruner {
public:
    using Clock = std::chrono::system_clock;
    using NextFireFn = std::function<Clock::time_point(Clock::time_point)>;

    GoodForDayPruner(std::function<void()> on_fire, NextFireFn next_fire);
    ~GoodForDayPruner();

    GoodForDayPruner(const GoodForDayPruner&) = delete;
    GoodForDayPruner& operator=(const GoodForDayPruner&) = delete;

    // Returns the next moment after now whose local-time hour equals
    // cutoff_hour (with minute and second zero). If now is at or past
    // today's cutoff, returns tomorrow's. Pure function - safe to call
    // from tests with arbitrary now values.
    static Clock::time_point nextDailyCutoff(Clock::time_point now,
                                             std::chrono::hours cutoff_hour);

private:
    void run();

    std::function<void()> on_fire;
    NextFireFn next_fire;
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<bool> shutdown{false};
    std::thread thread;
};
