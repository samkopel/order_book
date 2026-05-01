#include <gtest/gtest.h>
#include "order_book.h"

static Order makeOrder(OrderId id, Price price, Quantity qty, Side side,
                       OrderType order_type = OrderType::GoodTillCancel) {
    return Order{id, price, qty, side, order_type};
}

// Seed the book with a non-crossing limit order. The book has no public
// "add" - matchLimitOrder against an empty/non-crossing side has the same
// effect (the order rests on the book and OpenResult is returned).
static void seed(OrderBook& ob, OrderId id, Price price, Quantity qty, Side side,
                 OrderType order_type = OrderType::GoodTillCancel) {
    ob.matchLimitOrder(makeOrder(id, price, qty, side, order_type));
}

// ---- matchLimitOrder ----

TEST(LimitOrder, NoMatch_EmptyBook) {
    OrderBook ob;
    auto result = ob.matchLimitOrder(makeOrder(1, 100, 10, Side::BID));
    ASSERT_TRUE(std::holds_alternative<OpenResult>(result));
    EXPECT_EQ(std::get<OpenResult>(result).executed_quantity, 10u);
}

TEST(LimitOrder, NoMatch_PriceTooLow) {
    OrderBook ob;
    seed(ob, 1, 105, 10, Side::ASK);
    auto result = ob.matchLimitOrder(makeOrder(2, 100, 10, Side::BID));
    ASSERT_TRUE(std::holds_alternative<OpenResult>(result));
}

TEST(LimitOrder, OpenResult_OrderRestsAndCanBeMatchedLater) {
    OrderBook ob;
    seed(ob, 1, 100, 10, Side::BID);

    // The bid is on the book. A market sell of 10 at any price should
    // execute against it at price 100.
    auto m = ob.matchMarketOrder(Side::ASK, 10);
    ASSERT_TRUE(std::holds_alternative<Filled>(m));
    auto& f = std::get<Filled>(m);
    ASSERT_EQ(f.trades.size(), 1u);
    EXPECT_EQ(f.trades[0].price, 100);
    EXPECT_EQ(f.trades[0].quantity, 10u);
}

TEST(LimitOrder, FullFill) {
    OrderBook ob;
    seed(ob, 1, 100, 10, Side::ASK);
    auto result = ob.matchLimitOrder(makeOrder(2, 100, 10, Side::BID));
    ASSERT_TRUE(std::holds_alternative<Filled>(result));
    auto& filled = std::get<Filled>(result);
    EXPECT_EQ(filled.executed_quantity, 10u);
    ASSERT_EQ(filled.trades.size(), 1u);
    EXPECT_EQ(filled.trades[0].quantity, 10u);
    EXPECT_EQ(filled.trades[0].price, 100);
}

TEST(LimitOrder, FullFill_ClearsAskSide) {
    OrderBook ob;
    seed(ob, 1, 100, 10, Side::ASK);
    ob.matchLimitOrder(makeOrder(2, 100, 10, Side::BID));

    // Ask side is now empty - a market buy should find no liquidity.
    auto m = ob.matchMarketOrder(Side::BID, 1);
    EXPECT_TRUE(std::holds_alternative<NoLiquidityResult>(m));
}

TEST(LimitOrder, FullFill_BuyerPriceAboveAsk) {
    OrderBook ob;
    seed(ob, 1, 100, 10, Side::ASK);
    auto result = ob.matchLimitOrder(makeOrder(2, 110, 10, Side::BID));
    ASSERT_TRUE(std::holds_alternative<Filled>(result));
    EXPECT_EQ(std::get<Filled>(result).trades[0].price, 100);
}

TEST(LimitOrder, PartialFill) {
    OrderBook ob;
    seed(ob, 1, 100, 5, Side::ASK);
    auto result = ob.matchLimitOrder(makeOrder(2, 100, 10, Side::BID));
    ASSERT_TRUE(std::holds_alternative<PartiallyFilled>(result));
    auto& pf = std::get<PartiallyFilled>(result);
    EXPECT_EQ(pf.executed_quantity, 5u);
    EXPECT_EQ(pf.remaining_quantity, 5u);
    ASSERT_EQ(pf.trades.size(), 1u);
    EXPECT_EQ(pf.trades[0].quantity, 5u);
}

