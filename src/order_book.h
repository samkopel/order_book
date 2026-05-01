#pragma once
#include <cstdint>
#include <unordered_map>
#include <list>
#include <vector>
#include <map>
#include <mutex>
#include "order.h"
#include "price_level.h"
#include "trade.h"
#include "day_order_pruner.h"
#include "match_types.h"

class OrderBook {
public:
    using OrdersById = std::unordered_map<OrderId, OrderIterator>;
    template<typename T> using OrderMap = std::map<Price, PriceLevel, T>;
    using BidMap = OrderMap<std::greater<Price>>;
    using AskMap = OrderMap<std::less<Price>>;

    OrderBook();

    LimitOrderResult  matchLimitOrder(const Order& order);
    MarketOrderResult matchMarketOrder(Side side, Quantity quantity);
    bool cancel(const OrderId id);

private:
    TradeAccumulator tradeLimitOrder(const Order& order);
    TradeAccumulator tradeMarketOrder(Side side, Quantity quantity);
    template<typename T, typename MatchFn> TradeAccumulator trade(std::map<Price, PriceLevel, T>& map, Quantity qty, MatchFn isMatch);
    Quantity tradeLevelQuantity(PriceLevel& price_level, Quantity quantity);
    
    PriceLevel* getBestBidLevel();
    PriceLevel* getBestAskLevel();
    PriceLevel* getBestCounterpartyLevel(const Side side);
   
    template<typename T> PriceLevel* getBestLevel(std::map<Price, PriceLevel, T>& map);
    PriceLevel& getPriceLevel(const Side side, const Price price);
    template<typename T> PriceLevel& getPriceLevel(std::map<Price, PriceLevel, T>& map, const Price price, const Side side);
    
    bool add(const Order& order);
    OrderIterator eraseOrder(PriceLevel& price_level, OrderIterator& it);

    void cancelGoodForDayOrders();
    template<typename T> void cancelGoodForDayOrders(std::map<Price, PriceLevel, T>& map);

    BidMap bid_map;
    AskMap ask_map;
    OrdersById orders_by_id;
    std::mutex mutex;
    GoodForDayPruner pruner;
};