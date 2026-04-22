#pragma once
#include <cstdint>
#include "order_book.h"

enum class OrderStatus {
    FILLED,
    PARTIALLY_FILLED,
    OPEN
};

struct MatchResult { //TODO: make variant
    OrderStatus status;
    Quantity executed_quantity = 0;
    Quantity remaining_quantity = 0;
    std::vector<Trade> trades;

    static MatchResult open(Quantity remaining_quantity) {
        return MatchResult(OrderStatus::OPEN, 0, remaining_quantity, {});
    }

    static MatchResult filled(Quantity executed_quantity, std::vector<Trade> trades) {
        return MatchResult(OrderStatus::FILLED, executed_quantity, 0, std::move(trades));
    }

    static MatchResult partiallyFilled(Quantity executed_quantity, Quantity remaining_quantity, std::vector<Trade> trades) {
        return MatchResult(OrderStatus::PARTIALLY_FILLED, executed_quantity, remaining_quantity, std::move(trades));
    }

private:
    MatchResult(OrderStatus status, Quantity executed_quantity, Quantity remaining_quantity, std::vector<Trade> trades):
        status(status), executed_quantity(executed_quantity), remaining_quantity(remaining_quantity), trades(std::move(trades)) {}
};

MatchResult limitOrder(OrderBook& order_book, const Order& order);
MatchResult marketOrder(OrderBook& order_book, const Side side, const Quantity quantity);