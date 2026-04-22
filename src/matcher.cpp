#include <cstdint>
#include "matcher.h"
#include "order_book.h"

MatchResult limitOrder(OrderBook& order_book, const Order& order)
{
    auto best_level = order.side == Side::BID ? order_book.getBestAskLevel() : order_book.getBestBidLevel();
    if (best_level && best_level->isMatch(order.price))
    {
        TradeAccumulator trade_summary = order_book.tradeQuantity(order);
        auto remaining_quantity = trade_summary.remaining_quantity();
        if (remaining_quantity > 0)
        {
            auto remaining_order = Order(order, remaining_quantity);
            order_book.add(remaining_order);
            return MatchResult::partiallyFilled(trade_summary.total_executed, remaining_quantity, std::move(trade_summary.trades));
        }
        return MatchResult::filled(trade_summary.total_executed, std::move(trade_summary.trades));
    } else
    {
        order_book.add(order);
        return MatchResult::open(order.remaining_quantity);
    }
};

MatchResult marketOrder(OrderBook& order_book, const Side side, const Quantity quantity);