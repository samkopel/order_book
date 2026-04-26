#include <gtest/gtest.h>
#include "order_book.h"

static Order makeOrder(OrderId id, Price price, Quantity qty, Side side) {
    return Order{id, price, qty, side};
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

// ---- PriceLevel ----

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

// ---- OrderBook::add ----

TEST(OrderBookAdd, SingleBid) {
    OrderBook ob;
    EXPECT_TRUE(ob.add(makeOrder(1, 100, 10, Side::BID)));
    auto* best = ob.getBestBidLevel();
    ASSERT_NE(best, nullptr);
    EXPECT_EQ(best->price, 100);
    EXPECT_EQ(best->total_quantity, 10u);
}

TEST(OrderBookAdd, SingleAsk) {
    OrderBook ob;
    EXPECT_TRUE(ob.add(makeOrder(1, 100, 10, Side::ASK)));
    auto* best = ob.getBestAskLevel();
    ASSERT_NE(best, nullptr);
    EXPECT_EQ(best->price, 100);
    EXPECT_EQ(best->total_quantity, 10u);
}

TEST(OrderBookAdd, DuplicateIdReturnsFalse) {
    OrderBook ob;
    EXPECT_TRUE(ob.add(makeOrder(1, 100, 10, Side::BID)));
    EXPECT_FALSE(ob.add(makeOrder(1, 100, 10, Side::BID)));
}

TEST(OrderBookAdd, BestBidIsHighestPrice) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 10, Side::BID));
    ob.add(makeOrder(2, 105, 5,  Side::BID));
    ob.add(makeOrder(3, 95,  15, Side::BID));
    EXPECT_EQ(ob.getBestBidLevel()->price, 105);
}

TEST(OrderBookAdd, BestAskIsLowestPrice) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 10, Side::ASK));
    ob.add(makeOrder(2, 95,  5,  Side::ASK));
    ob.add(makeOrder(3, 110, 15, Side::ASK));
    EXPECT_EQ(ob.getBestAskLevel()->price, 95);
}

TEST(OrderBookAdd, SamePriceAccumulatesTotalQuantity) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 10, Side::BID));
    ob.add(makeOrder(2, 100, 5,  Side::BID));
    EXPECT_EQ(ob.getBestBidLevel()->total_quantity, 15u);
}

// ---- OrderBook::cancel ----

TEST(OrderBookCancel, CancelExistingOrder) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 10, Side::BID));
    EXPECT_TRUE(ob.cancel(1));
}

TEST(OrderBookCancel, CancelNonExistentReturnsFalse) {
    OrderBook ob;
    EXPECT_FALSE(ob.cancel(999));
}

TEST(OrderBookCancel, CancelReducesQuantityAtLevel) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 10, Side::BID));
    ob.add(makeOrder(2, 100, 5,  Side::BID));
    ob.cancel(1);
    EXPECT_EQ(ob.getBestBidLevel()->total_quantity, 5u);
}

TEST(OrderBookCancel, CancelLastOrderEmptiesBook) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 10, Side::BID));
    ob.cancel(1);
    EXPECT_EQ(ob.getBestBidLevel(), nullptr);
}

TEST(OrderBookCancel, CancelRemovesEmptyPriceLevel) {
    OrderBook ob;
    ob.add(makeOrder(1, 105, 5,  Side::BID));
    ob.add(makeOrder(2, 100, 10, Side::BID));
    ob.cancel(1);
    EXPECT_EQ(ob.getBestBidLevel()->price, 100);  // 105 level gone
}

TEST(OrderBookCancel, CancelAskOrder) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 10, Side::ASK));
    EXPECT_TRUE(ob.cancel(1));
    EXPECT_EQ(ob.getBestAskLevel(), nullptr);
}

// ---- getBestBidLevel / getBestAskLevel ----

TEST(OrderBookBest, EmptyBookReturnsNull) {
    OrderBook ob;
    EXPECT_EQ(ob.getBestBidLevel(), nullptr);
    EXPECT_EQ(ob.getBestAskLevel(), nullptr);
}

// ---- OrderBook::tradeLimitOrder ----

TEST(OrderBookTrade, NoMatchEmptyBook) {
    OrderBook ob;
    EXPECT_TRUE(ob.tradeLimitOrder(makeOrder(1, 100, 10, Side::BID)).trades.empty());
}

