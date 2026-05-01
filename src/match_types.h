#pragma once
#include <cstdint>
#include <variant>
#include "order.h"
#include "vector"
#include "trade.h"

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