#include "order_book.h"

bool OrderBook::add(const Order& order)
{
    if (orders_by_id.find(order.id) != orders_by_id.end()) return false;
    auto& price_level = getPriceLevel(order.side, order.price);
    auto order_it = price_level.addOrder(order);
    OrderRecord order_record = OrderRecord(&price_level, order_it);
    orders_by_id.try_emplace(order.id, order_record);
    return true;
}

bool OrderBook::cancel(const OrderId id)
{
    const auto map_it = orders_by_id.find(id);
    if (map_it == orders_by_id.end()) return false;

    PriceLevel& price_level = *map_it->second.price_level;
    const Side side  = price_level.side;
    const Price price = price_level.price;

    price_level.removeOrder(map_it->second.order_it);
    orders_by_id.erase(map_it);

    if (price_level.isEmpty())
    {
        if (side == Side::BID)
            bid_map.erase(price);
        else
            ask_map.erase(price);
    }
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

        Quantity remaining = trade_accumulator.remaining_quantity();
        Quantity leftover = tradeLevelQuantity(level, remaining);
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
        {
            quantity_to_trade -= it->quantity;
        }
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