#include "order_book.h"
#include "trade.h"
#include "price_level.h"
#include "order.h"
#include <chrono>

OrderBook::OrderBook()
    : pruner(
          [this] {
              std::scoped_lock lock{ mutex };
              cancelGoodForDayOrders();
          },
          [](GoodForDayPruner::Clock::time_point now) {
              return GoodForDayPruner::nextDailyCutoff(now, std::chrono::hours(16));
          })
{}

void OrderBook::cancelGoodForDayOrders()
{
    cancelGoodForDayOrders(bid_map);
    cancelGoodForDayOrders(ask_map);
}

template<typename T>
void OrderBook::cancelGoodForDayOrders(std::map<Price, PriceLevel, T>& map)
{
    auto map_it = map.begin();
    while (map_it != map.end())
    {
        PriceLevel& level = map_it->second;
        auto order_it = level.orders.begin();
        while (order_it != level.orders.end())
        {
            if (order_it->order_type == OrderType::GoodForDay)
                order_it = eraseOrder(level, order_it);
            else
                ++order_it;
        }

        // Erase by iterator (and capture the next valid iterator) - erasing
        // by key would invalidate map_it and corrupt the loop.
        if (level.isEmpty())
            map_it = map.erase(map_it);
        else
            ++map_it;
    }
}

LimitOrderResult OrderBook::matchLimitOrder(const Order& order)
{
    std::scoped_lock lock{ mutex };

    const PriceLevel* best_level = getBestCounterpartyLevel(order.side);
    if (best_level && order.isMatch(best_level->price))
    {
        TradeAccumulator trade_acc = tradeLimitOrder(order);
        const Quantity remaining = trade_acc.remaining_quantity();
        if (remaining > 0)
        {
            Order remaining_order{order.id, order.price, remaining, order.side, order.order_type};
            add(remaining_order);
            return PartiallyFilled(trade_acc.total_executed, remaining, std::move(trade_acc.trades));
        }
        return Filled(trade_acc.total_executed, std::move(trade_acc.trades));
    }

    add(order);
    return OpenResult(order.quantity);
}

MarketOrderResult OrderBook::matchMarketOrder(Side side, Quantity quantity)
{
    std::scoped_lock lock{ mutex };

    const PriceLevel* best_level = getBestCounterpartyLevel(side);
    if (!best_level)
        return NoLiquidityResult(quantity);

    TradeAccumulator trade_acc = tradeMarketOrder(side, quantity);
    const Quantity remaining = trade_acc.remaining_quantity();
    if (remaining > 0)
        return PartiallyFilled(trade_acc.total_executed, remaining, std::move(trade_acc.trades));
    return Filled(trade_acc.total_executed, std::move(trade_acc.trades));
}

bool OrderBook::cancel(const OrderId id)
{
    std::scoped_lock lock{ mutex };

    const auto map_it = orders_by_id.find(id);
    if (map_it == orders_by_id.end()) return false;

    OrderIterator& order_it = map_it->second;
    const Side  side  = order_it->side;
    const Price price = order_it->price;

    PriceLevel& price_level = getPriceLevel(side, price);
    eraseOrder(price_level, order_it);

    if (price_level.isEmpty())
    {
        if (side == Side::BID) bid_map.erase(price);
        else                   ask_map.erase(price);
    }
    return true;
}

bool OrderBook::add(const Order& order)
{
    if (orders_by_id.find(order.id) != orders_by_id.end()) return false;
    auto& price_level = getPriceLevel(order.side, order.price);
    orders_by_id[order.id] = price_level.addOrder(order);
    return true;
}

TradeAccumulator OrderBook::tradeLimitOrder(const Order& order)
{
    if (order.side == Side::BID)
        return trade(ask_map, order.quantity, [&](Price p){ return order.isMatch(p); });
    else
        return trade(bid_map, order.quantity, [&](Price p){ return order.isMatch(p); });
}

TradeAccumulator OrderBook::tradeMarketOrder(Side side, Quantity quantity)
{
    if (side == Side::BID)
        return trade(ask_map, quantity, [](Price){ return true; });
    else
        return trade(bid_map, quantity, [](Price){ return true; });
}

PriceLevel& OrderBook::getPriceLevel(const Side side, const Price price)
{
    if (side == Side::BID) return getPriceLevel(bid_map, price, side);
    else                   return getPriceLevel(ask_map, price, side);
}

PriceLevel* OrderBook::getBestBidLevel() { return getBestLevel(bid_map); }
PriceLevel* OrderBook::getBestAskLevel() { return getBestLevel(ask_map); }
PriceLevel* OrderBook::getBestCounterpartyLevel(const Side side)
{
    return side == Side::BID ? getBestAskLevel() : getBestBidLevel();
}

template<typename T>
PriceLevel* OrderBook::getBestLevel(std::map<Price, PriceLevel, T>& map)
{
    auto it = map.begin();
    return it != map.end() ? &it->second : nullptr;
}

template<typename T>
PriceLevel& OrderBook::getPriceLevel(std::map<Price, PriceLevel, T>& map, const Price price, const Side side)
{
    auto [it, _] = map.try_emplace(price, side, price);
    return it->second;
}

template<typename T, typename MatchFn>
TradeAccumulator OrderBook::trade(std::map<Price, PriceLevel, T>& map, Quantity quantity, MatchFn isMatch)
{
    TradeAccumulator trade_accumulator{quantity};
    while (!map.empty() && trade_accumulator.remaining_quantity() > 0)
    {
        auto& [price, level] = *map.begin();
        if (!isMatch(price)) break;

        const Quantity remaining = trade_accumulator.remaining_quantity();
        const Quantity leftover  = tradeLevelQuantity(level, remaining);
        trade_accumulator.addTrade(Trade(invert(level.side), level.price, remaining - leftover));

        if (level.isEmpty())
            map.erase(price);
    }
    return trade_accumulator;
}

OrderIterator OrderBook::eraseOrder(PriceLevel& price_level, OrderIterator& it)
{
    OrderId id = it->id;
    auto new_it = price_level.removeOrder(it);
    orders_by_id.erase(id);
    return new_it;
}

Quantity OrderBook::tradeLevelQuantity(PriceLevel& price_level, Quantity quantity_to_trade)
{
    if (quantity_to_trade >= price_level.total_quantity)
    {
        auto& orders = price_level.orders;
        for (auto it = orders.begin(); it != orders.end(); it = eraseOrder(price_level, it))
            quantity_to_trade -= it->quantity;
    }
    else
    {
        while (quantity_to_trade > 0)
        {
            auto order_it = price_level.orders.begin();
            if (quantity_to_trade >= order_it->quantity)
            {
                quantity_to_trade -= order_it->quantity;
                eraseOrder(price_level, order_it);
            }
            else
            {
                quantity_to_trade -= price_level.tradeQuantity(quantity_to_trade, order_it);
            }
        }
    }
    return quantity_to_trade;
}
