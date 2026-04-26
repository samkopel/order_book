#include <gtest/gtest.h>
#include "matcher.h"
#include "order_book.h"

static Order makeOrder(OrderId id, Price price, Quantity qty, Side side) {
    return Order{id, price, qty, side};
}

// ---- limitOrder ----

TEST(LimitOrder, NoMatch_EmptyBook) {
    OrderBook ob;
    auto result = limitOrder(ob, makeOrder(1, 100, 10, Side::BID));
    ASSERT_TRUE(std::holds_alternative<OpenResult>(result));
    EXPECT_EQ(std::get<OpenResult>(result).executed_quantity, 10u);
}

TEST(LimitOrder, NoMatch_PriceTooLow) {
    OrderBook ob;
    ob.add(makeOrder(1, 105, 10, Side::ASK));
    auto result = limitOrder(ob, makeOrder(2, 100, 10, Side::BID));
    ASSERT_TRUE(std::holds_alternative<OpenResult>(result));
}

TEST(LimitOrder, OpenResult_AddsOrderToBook) {
    OrderBook ob;
    limitOrder(ob, makeOrder(1, 100, 10, Side::BID));
    ASSERT_NE(ob.getBestBidLevel(), nullptr);
    EXPECT_EQ(ob.getBestBidLevel()->price, 100);
    EXPECT_EQ(ob.getBestBidLevel()->total_quantity, 10u);
}

TEST(LimitOrder, FullFill) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 10, Side::ASK));
    auto result = limitOrder(ob, makeOrder(2, 100, 10, Side::BID));
    ASSERT_TRUE(std::holds_alternative<Filled>(result));
    auto& filled = std::get<Filled>(result);
    EXPECT_EQ(filled.executed_quantity, 10u);
    ASSERT_EQ(filled.trades.size(), 1u);
    EXPECT_EQ(filled.trades[0].quantity, 10u);
    EXPECT_EQ(filled.trades[0].price, 100);
}

TEST(LimitOrder, FullFill_ClearsBook) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 10, Side::ASK));
    limitOrder(ob, makeOrder(2, 100, 10, Side::BID));
    EXPECT_EQ(ob.getBestAskLevel(), nullptr);
}

TEST(LimitOrder, FullFill_BuyerPriceAboveAsk) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 10, Side::ASK));
    auto result = limitOrder(ob, makeOrder(2, 110, 10, Side::BID));
    ASSERT_TRUE(std::holds_alternative<Filled>(result));
    EXPECT_EQ(std::get<Filled>(result).trades[0].price, 100);
}

TEST(LimitOrder, PartialFill) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 5, Side::ASK));
    auto result = limitOrder(ob, makeOrder(2, 100, 10, Side::BID));
    ASSERT_TRUE(std::holds_alternative<PartiallyFilled>(result));
    auto& pf = std::get<PartiallyFilled>(result);
    EXPECT_EQ(pf.executed_quantity, 5u);
    EXPECT_EQ(pf.remaining_quantity, 5u);
    ASSERT_EQ(pf.trades.size(), 1u);
    EXPECT_EQ(pf.trades[0].quantity, 5u);
}

TEST(LimitOrder, PartialFill_RemainingAddedToBook) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 5, Side::ASK));
    limitOrder(ob, makeOrder(2, 100, 10, Side::BID));
    ASSERT_NE(ob.getBestBidLevel(), nullptr);
    EXPECT_EQ(ob.getBestBidLevel()->price, 100);
    EXPECT_EQ(ob.getBestBidLevel()->total_quantity, 5u);
}

TEST(LimitOrder, SweepsMultipleLevels_Filled) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 5, Side::ASK));
    ob.add(makeOrder(2, 101, 5, Side::ASK));
    auto result = limitOrder(ob, makeOrder(3, 105, 10, Side::BID));
    ASSERT_TRUE(std::holds_alternative<Filled>(result));
    auto& filled = std::get<Filled>(result);
    EXPECT_EQ(filled.executed_quantity, 10u);
    EXPECT_EQ(filled.trades.size(), 2u);
    EXPECT_EQ(ob.getBestAskLevel(), nullptr);
}

TEST(LimitOrder, SweepsMultipleLevels_PartialFill) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 5, Side::ASK));
    ob.add(makeOrder(2, 101, 5, Side::ASK));
    auto result = limitOrder(ob, makeOrder(3, 105, 15, Side::BID));
    ASSERT_TRUE(std::holds_alternative<PartiallyFilled>(result));
    auto& pf = std::get<PartiallyFilled>(result);
    EXPECT_EQ(pf.executed_quantity, 10u);
    EXPECT_EQ(pf.remaining_quantity, 5u);
}

