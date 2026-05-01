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
#include "match_types.h"

// Thread-safety: every public method acquires the internal mutex for its
// duration. Callers do not need (and cannot) externally synchronize.
//
// The "Best level" accessors (getBestBidLevel / getBestAskLevel /
// getBestCounterpartyLevel) return raw pointers into the book's maps. They
// are intended for single-threaded inspection (e.g. tests, or a single owner
// thread between mutating calls). Concurrent mutators may invalidate the
// returned pointer; do not call these from multiple threads without external
// coordination.
class OrderBook {
public:
    using OrdersById = std::unordered_map<OrderId, OrderIterator>;
    template<typename T> using OrderMap = std::map<Price, PriceLevel, T>;
    using BidMap = OrderMap<std::greater<Price>>;
    using AskMap = OrderMap<std::less<Price>>;

    OrderBook() = default;

    // Mutating entry points - all take the mutex.
    LimitOrderResult  matchLimitOrder(const Order& order);
    MarketOrderResult matchMarketOrder(Side side, Quantity quantity);
    bool add(const Order& order);
    bool cancel(OrderId id);
    void pruneGoodForDayOrders();

    // Trade-only entry points: match against the book and return trades,
    // but do NOT add a residual order back to the book on partial fill.
    // Useful for tests that exercise the matching primitive directly.
    TradeAccumulator tradeLimitOrder(const Order& order);
    TradeAccumulator tradeMarketOrder(Side side, Quantity quantity);

    // See "thread-safety" note above.
    PriceLevel* getBestBidLevel();
    PriceLevel* getBestAskLevel();
    PriceLevel* getBestCounterpartyLevel(Side side);

private:
    // Unlocked variants - precondition: caller holds `mutex`.
    bool addUnlocked(const Order& order);
    bool cancelUnlocked(OrderId id);
    TradeAccumulator tradeLimitOrderUnlocked(const Order& order);
    TradeAccumulator tradeMarketOrderUnlocked(Side side, Quantity quantity);
    void pruneGoodForDayOrdersUnlocked();

    PriceLevel& getPriceLevel(Side side, Price price);
    template<typename T> PriceLevel& getPriceLevel(std::map<Price, PriceLevel, T>& map, Price price, Side side);
    template<typename T> PriceLevel* getBestLevel(std::map<Price, PriceLevel, T>& map);
    template<typename T, typename MatchFn> TradeAccumulator trade(std::map<Price, PriceLevel, T>& map, Quantity qty, MatchFn isMatch);
    OrderIterator eraseOrder(PriceLevel& price_level, OrderIterator& it);
    Quantity tradeLevelQuantity(PriceLevel& price_level, Quantity quantity);
    template<typename T> void pruneGoodForDayOrdersIn(std::map<Price, PriceLevel, T>& map);

    BidMap bid_map;
    AskMap ask_map;
    OrdersById orders_by_id;
    std::mutex mutex;
};
