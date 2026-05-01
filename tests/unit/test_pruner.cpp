#include <gtest/gtest.h>
#include "day_order_pruner.h"
#include <atomic>
#include <chrono>
#include <ctime>
#include <thread>

// Note: end-to-end pruning of GoodForDay orders out of an OrderBook is not
// directly unit-testable through the public OrderBook API, since the
// pruning entry point is private and only invoked by the pruner's own
// background thread at the next scheduled cutoff. The tests here cover the
// two pieces that ARE testable in isolation:
//   - GoodForDayPruner::nextDailyCutoff (pure scheduling math)
//   - GoodForDayPruner's own thread / shutdown lifecycle, with injected
//     callbacks that have nothing to do with OrderBook.

// ===== nextDailyCutoff scheduling =====

namespace {

// Build a system_clock::time_point at the given local-time calendar values.
// Uses mktime so DST is handled by the platform.
GoodForDayPruner::Clock::time_point localTime(int year, int mon, int day,
                                              int hour, int minute, int second) {
    std::tm parts{};
    parts.tm_year = year - 1900;
    parts.tm_mon  = mon - 1;
    parts.tm_mday = day;
    parts.tm_hour = hour;
    parts.tm_min  = minute;
    parts.tm_sec  = second;
    parts.tm_isdst = -1;
    return GoodForDayPruner::Clock::from_time_t(std::mktime(&parts));
}

// Decompose a system_clock::time_point back into local-time tm parts so
// tests can assert on the hour/minute/day without caring about TZ offsets.
std::tm toLocalParts(GoodForDayPruner::Clock::time_point tp) {
    std::time_t t = GoodForDayPruner::Clock::to_time_t(tp);
    std::tm out{};
#if defined(_WIN32)
    localtime_s(&out, &t);
#else
    localtime_r(&t, &out);
#endif
    return out;
}

} // namespace

TEST(NextDailyCutoff, ReturnsTodayWhenNowIsBeforeCutoff) {
    auto now    = localTime(2026, 4, 30, 10, 0, 0);
    auto cutoff = GoodForDayPruner::nextDailyCutoff(now, std::chrono::hours(16));

    auto parts = toLocalParts(cutoff);
    EXPECT_EQ(parts.tm_year, 2026 - 1900);
    EXPECT_EQ(parts.tm_mon,  4 - 1);
    EXPECT_EQ(parts.tm_mday, 30);
    EXPECT_EQ(parts.tm_hour, 16);
    EXPECT_EQ(parts.tm_min,  0);
    EXPECT_EQ(parts.tm_sec,  0);
}

TEST(NextDailyCutoff, ReturnsTomorrowWhenNowIsPastCutoff) {
    auto now    = localTime(2026, 4, 30, 17, 30, 0);
    auto cutoff = GoodForDayPruner::nextDailyCutoff(now, std::chrono::hours(16));

    auto parts = toLocalParts(cutoff);
    // April 30 + 1 day, normalized by mktime, is May 1.
    EXPECT_EQ(parts.tm_year, 2026 - 1900);
    EXPECT_EQ(parts.tm_mon,  5 - 1);
    EXPECT_EQ(parts.tm_mday, 1);
    EXPECT_EQ(parts.tm_hour, 16);
    EXPECT_EQ(parts.tm_min,  0);
}

TEST(NextDailyCutoff, ReturnsTomorrowAtExactCutoff) {
    // Per the existing semantics: at-or-past cutoff schedules the *next* day.
    auto now    = localTime(2026, 4, 30, 16, 0, 0);
    auto cutoff = GoodForDayPruner::nextDailyCutoff(now, std::chrono::hours(16));

    auto parts = toLocalParts(cutoff);
    EXPECT_EQ(parts.tm_mon,  5 - 1);
    EXPECT_EQ(parts.tm_mday, 1);
    EXPECT_EQ(parts.tm_hour, 16);
}

TEST(NextDailyCutoff, IsStrictlyAfterNow) {
    using std::chrono::hours;
    // Sample several times of day; the cutoff must always be in the future.
    for (int hour : {0, 8, 15, 16, 17, 23}) {
        auto now    = localTime(2026, 6, 15, hour, 0, 0);
        auto cutoff = GoodForDayPruner::nextDailyCutoff(now, hours(16));
        EXPECT_GT(cutoff, now) << "now hour = " << hour;
    }
}

TEST(NextDailyCutoff, RollsOverMonthBoundary) {
    auto now    = localTime(2026, 4, 30, 23, 0, 0);  // April 30
    auto cutoff = GoodForDayPruner::nextDailyCutoff(now, std::chrono::hours(16));

    auto parts = toLocalParts(cutoff);
    EXPECT_EQ(parts.tm_mon,  5 - 1);  // May
    EXPECT_EQ(parts.tm_mday, 1);
    EXPECT_EQ(parts.tm_hour, 16);
}

// ===== Pruner thread lifecycle =====

TEST(GoodForDayPrunerThread, FiresWhenScheduledImmediately) {
    // next_fire returns ~1ms in the future, so the worker thread fires
    // almost immediately. We give it some wall-clock time, then let the
    // destructor signal shutdown and join.
    std::atomic<int> fire_count{0};
    {
        GoodForDayPruner pruner(
            [&] { fire_count.fetch_add(1, std::memory_order_relaxed); },
            [](GoodForDayPruner::Clock::time_point) {
                return GoodForDayPruner::Clock::now()
                     + std::chrono::milliseconds(1);
            });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } // destructor signals shutdown and joins

    EXPECT_GE(fire_count.load(), 1);
}

TEST(GoodForDayPrunerThread, ShutdownIsPromptWhenNextFireIsFarAway) {
    // The worker is asleep waiting for "tomorrow." Destructor must wake it
    // promptly via the shutdown cv and join without waiting for the timeout.
    auto start = std::chrono::steady_clock::now();
    {
        GoodForDayPruner pruner(
            [] {},
            [](GoodForDayPruner::Clock::time_point) {
                return GoodForDayPruner::Clock::now() + std::chrono::hours(24);
            });
    }
    auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(elapsed, std::chrono::seconds(2));
}

TEST(GoodForDayPrunerThread, OnFireCallbackReceivesScheduledTimes) {
    // Verify that next_fire is actually called with reasonable "now" values
    // - useful as a smoke test that the loop is running.
    std::atomic<int> next_fire_calls{0};
    {
        GoodForDayPruner pruner(
            [] {},
            [&](GoodForDayPruner::Clock::time_point) {
                next_fire_calls.fetch_add(1, std::memory_order_relaxed);
                // Schedule far in the future so we don't actually fire.
                return GoodForDayPruner::Clock::now() + std::chrono::hours(24);
            });
        // Give the worker time to call next_fire once at the top of the loop.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    EXPECT_GE(next_fire_calls.load(), 1);
}
