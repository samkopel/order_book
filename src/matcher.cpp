#include <cstdint>
#include "matcher.h"
#include "order_book.h"

LimitOrderResult limitOrder(OrderBook& order_book, const Order& order)
{
    const PriceLevel* best_level = order_book.getBestCounterpartyLevel(order.side);
    if (best_level && order.isMatch(best_level->price))
    {
        TradeAccumulator trade_acc = order_book.tradeLimitOrder(order);
        auto remaining_quantity = trade_acc.remaining_quantity();
        if (remaining_quantity > 0)
        {
            auto remaining_order = Order(order.id, order.price, remaining_quantity, order.side);
            order_book.add(remaining_order);
            return PartiallyFilled(trade_acc.total_executed, remaining_quantity, std::move(trade_acc.trades));
        } else {
            return Filled(trade_acc.total_executed, std::move(trade_acc.trades));
        }
    } else
    {
        order_book.add(order);
        return OpenResult(order.quantity);
    }
}

MarketOrderResult marketOrder(OrderBook& order_book, Side side, Quantity quantity)
{
    const PriceLevel* best_level = order_book.getBestCounterpartyLevel(side);
    if (best_level)
    {
        TradeAccumulator trade_acc = order_book.tradeMarketOrder(side, quantity);
        auto remaining_quantity = trade_acc.remaining_quantity();
        if (remaining_quantity > 0)
            return PartiallyFilled(trade_acc.total_executed, remaining_quantity, std::move(trade_acc.trades));
        else
            return Filled(trade_acc.total_executed, std::move(trade_acc.trades));
    } else
    {
        return NoLiquidityResult(quantity);
    }
}
