#pragma once
#include <cstdint>
#include <unordered_map>
#include <list>
#include <vector>
#include <map>

using Quantity = std::uint64_t;
using OrderId = std::int64_t;
using Price = std::int64_t;
using Timestamp = std::uint64_t;

enum class Side { BID = 0, ASK = 1 };

inline Side invert(Side s) 
{
    return s == Side::BID ? Side::ASK : Side::BID;
}

struct Order {
    OrderId id;
    Price price;
    Quantity remaining_quantity;
    Side side;
    //TODO: add timestamp

    Order(OrderId id, Price price, Quantity remaining_quantity, Side side):
        id(id), price(price), remaining_quantity(remaining_quantity), side(side) {}

    Order(const Order& old_order, Quantity unfilled_quantity):
        id(old_order.id), price(old_order.price), remaining_quantity(unfilled_quantity), side(old_order.side) {}

    bool isFilled() const
    {
        return remaining_quantity == 0;
    }

    Quantity tradeQuantity(const Quantity incoming_quantity)
    {
        if (incoming_quantity >= remaining_quantity)
        {
            Quantity old_quantity = remaining_quantity;
            remaining_quantity = 0;
            return old_quantity;
        }
        else
        {
            remaining_quantity -= incoming_quantity;
            return incoming_quantity;
        }
    }
};

using OrderIterator = std::list<Order>::iterator;

struct Trade {
    Side side;
    Price price;
    Quantity quantity;

    Trade(const Side side, const Price price, const Quantity quantity):
        side(side), price(price), quantity(quantity) {}

};

struct TradeAccumulator
{
    Quantity initial_quantity;
    Quantity total_executed = 0;
    std::vector<Trade> trades;

    explicit TradeAccumulator(Quantity initial_quantity) : initial_quantity(initial_quantity) {}

    Quantity remaining_quantity() const { return initial_quantity - total_executed; }

    void addTrade(Trade trade)
    {
        total_executed += trade.quantity;
        trades.push_back(std::move(trade));
    }
};

struct PriceLevel {
    Price price;
    Quantity total_quantity;
    std::list<Order> orders; //TODO: use deque
    Side side;

    PriceLevel(const Side side, const Price price):
        price(price), total_quantity(), orders(), side(side) {}

    bool isEmpty() const
    {
        return orders.empty();
    }

    OrderIterator addOrder(const Order& order)
    {
        total_quantity += order.remaining_quantity;
        return orders.insert(orders.end(), order);
    }

    void removeOrder(const OrderIterator it) {
        total_quantity -= it->remaining_quantity;
        orders.erase(it);
    }

    bool isMatch(const Price incoming_price) const
    {
        if (side == Side::BID)
        {
            return price >= incoming_price; 
        }
        else
        {
            return incoming_price >= price;
        }
    }

};

class OrderBook {
public:
    
    using OrdersById = std::unordered_map<OrderId, OrderIterator>;
    template<typename T> using OrderMap = std::map<Price, PriceLevel, T>;
    using BidMap = OrderMap<std::greater<Price>>; //TODO add hot array for price ticks close to sod price
    using AskMap = OrderMap<std::less<Price>>;

    OrderBook() = default;

    bool add(const Order& order);
    bool cancel(const OrderId id);
    TradeAccumulator tradeQuantity(const Order& order);
    PriceLevel* getBestBidLevel();
    PriceLevel* getBestAskLevel();
    
private:
    PriceLevel& getPriceLevel(const Side side, const Price price);
    template<typename T> PriceLevel& getPriceLevel(std::map<Price, PriceLevel, T>& map, const Price price, const Side side);
    template<typename T> PriceLevel* getBestLevel(std::map<Price, PriceLevel, T>& map);
    template<typename T> TradeAccumulator trade(std::map<Price, PriceLevel, T>& map, const Order& order); //TODO: heap allocated on every match, pass in pre-allocated output param
    OrderIterator eraseOrder(std::list<Order>& orders, OrderIterator it);
    BidMap bid_map;
    AskMap ask_map;
    OrdersById orders_by_id;
};