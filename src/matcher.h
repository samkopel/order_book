#pragma once
#include <cstdint>
#include <variant>
#include "order_book.h"

struct Filled {                                                                                                                                            
      Quantity executed_quantity;
      std::vector<Trade> trades;
      Filled(Quantity executed_quantity, std::vector<Trade> trades)
        : executed_quantity(executed_quantity), trades(std::move(trades)) {};
  };

struct PartiallyFilled {
    Quantity executed_quantity;
    Quantity remaining_quantity;
    std::vector<Trade> trades;
    PartiallyFilled(Quantity executed_quantity, Quantity remaining_quantity, std::vector<Trade> trades)
        : executed_quantity(executed_quantity), remaining_quantity(remaining_quantity), trades(std::move(trades)) {};
};

struct OpenResult {
    Quantity executed_quantity;
    OpenResult(Quantity executed_quantity): executed_quantity(executed_quantity) {};
};

struct NoLiquidityResult {
    Quantity unfilled_quantity;
    NoLiquidityResult(Quantity unfilled_quantity): unfilled_quantity{unfilled_quantity} {};
};

using LimitOrderResult = std::variant<Filled, PartiallyFilled, OpenResult>;
using MarketOrderResult = std::variant<Filled, PartiallyFilled, NoLiquidityResult>;

LimitOrderResult limitOrder(OrderBook& order_book, const Order& order);
MarketOrderResult marketOrder(OrderBook& order_book, Side side, Quantity quantity);