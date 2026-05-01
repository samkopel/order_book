#include <gtest/gtest.h>
#include "order_book.h"

// Helpers for building Orders. `makeOrder` defaults order_type to GoodTillCancel
// so existing call sites don't need to specify it.
static Order makeOrder(OrderId id, Price price, Quantity qty, Side side,
                       OrderType order_type = OrderType::GoodTillCancel) {
    return Order{id, price, qty, side, order_type};
}

// Place a non-crossing limit order — used to seed the book for tests that
// need resting liquidity. Returns the result (always OpenResult here).
static LimitOrderResult place(OrderBook& ob, OrderId id, Price price,
                              Quantity qty, Side side,
                              OrderType order_type = OrderType::GoodTillCancel) {
    return ob.matchLimitOrder(makeOrder(id, price, qty, side, order_type));
}

// ---- Order ----

TEST(Order, IsFilled) {
    Order o = makeOrder(1, 100, 0, Side::BID);
    EXPECT_TRUE(o.isFilled());
}

TEST(Order, IsNotFilled) {
    Order o = makeOrder(1, 100, 10, Side::BID);
    EXPECT_FALSE(o.isFilled());
}

TEST(Order, TradeQuantityFull) {
    Order o = makeOrder(1, 100, 10, Side::BID);
    EXPECT_EQ(o.tradeQuantity(10), 10u);
    EXPECT_TRUE(o.isFilled());
}

TEST(Order, TradeQuantityPartial) {
    Order o = makeOrder(1, 100, 10, Side::BID);
    EXPECT_EQ(o.tradeQuantity(3), 3u);
    EXPECT_EQ(o.quantity, 7u);
}

TEST(Order, TradeQuantityExceedsRemaining) {
    Order o = makeOrder(1, 100, 10, Side::BID);
    EXPECT_EQ(o.tradeQuantity(50), 10u);  // only 10 available
    EXPECT_TRUE(o.isFilled());
}

TEST(Order, IsMatchAskAgainstBidLevel) {
    // an incoming ask matches a bid level when level_price >= ask price
    EXPECT_TRUE(makeOrder(1, 100, 1, Side::ASK).isMatch(100));
    EXPECT_TRUE(makeOrder(1, 99,  1, Side::ASK).isMatch(100));
    EXPECT_FALSE(makeOrder(1, 101, 1, Side::ASK).isMatch(100));
}

TEST(Order, IsMatchBidAgainstAskLevel) {
    // an incoming bid matches an ask level when bid price >= level_price
    EXPECT_TRUE(makeOrder(1, 100, 1, Side::BID).isMatch(100));
    EXPECT_TRUE(makeOrder(1, 101, 1, Side::BID).isMatch(100));
    EXPECT_FALSE(makeOrder(1, 99,  1, Side::BID).isMatch(100));
}

// ---- PriceLevel ----

TEST(PriceLevel, AddOrderAccumulatesQuantity) {
    PriceLevel level(Side::BID, 100);
    level.addOrder(makeOrder(1, 100, 10, Side::BID));
    level.addOrder(makeOrder(2, 100, 5,  Side::BID));
    EXPECT_EQ(level.total_quantity, 15u);
}

TEST(PriceLevel, RemoveOrderReducesQuantity) {
    PriceLevel level(Side::BID, 100);
    auto it = level.addOrder(makeOrder(1, 100, 10, Side::BID));
    level.removeOrder(it);
    EXPECT_EQ(level.total_quantity, 0u);
    EXPECT_TRUE(level.isEmpty());
}

// ---- OrderBook: placing a non-crossing limit order ----

TEST(OrderBookPlace, NonCrossingBidLandsOnBook) {
    OrderBook ob;
    auto r = place(ob, 1, 100, 10, Side::BID);
    ASSERT_TRUE(std::holds_alternative<OpenResult>(r));
    EXPECT_EQ(std::get<OpenResult>(r).executed_quantity, 10u);

    // It's resting on the book: a market sell of 10 should fully consume it
    // at price 100.
    auto m = ob.matchMarketOrder(Side::ASK, 10);
    ASSERT_TRUE(std::holds_alternative<Filled>(m));
    auto& f = std::get<Filled>(m);
    ASSERT_EQ(f.trades.size(), 1u);
    EXPECT_EQ(f.trades[0].price, 100);
    EXPECT_EQ(f.trades[0].quantity, 10u);
}

TEST(OrderBookPlace, NonCrossingAskLandsOnBook) {
    OrderBook ob;
    place(ob, 1, 100, 10, Side::ASK);

    auto m = ob.matchMarketOrder(Side::BID, 10);
    ASSERT_TRUE(std::holds_alternative<Filled>(m));
    auto& f = std::get<Filled>(m);
    ASSERT_EQ(f.trades.size(), 1u);
    EXPECT_EQ(f.trades[0].price, 100);
}

TEST(OrderBookPlace, BestBidIsHighestPrice) {
    OrderBook ob;
    place(ob, 1, 100, 10, Side::BID);
    place(ob, 2, 105, 5,  Side::BID);   // best
    place(ob, 3, 95,  15, Side::BID);

    // A market sell of 1 should hit the best bid (105).
    auto m = ob.matchMarketOrder(Side::ASK, 1);
    ASSERT_TRUE(std::holds_alternative<Filled>(m));
    EXPECT_EQ(std::get<Filled>(m).trades[0].price, 105);
}

