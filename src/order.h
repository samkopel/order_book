#pragma once
#include <cstdint>

using Quantity = std::uint64_t;
using OrderId = std::int64_t;
using Price = std::int64_t;

enum class Side { BID = 0, ASK = 1 };
enum class OrderType { GoodForDay, GoodTillCancel };

inline Side invert(Side s)
{
    return s == Side::BID ? Side::ASK : Side::BID;
}

struct Order {
    OrderId id;
    Price price;
    Quantity quantity;
    Side side;
    OrderType order_type;

    Order(OrderId id, Price price, Quantity quantity, Side side,
          OrderType order_type = OrderType::GoodTillCancel):
        id(id), price(price), quantity(quantity), side(side), order_type(order_type) {}

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
