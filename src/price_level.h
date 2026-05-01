#pragma once
#include "order.h"

using OrderIterator = std::list<Order>::iterator;

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