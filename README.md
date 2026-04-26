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

Built with Google Benchmark. Two suites: `bench_order_book.cpp` measures `OrderBook` directly; `bench_matchers.cpp` measures the public matcher entry points (`limitOrder` / `marketOrder`) so the variant construction and re-insert costs are visible.

```bash
./build/order_book_bench
./build/order_book_bench --benchmark_filter="LimitOrder|MarketOrder"
```

### Engine layer (`OrderBook`)

| Operation                            | Time per op  |
|--------------------------------------|--------------|
| Trade (partial fill, no resize)      | 24.9 ns      |
| Mixed (add + trade + cancel)         | 61.0 ns      |
| Add (batch, amortized over 1k–100k)  | 57–69 ns     |
| Add (single, into warm book)         | 79.5 ns      |
| Cancel (from warm book)              | 169 ns       |
| Trade sweep (1 / 10 / 100 / 1000 levels) | 190 / 600 / 4 240 / 43 000 ns |

### Matcher layer (`limitOrder` / `marketOrder`)

| Operation                       | Time per op | Note |
|---------------------------------|-------------|------|
| `marketOrder` — no liquidity    | 1.15 ns     | empty-book detect + variant ctor — the floor |
| `marketOrder` — full fill       | 33.0 ns     | matcher adds ~8 ns over `tradeQuantity` direct |
| `limitOrder` — full fill        | 33.7 ns     | parallel to market full fill — lambda inlines cleanly |
| `limitOrder` — open             | 81.0 ns     | dominated by `add`, as expected |
| `limitOrder` — partial fill     | 349 ns      | includes residual cleanup; effective cost ≈ 180 ns |
| `marketOrder` — sweep (1 / 10 / 100 / 1000) | 186 / 579 / 4 239 / 42 542 ns | tracks engine sweep almost exactly — variant cost amortizes away |

### Observations

- **The lambda predicate works.** `limitOrder` full-fill (33.7 ns) and `marketOrder` full-fill (33.0 ns) sit within noise of each other. The compile-time dispatch via the match lambda is paying off — there's no measurable overhead from going through the `Order::isMatch` path vs. the `[](Price){ return true; }` path.
- **Matcher overhead is ~8 ns.** Direct `tradeLimitOrder` is 24.9 ns; matcher full-fill is 33.7 ns. That ~8 ns is the variant construction and the best-level pre-check.
- **Cancel is still the slowest single-order path** at 169 ns. Currently does `orders_by_id` lookup → `getPriceLevel` lookup → erase from list + map. Storing the price-level pointer alongside the iterator in `orders_by_id` would collapse this to one hop (see roadmap).
- **Sweeps amortize cleanly to ~43 ns/level at 1000 levels** — that's the per-level work (map iteration, level erase, trade record).
- **Partial-fill matcher number is misleading.** `BM_LimitOrder_PartialFill` cancels the residual every iteration as cleanup, so it bakes in the 169 ns cancel cost. The actual partial-fill matcher cost is closer to 180 ns. Worth replacing the cleanup with a different strategy (e.g. burst then reset book) for a cleaner number.

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