TEST(OrderBookTrade, NoMatchPriceMismatch) {
    OrderBook ob;
    ob.add(makeOrder(1, 105, 10, Side::ASK));
    // buyer willing to pay 100 but ask is 105
    EXPECT_TRUE(ob.tradeLimitOrder(makeOrder(2, 100, 10, Side::BID)).trades.empty());
}

TEST(OrderBookTrade, FullFillSingleAsk) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 10, Side::ASK));
    auto result = ob.tradeLimitOrder(makeOrder(2, 100, 10, Side::BID));
    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].quantity, 10u);
    EXPECT_EQ(result.trades[0].price, 100);
    EXPECT_EQ(result.total_executed, 10u);
    EXPECT_EQ(ob.getBestAskLevel(), nullptr);
}

TEST(OrderBookTrade, PartialFillLeavesResidual) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 20, Side::ASK));
    auto result = ob.tradeLimitOrder(makeOrder(2, 100, 10, Side::BID));
    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].quantity, 10u);
    ASSERT_NE(ob.getBestAskLevel(), nullptr);
    EXPECT_EQ(ob.getBestAskLevel()->total_quantity, 10u);
}

TEST(OrderBookTrade, BuyerPriceAboveAskExecutesAtAskPrice) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 10, Side::ASK));
    auto result = ob.tradeLimitOrder(makeOrder(2, 110, 10, Side::BID));
    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].price, 100);   // executes at resting ask price
    EXPECT_EQ(result.trades[0].quantity, 10u);
}

TEST(OrderBookTrade, SellMatchesBid) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 10, Side::BID));
    auto result = ob.tradeLimitOrder(makeOrder(2, 100, 10, Side::ASK));
    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].quantity, 10u);
    EXPECT_EQ(ob.getBestBidLevel(), nullptr);
}

TEST(OrderBookTrade, SellerPriceBelowBidExecutesAtBidPrice) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 10, Side::BID));
    auto result = ob.tradeLimitOrder(makeOrder(2, 90, 10, Side::ASK));
    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].price, 100);   // executes at resting bid price
}

TEST(OrderBookTrade, SweepsMultiplePriceLevels) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 5, Side::ASK));
    ob.add(makeOrder(2, 101, 5, Side::ASK));
    auto result = ob.tradeLimitOrder(makeOrder(3, 105, 10, Side::BID));
    ASSERT_EQ(result.trades.size(), 2u);
    EXPECT_EQ(result.trades[0].price, 100);
    EXPECT_EQ(result.trades[0].quantity, 5u);
    EXPECT_EQ(result.trades[1].price, 101);
    EXPECT_EQ(result.trades[1].quantity, 5u);
    EXPECT_EQ(result.total_executed, 10u);
    EXPECT_EQ(ob.getBestAskLevel(), nullptr);
}

TEST(OrderBookTrade, DoesNotMatchBeyondIncomingPrice) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 10, Side::ASK));
    ob.add(makeOrder(2, 110, 10, Side::ASK));
    auto result = ob.tradeLimitOrder(makeOrder(3, 100, 20, Side::BID));
    EXPECT_EQ(result.total_executed, 10u);           // only matched the 100 level
    EXPECT_EQ(ob.getBestAskLevel()->price, 110);     // 110 level untouched
}

TEST(OrderBookTrade, FIFOOrderingWithinPriceLevel) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 5, Side::ASK));  // arrives first
    ob.add(makeOrder(2, 100, 5, Side::ASK));  // arrives second
    ob.tradeLimitOrder(makeOrder(3, 100, 5, Side::BID));  // buy 5 — should fill order 1
    EXPECT_FALSE(ob.cancel(1));  // order 1 was filled and removed
    EXPECT_TRUE(ob.cancel(2));   // order 2 is still resting
}

TEST(OrderBookTrade, MultipleOrdersAtSameLevelPartialSweep) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 5, Side::ASK));
    ob.add(makeOrder(2, 100, 5, Side::ASK));
    auto result = ob.tradeLimitOrder(makeOrder(3, 100, 8, Side::BID));
    EXPECT_EQ(result.total_executed, 8u);
    // 2 units remain at price 100
    ASSERT_NE(ob.getBestAskLevel(), nullptr);
    EXPECT_EQ(ob.getBestAskLevel()->total_quantity, 2u);
}

TEST(OrderBookTrade, FilledOrdersRemovedFromCancelIndex) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 10, Side::ASK));
    ob.tradeLimitOrder(makeOrder(2, 100, 10, Side::BID));
    EXPECT_FALSE(ob.cancel(1));  // fully filled, not in the book anymore
}