TEST(LimitOrder, PartialFill_RemainderRestsOnBook) {
    OrderBook ob;
    seed(ob, 1, 100, 5, Side::ASK);
    ob.matchLimitOrder(makeOrder(2, 100, 10, Side::BID));   // 5 traded, 5 residual bid at 100

    // A counter market sell of 5 should fully consume the residual at 100.
    auto m = ob.matchMarketOrder(Side::ASK, 5);
    ASSERT_TRUE(std::holds_alternative<Filled>(m));
    auto& f = std::get<Filled>(m);
    ASSERT_EQ(f.trades.size(), 1u);
    EXPECT_EQ(f.trades[0].price, 100);
    EXPECT_EQ(f.trades[0].quantity, 5u);
}

TEST(LimitOrder, SweepsMultipleLevels_Filled) {
    OrderBook ob;
    seed(ob, 1, 100, 5, Side::ASK);
    seed(ob, 2, 101, 5, Side::ASK);
    auto result = ob.matchLimitOrder(makeOrder(3, 105, 10, Side::BID));
    ASSERT_TRUE(std::holds_alternative<Filled>(result));
    auto& filled = std::get<Filled>(result);
    EXPECT_EQ(filled.executed_quantity, 10u);
    ASSERT_EQ(filled.trades.size(), 2u);
    EXPECT_EQ(filled.trades[0].price, 100);   // best ask first
    EXPECT_EQ(filled.trades[1].price, 101);
}

TEST(LimitOrder, SweepsMultipleLevels_PartialFill) {
    OrderBook ob;
    seed(ob, 1, 100, 5, Side::ASK);
    seed(ob, 2, 101, 5, Side::ASK);
    auto result = ob.matchLimitOrder(makeOrder(3, 105, 15, Side::BID));
    ASSERT_TRUE(std::holds_alternative<PartiallyFilled>(result));
    auto& pf = std::get<PartiallyFilled>(result);
    EXPECT_EQ(pf.executed_quantity, 10u);
    EXPECT_EQ(pf.remaining_quantity, 5u);
}

TEST(LimitOrder, DoesNotMatchBeyondIncomingPrice) {
    OrderBook ob;
    seed(ob, 1, 100, 10, Side::ASK);
    seed(ob, 2, 110, 10, Side::ASK);

    // Bid willing to pay only up to 100 - should fill against the 100 ask
    // and stop, leaving 110 untouched.
    auto result = ob.matchLimitOrder(makeOrder(3, 100, 20, Side::BID));
    ASSERT_TRUE(std::holds_alternative<PartiallyFilled>(result));
    auto& pf = std::get<PartiallyFilled>(result);
    EXPECT_EQ(pf.executed_quantity, 10u);
    EXPECT_EQ(pf.remaining_quantity, 10u);
    ASSERT_EQ(pf.trades.size(), 1u);
    EXPECT_EQ(pf.trades[0].price, 100);
}

TEST(LimitOrder, SellSide_FullFill) {
    OrderBook ob;
    seed(ob, 1, 100, 10, Side::BID);
    auto result = ob.matchLimitOrder(makeOrder(2, 100, 10, Side::ASK));
    ASSERT_TRUE(std::holds_alternative<Filled>(result));
    EXPECT_EQ(std::get<Filled>(result).executed_quantity, 10u);
}

TEST(LimitOrder, SellSide_NoMatch) {
    OrderBook ob;
    seed(ob, 1, 90, 10, Side::BID);
    auto result = ob.matchLimitOrder(makeOrder(2, 100, 10, Side::ASK));
    ASSERT_TRUE(std::holds_alternative<OpenResult>(result));
}

TEST(LimitOrder, SellSide_PartialFill) {
    OrderBook ob;
    seed(ob, 1, 100, 5, Side::BID);
    auto result = ob.matchLimitOrder(makeOrder(2, 100, 10, Side::ASK));
    ASSERT_TRUE(std::holds_alternative<PartiallyFilled>(result));
    EXPECT_EQ(std::get<PartiallyFilled>(result).executed_quantity, 5u);
}

TEST(LimitOrder, SellerPriceBelowBidExecutesAtBidPrice) {
    OrderBook ob;
    seed(ob, 1, 100, 10, Side::BID);
    auto result = ob.matchLimitOrder(makeOrder(2, 90, 10, Side::ASK));
    ASSERT_TRUE(std::holds_alternative<Filled>(result));
    EXPECT_EQ(std::get<Filled>(result).trades[0].price, 100);
}

