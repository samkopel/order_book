#include <gtest/gtest.h>
#include "order_book.h"
#include "day_order_pruner.h"
#include <chrono>
#include <ctime>

// ===== OrderBook::pruneGoodForDayOrders behavior =====

static Order gfd(OrderId id, Price price, Quantity qty, Side side) {
    return Order{id, price, qty, side, OrderType::GoodForDay};
}
static Order gtc(OrderId id, Price price, Quantity qty, Side side) {
    return Order{id, price, qty, side, OrderType::GoodTillCancel};
}

TEST(Prune, EmptyBookIsNoop) {
    OrderBook ob;
    ob.pruneGoodForDayOrders();
    EXPECT_EQ(ob.getBestBidLevel(), nullptr);
    EXPECT_EQ(ob.getBestAskLevel(), nullptr);
}

TEST(Prune, RemovesGoodForDayBids) {
    OrderBook ob;
    ob.add(gfd(1, 100, 10, Side::BID));
    ob.pruneGoodForDayOrders();
    EXPECT_EQ(ob.getBestBidLevel(), nullptr);
}

TEST(Prune, RemovesGoodForDayAsks) {
    OrderBook ob;
    ob.add(gfd(1, 100, 10, Side::ASK));
    ob.pruneGoodForDayOrders();
    EXPECT_EQ(ob.getBestAskLevel(), nullptr);
}

TEST(Prune, KeepsGoodTillCancel) {
    OrderBook ob;
    ob.add(gtc(1, 100, 10, Side::BID));
    ob.pruneGoodForDayOrders();
    ASSERT_NE(ob.getBestBidLevel(), nullptr);
    EXPECT_EQ(ob.getBestBidLevel()->total_quantity, 10u);
}

TEST(Prune, MixedOrdersAtSameLevel_KeepsGTCRemovesGFD) {
    OrderBook ob;
    ob.add(gfd(1, 100, 7, Side::BID));   // pruned
    ob.add(gtc(2, 100, 3, Side::BID));   // kept
    ob.add(gfd(3, 100, 4, Side::BID));   // pruned

    ob.pruneGoodForDayOrders();

    auto* level = ob.getBestBidLevel();
    ASSERT_NE(level, nullptr);
    EXPECT_EQ(level->total_quantity, 3u);
    // Cancel index reflects only the surviving order.
    EXPECT_FALSE(ob.cancel(1));
    EXPECT_FALSE(ob.cancel(3));
    EXPECT_TRUE(ob.cancel(2));
}

TEST(Prune, EmptyLevelsAreRemoved) {
    OrderBook ob;
    ob.add(gfd(1, 105, 10, Side::BID));  // entire level pruned
    ob.add(gtc(2, 100, 5, Side::BID));   // survives at 100

    ob.pruneGoodForDayOrders();

    auto* level = ob.getBestBidLevel();
    ASSERT_NE(level, nullptr);
    EXPECT_EQ(level->price, 100);  // 105 level should be gone
}

TEST(Prune, MultipleLevelsBothSides) {
    OrderBook ob;
    ob.add(gfd(1, 100, 10, Side::BID));
    ob.add(gtc(2, 99,  10, Side::BID));
    ob.add(gfd(3, 105, 10, Side::ASK));
    ob.add(gtc(4, 106, 10, Side::ASK));

    ob.pruneGoodForDayOrders();

    ASSERT_NE(ob.getBestBidLevel(), nullptr);
    EXPECT_EQ(ob.getBestBidLevel()->price, 99);
    ASSERT_NE(ob.getBestAskLevel(), nullptr);
    EXPECT_EQ(ob.getBestAskLevel()->price, 106);
}

TEST(Prune, PrunedOrdersCannotBeCancelled) {
    OrderBook ob;
    ob.add(gfd(1, 100, 10, Side::BID));
    ob.pruneGoodForDayOrders();
    EXPECT_FALSE(ob.cancel(1));
}

TEST(Prune, IsIdempotent) {
    OrderBook ob;
    ob.add(gfd(1, 100, 5, Side::BID));
    ob.add(gtc(2, 100, 5, Side::BID));

    ob.pruneGoodForDayOrders();
    ob.pruneGoodForDayOrders();  // calling twice should be safe

    auto* level = ob.getBestBidLevel();
    ASSERT_NE(level, nullptr);
    EXPECT_EQ(level->total_quantity, 5u);
}

TEST(Prune, AfterPruneBookContinuesToMatch) {
    OrderBook ob;
    ob.add(gfd(1, 100, 5, Side::ASK));   // pruned
    ob.add(gtc(2, 101, 5, Side::ASK));   // kept

    ob.pruneGoodForDayOrders();

    // A matching bid should now hit the surviving 101 ask, not the pruned one.
    auto result = ob.matchLimitOrder(gtc(3, 105, 5, Side::BID));
    ASSERT_TRUE(std::holds_alternative<Filled>(result));
    auto& f = std::get<Filled>(result);
    ASSERT_EQ(f.trades.size(), 1u);
    EXPECT_EQ(f.trades[0].price, 101);
}

// ===== nextDailyCutoff scheduling =====

namespace {

// Build a system_clock::time_point at the given local-time calendar values.
// Uses mktime so DST is handled by the platform.
static GoodForDayPruner::Clock::time_point localTime(int year, int mon, int day,
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
static std::tm toLocalParts(GoodForDayPruner::Clock::time_point tp) {
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

// ===== End-to-end: pruner thread firing on its scheduled callback =====

TEST(GoodForDayPrunerThread, FiresImmediatelyWhenScheduledNow) {
    // Schedule the next fire at "now" so the thread fires once almost
    // immediately, then we shut it down via the destructor.
    std::atomic<int> fire_count{0};

    {
        GoodForDayPruner pruner(
            [&] { fire_count.fetch_add(1, std::memory_order_relaxed); },
            [](GoodForDayPruner::Clock::time_point) {
                // Always schedule fire 1ms in the future. After firing, the
                // loop will reschedule and likely fire again, but we'll be
                // shutting down before that matters much.
                return GoodForDayPruner::Clock::now()
                     + std::chrono::milliseconds(1);
            });

        // Give the worker thread enough time to fire at least once.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } // destructor signals shutdown and joins

    EXPECT_GE(fire_count.load(), 1);
}

TEST(GoodForDayPrunerThread, ShutdownIsClean) {
    // The thread waits until far in the future. Destructor must wake it
    // promptly via the shutdown cv and join without blocking on the wait.
    auto start = std::chrono::steady_clock::now();
    {
        GoodForDayPruner pruner(
            [] {},
            [](GoodForDayPruner::Clock::time_point) {
                return GoodForDayPruner::Clock::now() + std::chrono::hours(24);
            });
        // No sleep - exit scope immediately.
    }
    auto elapsed = std::chrono::steady_clock::now() - start;
    // Should join in well under a second; pick a generous bound to avoid flake.
    EXPECT_LT(elapsed, std::chrono::seconds(2));
}
