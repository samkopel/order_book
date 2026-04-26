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
    Quantity quantity;
    Side side;

    Order(OrderId id, Price price, Quantity quantity, Side side):
        id(id), price(price), quantity(quantity), side(side) {}

    bool isMatch(const Price level_price) const
    {
        if (side == Side::ASK)
            return level_price >= price;
        else
            return price >= level_price;
    }

    bool isFilled() const
    {
        return quantity == 0;
    }

    Quantity tradeQuantity(const Quantity incoming_quantity)
    {
        if (incoming_quantity >= quantity)
        {
            Quantity traded = quantity;
            quantity = 0;
            return traded;
        }
        else
        {
            quantity -= incoming_quantity;
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
    std::list<Order> orders;
    Side side;

    PriceLevel(const Side side, const Price price):
        price(price), total_quantity(), orders(), side(side) {}

    bool isEmpty() const { return orders.empty(); }

    OrderIterator addOrder(const Order& order)
    {
        total_quantity += order.quantity;
        return orders.insert(orders.end(), order);
    }

    OrderIterator removeOrder(const OrderIterator it)
    {
        total_quantity -= it->quantity;
        return orders.erase(it);
    }

    Quantity tradeQuantity(Quantity incoming_quantity, OrderIterator& it)
    {
        Quantity traded = it->tradeQuantity(incoming_quantity);
        total_quantity -= traded;
        return traded;
    }
};

class OrderBook {
public:
    using OrdersById = std::unordered_map<OrderId, OrderIterator>;
    template<typename T> using OrderMap = std::map<Price, PriceLevel, T>;
    using BidMap = OrderMap<std::greater<Price>>;
    using AskMap = OrderMap<std::less<Price>>;

    OrderBook() = default;

    bool add(const Order& order);
    bool cancel(const OrderId id);
    TradeAccumulator tradeLimitOrder(const Order& order);
    TradeAccumulator tradeMarketOrder(Side side, Quantity quantity);
    PriceLevel* getBestBidLevel();
    PriceLevel* getBestAskLevel();
    PriceLevel* getBestCounterpartyLevel(const Side side);

private:
    PriceLevel& getPriceLevel(const Side side, const Price price);
    template<typename T> PriceLevel& getPriceLevel(std::map<Price, PriceLevel, T>& map, const Price price, const Side side);
    template<typename T> PriceLevel* getBestLevel(std::map<Price, PriceLevel, T>& map);
    template<typename T, typename MatchFn> TradeAccumulator trade(std::map<Price, PriceLevel, T>& map, Quantity qty, MatchFn isMatch);
    OrderIterator eraseOrder(PriceLevel& price_level, OrderIterator& it);
    Quantity tradeLevelQuantity(PriceLevel& price_level, Quantity quantity);
    BidMap bid_map;
    AskMap ask_map;
    OrdersById orders_by_id;
};