// ---- matchMarketOrder ----

TEST(MarketOrder, NoLiquidity_EmptyBook) {
    OrderBook ob;
    auto result = ob.matchMarketOrder(Side::BID, 10);
    ASSERT_TRUE(std::holds_alternative<NoLiquidityResult>(result));
    EXPECT_EQ(std::get<NoLiquidityResult>(result).unfilled_quantity, 10u);
}

TEST(MarketOrder, NoLiquidityDoesNotResurfaceLater) {
    OrderBook ob;
    ob.matchMarketOrder(Side::BID, 10);

    // Calling again on still-empty book stays NoLiquidity.
    auto m = ob.matchMarketOrder(Side::BID, 1);
    EXPECT_TRUE(std::holds_alternative<NoLiquidityResult>(m));
}

TEST(MarketOrder, FullFill) {
    OrderBook ob;
    seed(ob, 1, 100, 10, Side::ASK);
    auto result = ob.matchMarketOrder(Side::BID, 10);
    ASSERT_TRUE(std::holds_alternative<Filled>(result));
    auto& filled = std::get<Filled>(result);
    EXPECT_EQ(filled.executed_quantity, 10u);
    ASSERT_EQ(filled.trades.size(), 1u);
    EXPECT_EQ(filled.trades[0].quantity, 10u);
}

TEST(MarketOrder, FullFill_ClearsAskSide) {
    OrderBook ob;
    seed(ob, 1, 100, 10, Side::ASK);
    ob.matchMarketOrder(Side::BID, 10);
    auto m = ob.matchMarketOrder(Side::BID, 1);
    EXPECT_TRUE(std::holds_alternative<NoLiquidityResult>(m));
}

TEST(MarketOrder, PartialFill_InsufficientLiquidity) {
    OrderBook ob;
    seed(ob, 1, 100, 5, Side::ASK);
    auto result = ob.matchMarketOrder(Side::BID, 10);
    ASSERT_TRUE(std::holds_alternative<PartiallyFilled>(result));
    auto& pf = std::get<PartiallyFilled>(result);
    EXPECT_EQ(pf.executed_quantity, 5u);
    EXPECT_EQ(pf.remaining_quantity, 5u);
}

TEST(MarketOrder, PartialFill_RemainderIsNotPlaced) {
    OrderBook ob;
    seed(ob, 1, 100, 5, Side::ASK);
    ob.matchMarketOrder(Side::BID, 10);   // 5 unfilled - dropped, NOT placed

    // Bid side should still be empty - market sell finds no liquidity.
    auto m = ob.matchMarketOrder(Side::ASK, 1);
    EXPECT_TRUE(std::holds_alternative<NoLiquidityResult>(m));
}

TEST(MarketOrder, SweepsMultipleLevels) {
    OrderBook ob;
    seed(ob, 1, 100, 5, Side::ASK);
    seed(ob, 2, 101, 5, Side::ASK);
    auto result = ob.matchMarketOrder(Side::BID, 10);
    ASSERT_TRUE(std::holds_alternative<Filled>(result));
    auto& f = std::get<Filled>(result);
    EXPECT_EQ(f.executed_quantity, 10u);
    ASSERT_EQ(f.trades.size(), 2u);
    EXPECT_EQ(f.trades[0].price, 100);
    EXPECT_EQ(f.trades[1].price, 101);
}

TEST(MarketOrder, SellSide_FullFill) {
    OrderBook ob;
    seed(ob, 1, 100, 10, Side::BID);
    auto result = ob.matchMarketOrder(Side::ASK, 10);
    ASSERT_TRUE(std::holds_alternative<Filled>(result));
    EXPECT_EQ(std::get<Filled>(result).executed_quantity, 10u);
}

TEST(MarketOrder, SellSide_NoLiquidity) {
    OrderBook ob;
    auto result = ob.matchMarketOrder(Side::ASK, 10);
    ASSERT_TRUE(std::holds_alternative<NoLiquidityResult>(result));
    EXPECT_EQ(std::get<NoLiquidityResult>(result).unfilled_quantity, 10u);
}