TEST(OrderBookPlace, BestAskIsLowestPrice) {
    OrderBook ob;
    place(ob, 1, 100, 10, Side::ASK);
    place(ob, 2, 95,  5,  Side::ASK);   // best
    place(ob, 3, 110, 15, Side::ASK);

    auto m = ob.matchMarketOrder(Side::BID, 1);
    ASSERT_TRUE(std::holds_alternative<Filled>(m));
    EXPECT_EQ(std::get<Filled>(m).trades[0].price, 95);
}

TEST(OrderBookPlace, SamePriceAccumulates) {
    OrderBook ob;
    place(ob, 1, 100, 10, Side::BID);
    place(ob, 2, 101, 5,  Side::BID);

    // Combined depth at price 100 is 15. A market sell for 15 should fully fill,
    // touching both orders in arrival order (FIFO).
    auto m = ob.matchMarketOrder(Side::ASK, 15);
    ASSERT_TRUE(std::holds_alternative<Filled>(m));
    auto& f = std::get<Filled>(m);
    EXPECT_EQ(f.executed_quantity, 15u);
    ASSERT_EQ(f.trades.size(), 2u);
    EXPECT_EQ(f.trades[0].quantity, 5u);  // order 2 first (best price)
    EXPECT_EQ(f.trades[1].quantity, 10u); // then order 1
}

// ---- OrderBook::cancel ----

TEST(OrderBookCancel, CancelExistingOrder) {
    OrderBook ob;
    place(ob, 1, 100, 10, Side::BID);
    EXPECT_TRUE(ob.cancel(1));
}

TEST(OrderBookCancel, CancelNonExistentReturnsFalse) {
    OrderBook ob;
    EXPECT_FALSE(ob.cancel(999));
}

TEST(OrderBookCancel, CancelTwiceReturnsFalseOnSecond) {
    OrderBook ob;
    place(ob, 1, 100, 10, Side::BID);
    EXPECT_TRUE(ob.cancel(1));
    EXPECT_FALSE(ob.cancel(1));
}

TEST(OrderBookCancel, CancelReducesAvailableQuantity) {
    OrderBook ob;
    place(ob, 1, 100, 10, Side::BID);
    place(ob, 2, 100, 5,  Side::BID);
    ob.cancel(1);

    // Remaining liquidity at 100 should be 5. A market sell for 10 should
    // partially fill (5 executed, 5 unfilled).
    auto m = ob.matchMarketOrder(Side::ASK, 10);
    ASSERT_TRUE(std::holds_alternative<PartiallyFilled>(m));
    auto& pf = std::get<PartiallyFilled>(m);
    EXPECT_EQ(pf.executed_quantity, 5u);
    EXPECT_EQ(pf.remaining_quantity, 5u);
}

TEST(OrderBookCancel, CancelLastOrderEmptiesSide) {
    OrderBook ob;
    place(ob, 1, 100, 10, Side::BID);
    ob.cancel(1);

    auto m = ob.matchMarketOrder(Side::ASK, 1);
    EXPECT_TRUE(std::holds_alternative<NoLiquidityResult>(m));
}

TEST(OrderBookCancel, CancelRemovesEmptyPriceLevel) {
    OrderBook ob;
    place(ob, 1, 105, 5,  Side::BID);   // would be best
    place(ob, 2, 100, 10, Side::BID);
    ob.cancel(1);                       // 105 level is now empty

    // Best bid should now be 100.
    auto m = ob.matchMarketOrder(Side::ASK, 1);
    ASSERT_TRUE(std::holds_alternative<Filled>(m));
    EXPECT_EQ(std::get<Filled>(m).trades[0].price, 100);
}

TEST(OrderBookCancel, CancelAskOrder) {
    OrderBook ob;
    place(ob, 1, 100, 10, Side::ASK);
    EXPECT_TRUE(ob.cancel(1));

    auto m = ob.matchMarketOrder(Side::BID, 1);
    EXPECT_TRUE(std::holds_alternative<NoLiquidityResult>(m));
}

TEST(OrderBookCancel, FilledOrderIsNotCancellable) {
    OrderBook ob;
    place(ob, 1, 100, 10, Side::ASK);
    ob.matchLimitOrder(makeOrder(2, 100, 10, Side::BID));   // fully consumes id 1
    EXPECT_FALSE(ob.cancel(1));
}

// ---- OrderBook scenarios that go beyond a single op ----

TEST(OrderBookScenario, MarketOrderAgainstEmptySideReturnsNoLiquidity) {
    OrderBook ob;
    auto m = ob.matchMarketOrder(Side::BID, 10);
    EXPECT_TRUE(std::holds_alternative<NoLiquidityResult>(m));
}

TEST(OrderBookScenario, FIFOOrderingWithinPriceLevel) {
    OrderBook ob;
    place(ob, 1, 100, 5, Side::ASK);   // arrives first
    place(ob, 2, 100, 5, Side::ASK);   // arrives second

    // A 5-unit buy at 100 should fully consume order 1 and leave order 2 intact.
    ob.matchLimitOrder(makeOrder(3, 100, 5, Side::BID));

    EXPECT_FALSE(ob.cancel(1));   // gone
    EXPECT_TRUE(ob.cancel(2));    // still resting
}
