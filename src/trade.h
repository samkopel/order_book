#pragma once
#include "vector"
#include "order.h"

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
