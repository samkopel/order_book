#include "order_book.h"

bool OrderBook::add(const Order& order)
{
    if (orders_by_id.find(order.id) != orders_by_id.end()) return false;
    auto& price_level = getPriceLevel(order.side, order.price);
    orders_by_id[order.id] = price_level.addOrder(order);
    return true;
}

bool OrderBook::cancel(const OrderId id)
{
    const auto map_it = orders_by_id.find(id);
    if (map_it == orders_by_id.end()) return false;

    const OrderIterator order_it = map_it->second;
    const Side side  = order_it->side;  
    const Price price = order_it->price;

    PriceLevel& price_level = getPriceLevel(side, price);
    price_level.total_quantity -= order_it->remaining_quantity;
    eraseOrder(price_level.orders, order_it);

    if (price_level.isEmpty())
    {
        if (side == Side::BID) 
            bid_map.erase(price);
        else 
            ask_map.erase(price);
    }
    return true;
}

TradeAccumulator OrderBook::tradeQuantity(const Order& order)
{
    if (order.side == Side::BID)
    {
        return trade(ask_map, order);  // buy matches asks
    }
    else
    {
        return trade(bid_map, order);  // sell matches bids
    }
}

PriceLevel& OrderBook::getPriceLevel(const Side side, const Price price)
{
    if (side == Side::BID)
    {
       return getPriceLevel(bid_map, price, side);
    }
    else
    {
        return getPriceLevel(ask_map, price, side);
    }
}

PriceLevel* OrderBook::getBestBidLevel()
{
    return getBestLevel(bid_map);
}

PriceLevel* OrderBook::getBestAskLevel() 
{
    return getBestLevel(ask_map);
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

template<typename T>
TradeAccumulator OrderBook::trade(std::map<Price, PriceLevel, T>& map, const Order& order) {
    TradeAccumulator trade_summary = TradeAccumulator(order.remaining_quantity);
    while (!map.empty() && trade_summary.remaining_quantity() > 0)
    {
        auto& [best_level_price, best_level] = *map.begin();

        if (!best_level.isMatch(order.price)) break;
        
        auto& orders = best_level.orders;
        Quantity level_trade_quantity = 0;
        Quantity quantity_to_trade = trade_summary.remaining_quantity();

        if (quantity_to_trade >= best_level.total_quantity)
        {
            for (auto it = orders.begin(); it != orders.end(); it = eraseOrder(orders, it));
            level_trade_quantity = best_level.total_quantity;
        }
        else
        {
            while (quantity_to_trade > 0)
            {
                auto order_it = best_level.orders.begin();
                Quantity trade_quantity = order_it->tradeQuantity(quantity_to_trade);
                if (order_it->isFilled()) eraseOrder(orders, order_it);
                level_trade_quantity += trade_quantity;
                quantity_to_trade -= trade_quantity;
            }
        }

        trade_summary.addTrade(Trade(invert(best_level.side), best_level.price, level_trade_quantity));

        if (best_level.isEmpty())
            map.erase(best_level_price);
        else
            best_level.total_quantity -= level_trade_quantity;
    }
    return trade_summary;
}

OrderIterator OrderBook::eraseOrder(std::list<Order>& orders, OrderIterator it)
{
    OrderId id = it->id;
    auto new_it = orders.erase(it);
    orders_by_id.erase(id);
    return new_it;
}