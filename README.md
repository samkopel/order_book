# Order Book

A price-time priority matching engine in C++17. Limit and market orders, FIFO within price levels, multi-level sweeps, and type-safe match outcomes via `std::variant`.

This is a work in progress. The matching engine and order book are complete and tested; an HTTP layer (`OrderBookSummary`, `add`, `cancel`) is being built on top.

## What's here

- **`OrderBook`** — bid/ask sides backed by `std::map<Price, PriceLevel>` with custom comparators so `begin()` is always the best price. `unordered_map<OrderId, iterator>` indexes resting orders for O(1) cancel.
- **`PriceLevel`** — `std::list<Order>` for FIFO ordering and stable iterators (so cancellation never invalidates the order index).
- **`limitOrder` / `marketOrder`** — high-level entry points returning `std::variant`s that distinguish each outcome at the type level.

## Design notes

**Match outcomes are sum types, not status enums.**

```cpp
using LimitOrderResult  = std::variant<Filled, PartiallyFilled, OpenResult>;
using MarketOrderResult = std::variant<Filled, PartiallyFilled, NoLiquidityResult>;
```

Each alternative carries only the fields that are valid for that outcome — `Filled` has no "remaining quantity," `OpenResult` has no trades. Callers handle every case via `std::visit` or `std::holds_alternative`; forgetting a case is a compile error.

**No virtual dispatch in the matching loop.**

The shared matching loop is parameterized by a predicate rather than a polymorphic order type:

```cpp
template<typename T, typename MatchFn>
TradeAccumulator trade(std::map<Price, PriceLevel, T>& map, Quantity qty, MatchFn isMatch);
```

Limit orders pass `[&](Price p){ return order.isMatch(p); }`; market orders pass `[](Price){ return true; }`. The lambdas inline at compile time — zero overhead, no `IOrder` base class.

**Custom comparators give O(1) best-price lookup.**

```cpp
using BidMap = std::map<Price, PriceLevel, std::greater<Price>>;  // highest first
using AskMap = std::map<Price, PriceLevel, std::less<Price>>;     // lowest first
```

`map.begin()` is always the best price on either side.

## API

```cpp
// Public OrderBook surface
bool add(const Order& order);
bool cancel(OrderId id);
TradeAccumulator tradeLimitOrder(const Order& order);
TradeAccumulator tradeMarketOrder(Side side, Quantity quantity);

// Matcher entry points
LimitOrderResult  limitOrder(OrderBook&, const Order&);
MarketOrderResult marketOrder(OrderBook&, Side, Quantity);
```

Example:

```cpp
OrderBook ob;
ob.add({1, 100, 10, Side::ASK});

auto result = limitOrder(ob, {2, 100, 10, Side::BID});
std::visit([](auto&& r) {
    using T = std::decay_t<decltype(r)>;
    if constexpr (std::is_same_v<T, Filled>) {
        // r.executed_quantity, r.trades
    } else if constexpr (std::is_same_v<T, PartiallyFilled>) {
        // r.executed_quantity, r.remaining_quantity, r.trades
    } else if constexpr (std::is_same_v<T, OpenResult>) {
        // r.executed_quantity (resting quantity)
    }
}, result);
```

## Building

Dependencies are managed with [vcpkg](https://vcpkg.io) (manifest mode):

- `nlohmann_json`
- `gtest`
- `benchmark`
- `cpp-httplib`

```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

## Tests

```bash
cd build && ctest --output-on-failure
```

Coverage spans `Order` and `PriceLevel` invariants, `OrderBook` add/cancel/match behavior including FIFO ordering and multi-level sweeps, and both `limitOrder` and `marketOrder` outcome variants.

## Benchmarks

Built with Google Benchmark. Run: `./build/order_book_bench`.

![Performance results](images/performance%20results.jpg)

Cancel is the slowest single-order path — two lookups (`orders_by_id` then `getPriceLevel`) where one would do. Storing the price-level pointer alongside the iterator collapses it to one hop (see roadmap).

## Roadmap

### Engine
- Modify order (in-place replace, avoiding cancel + add round-trip)
- Order timestamps for audit/replay
- Cancel optimization: store price-level pointer in the id index to skip the second lookup
- Hot array for price ticks near the current SOD price (avoid map overhead in the dense region)

### Order types
- Good-til-cancelled / expiring orders
- All-or-nothing
- Stop-limit (needs analysis)

### API
- HTTP layer for add / cancel / order-book summary

## Layout

```
src/
  order_book.{h,cpp}    # OrderBook, PriceLevel, Order, Trade
  matcher.{h,cpp}       # limitOrder / marketOrder + result variants
  main.cpp              # HTTP server (WIP)
tests/
  unit/                 # GTest unit tests
  bench/                # Google Benchmark suite
```