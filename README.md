# Order Book

A price-time priority matching engine in C++17. Limit and market orders, FIFO within price levels, and type-safe match outcomes via `std::variant`.

Work in progress — the matching engine and order book are complete and tested; an HTTP layer is being built on top.

## What's here

- **`OrderBook`** — bid/ask sides backed by `std::map<Price, PriceLevel>` with custom comparators so `begin()` is always the best price. An `unordered_map<OrderId, OrderRecord>` indexes resting orders, where each record holds both the list iterator and a pointer to its price level — so cancel is one hash lookup with no map traversal.
- **`PriceLevel`** — `std::list<Order>` for FIFO ordering and stable iterators (cancellation never invalidates the order index).
- **`limitOrder` / `marketOrder`** — high-level entry points returning `std::variant`s that distinguish each outcome at the type level.

## Design notes

**Match outcomes are sum types, not status enums.**

```cpp
using LimitOrderResult  = std::variant<Filled, PartiallyFilled, OpenResult>;
using MarketOrderResult = std::variant<Filled, PartiallyFilled, NoLiquidityResult>;
```

Each alternative carries only the fields that are valid for that outcome — `Filled` has no remaining quantity, `OpenResult` has no trades. Forgetting a case in `std::visit` is a compile error.

**No virtual dispatch in the matching loop.**

The shared matching loop is parameterized by a predicate rather than a polymorphic order type:

```cpp
template<typename T, typename MatchFn>
TradeAccumulator trade(std::map<Price, PriceLevel, T>& map, Quantity qty, MatchFn isMatch);
```

Limit orders pass `[&](Price p){ return order.isMatch(p); }`; market orders pass `[](Price){ return true; }`. The lambdas inline at compile time — zero runtime cost, no `IOrder` base class.

**Custom comparators give O(1) best-price lookup.**

```cpp
using BidMap = std::map<Price, PriceLevel, std::greater<Price>>;  // highest first
using AskMap = std::map<Price, PriceLevel, std::less<Price>>;     // lowest first
```

`map.begin()` is always the best price.

## API

```cpp
bool add(const Order& order);
bool cancel(OrderId id);
TradeAccumulator tradeLimitOrder(const Order& order);
TradeAccumulator tradeMarketOrder(Side side, Quantity quantity);

LimitOrderResult  limitOrder(OrderBook&, const Order&);
MarketOrderResult marketOrder(OrderBook&, Side, Quantity);
```

Example:

```cpp
OrderBook ob;
ob.add({1, 100, 10, Side::ASK});

auto result = limitOrder(ob, {2, 100, 10, Side::BID});

if (auto* f = std::get_if<Filled>(&result)) {
    // f->executed_quantity, f->trades
} else if (auto* p = std::get_if<PartiallyFilled>(&result)) {
    // p->executed_quantity, p->remaining_quantity, p->trades
} else if (auto* o = std::get_if<OpenResult>(&result)) {
    // o->executed_quantity (resting quantity)
}
```

## Building

Dependencies via [vcpkg](https://vcpkg.io) (manifest mode): `nlohmann_json`, `gtest`, `benchmark`, `cpp-httplib`.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

## Tests

```bash
cd build && ctest --output-on-failure
```

Coverage spans `Order` matching logic, `PriceLevel` invariants, `OrderBook` add/cancel/match behavior including FIFO ordering and multi-level sweeps, and both matcher entry points across all variant outcomes.

## Benchmarks

Built with Google Benchmark. Run: `./build/order_book_bench`.

![Performance results](images/performance%20results.jpg)

## Roadmap

### Engine
- Modify order (in-place replace, avoiding cancel + add round-trip)
- Order timestamps for audit/replay
- Investigate cancel performance — likely allocator-bound; needs profiling
- Hot array for price ticks near the SOD price (avoid map overhead in the dense region)
- Pre-allocated `TradeAccumulator` to avoid per-match heap allocation

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