TEST(LimitOrder, SellSide_FullFill) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 10, Side::BID));
    auto result = limitOrder(ob, makeOrder(2, 100, 10, Side::ASK));
    ASSERT_TRUE(std::holds_alternative<Filled>(result));
    EXPECT_EQ(std::get<Filled>(result).executed_quantity, 10u);
}

TEST(LimitOrder, SellSide_NoMatch) {
    OrderBook ob;
    ob.add(makeOrder(1, 90, 10, Side::BID));
    auto result = limitOrder(ob, makeOrder(2, 100, 10, Side::ASK));
    ASSERT_TRUE(std::holds_alternative<OpenResult>(result));
}

TEST(LimitOrder, SellSide_PartialFill) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 5, Side::BID));
    auto result = limitOrder(ob, makeOrder(2, 100, 10, Side::ASK));
    ASSERT_TRUE(std::holds_alternative<PartiallyFilled>(result));
    EXPECT_EQ(std::get<PartiallyFilled>(result).executed_quantity, 5u);
}

// ---- marketOrder ----

TEST(MarketOrder, NoLiquidity_EmptyBook) {
    OrderBook ob;
    auto result = marketOrder(ob, Side::BID, 10);
    ASSERT_TRUE(std::holds_alternative<NoLiquidityResult>(result));
    EXPECT_EQ(std::get<NoLiquidityResult>(result).unfilled_quantity, 10u);
}

TEST(MarketOrder, NoLiquidity_NotAddedToBook) {
    OrderBook ob;
    marketOrder(ob, Side::BID, 10);
    EXPECT_EQ(ob.getBestBidLevel(), nullptr);
    EXPECT_EQ(ob.getBestAskLevel(), nullptr);
}

TEST(MarketOrder, FullFill) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 10, Side::ASK));
    auto result = marketOrder(ob, Side::BID, 10);
    ASSERT_TRUE(std::holds_alternative<Filled>(result));
    auto& filled = std::get<Filled>(result);
    EXPECT_EQ(filled.executed_quantity, 10u);
    ASSERT_EQ(filled.trades.size(), 1u);
    EXPECT_EQ(filled.trades[0].quantity, 10u);
}

TEST(MarketOrder, FullFill_ClearsBook) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 10, Side::ASK));
    marketOrder(ob, Side::BID, 10);
    EXPECT_EQ(ob.getBestAskLevel(), nullptr);
}

TEST(MarketOrder, PartialFill_InsufficientLiquidity) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 5, Side::ASK));
    auto result = marketOrder(ob, Side::BID, 10);
    ASSERT_TRUE(std::holds_alternative<PartiallyFilled>(result));
    auto& pf = std::get<PartiallyFilled>(result);
    EXPECT_EQ(pf.executed_quantity, 5u);
    EXPECT_EQ(pf.remaining_quantity, 5u);
}

TEST(MarketOrder, PartialFill_RemainingNotAddedToBook) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 5, Side::ASK));
    marketOrder(ob, Side::BID, 10);
    EXPECT_EQ(ob.getBestBidLevel(), nullptr);
}

TEST(MarketOrder, SweepsMultipleLevels) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 5, Side::ASK));
    ob.add(makeOrder(2, 101, 5, Side::ASK));
    auto result = marketOrder(ob, Side::BID, 10);
    ASSERT_TRUE(std::holds_alternative<Filled>(result));
    EXPECT_EQ(std::get<Filled>(result).executed_quantity, 10u);
    EXPECT_EQ(ob.getBestAskLevel(), nullptr);
}

TEST(MarketOrder, SellSide_FullFill) {
    OrderBook ob;
    ob.add(makeOrder(1, 100, 10, Side::BID));
    auto result = marketOrder(ob, Side::ASK, 10);
    ASSERT_TRUE(std::holds_alternative<Filled>(result));
    EXPECT_EQ(std::get<Filled>(result).executed_quantity, 10u);
}

TEST(MarketOrder, SellSide_NoLiquidity) {
    OrderBook ob;
    auto result = marketOrder(ob, Side::ASK, 10);
    ASSERT_TRUE(std::holds_alternative<NoLiquidityResult>(result));
    EXPECT_EQ(std::get<NoLiquidityResult>(result).unfilled_quantity, 10u);
}
