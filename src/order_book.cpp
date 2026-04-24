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

    OrderIterator& order_it = map_it->second;
    const Side side  = order_it->side;  
    const Price price = order_it->price;

    PriceLevel& price_level = getPriceLevel(side, price);
    eraseOrder(price_level, order_it);

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
TradeAccumulator OrderBook::trade(std::map<Price, PriceLevel, T>& map, const Order& order)
{
    TradeAccumulator trade_accumulator = TradeAccumulator(order.quantity);
    while (!map.empty() && trade_accumulator.remaining_quantity() > 0)
    {
        auto& [price, level] = *map.begin();

        if (!level.isMatch(order.price)) break;
        
        Quantity remaining_quantity = trade_accumulator.remaining_quantity();
        Quantity traded_quantity = tradeLevelQuantity(level, remaining_quantity);
        trade_accumulator.addTrade(Trade(invert(level.side), level.price, remaining_quantity - traded_quantity));

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