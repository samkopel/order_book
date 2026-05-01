#include <benchmark/benchmark.h>
#include "order_book.h"
#include <limits>

static Order makeOrder(OrderId id, Price price, Quantity qty, Side side,
                       OrderType order_type = OrderType::GoodTillCancel) {
    return Order{id, price, qty, side, order_type};
}

// Seed the book with a non-crossing limit order. The book has no public
// "add" - matchLimitOrder against an empty/non-crossing side has the same
// effect (the order rests on the book and OpenResult is returned).
static void seed(OrderBook& ob, OrderId id, Price price, Quantity qty, Side side,
                 OrderType order_type = OrderType::GoodTillCancel) {
    ob.matchLimitOrder(makeOrder(id, price, qty, side, order_type));
}

// ---- matchLimitOrder ----

// No-match path: incoming bid doesn't cross the resting ask, so the order
// is added to the book and an OpenResult is returned.
static void BM_LimitOrder_Open(benchmark::State& state) {
    OrderBook ob;
    seed(ob, 1, 200, 1'000'000, Side::ASK);  // ask far above any incoming bid
    OrderId id = 2;
    for (auto _ : state) {
        benchmark::DoNotOptimize(ob.matchLimitOrder({id, 100, 10, Side::BID}));
        ++id;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_LimitOrder_Open);

// Full-fill path: incoming bid fully consumed by a single resting ask.
// The resting ask has effectively unlimited size so it's never exhausted.
static void BM_LimitOrder_FullFill(benchmark::State& state) {
    OrderBook ob;
    seed(ob, 1, 100, std::numeric_limits<Quantity>::max() / 2, Side::ASK);
    OrderId id = 2;
    for (auto _ : state) {
        benchmark::DoNotOptimize(ob.matchLimitOrder({id, 100, 10, Side::BID}));
        ++id;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_LimitOrder_FullFill);

// Partial-fill path: incoming bid matches a thin ask, then the residual
// is added back to the book. Cleans up the residual each iteration.
static void BM_LimitOrder_PartialFill(benchmark::State& state) {
    OrderBook ob;
    OrderId resting_id = 0;
    OrderId incoming_id = 1'000'000;
    for (auto _ : state) {
        state.PauseTiming();
        seed(ob, resting_id++, 100, 5, Side::ASK);
        state.ResumeTiming();

        benchmark::DoNotOptimize(ob.matchLimitOrder({incoming_id, 100, 10, Side::BID}));

        state.PauseTiming();
        ob.cancel(incoming_id);  // remove the residual so the book stays bounded
        ++incoming_id;
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_LimitOrder_PartialFill);

// ---- matchMarketOrder ----

// Full-fill path: deep ask, market buy fully consumed.
static void BM_MarketOrder_FullFill(benchmark::State& state) {
    OrderBook ob;
    seed(ob, 1, 100, std::numeric_limits<Quantity>::max() / 2, Side::ASK);
    for (auto _ : state) {
        benchmark::DoNotOptimize(ob.matchMarketOrder(Side::BID, 10));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MarketOrder_FullFill);

// No-liquidity path: empty book, market order returns NoLiquidityResult.
// Useful as a floor — measures the cost of detecting empty book + variant ctor.
static void BM_MarketOrder_NoLiquidity(benchmark::State& state) {
    OrderBook ob;
    for (auto _ : state) {
        benchmark::DoNotOptimize(ob.matchMarketOrder(Side::BID, 10));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MarketOrder_NoLiquidity);

// Sweep path: market buy sweeps N ask price levels.
// Each iteration rebuilds the book; the sweep itself is what's timed.
static void BM_MarketOrder_Sweep(benchmark::State& state) {
    const int N = static_cast<int>(state.range(0));
    for (auto _ : state) {
        state.PauseTiming();
        OrderBook ob;
        for (int i = 0; i < N; ++i)
            seed(ob, i, 100 + i, 10, Side::ASK);
        state.ResumeTiming();

        benchmark::DoNotOptimize(
            ob.matchMarketOrder(Side::BID, static_cast<Quantity>(10 * N))
        );
    }
    state.SetItemsProcessed(state.iterations() * N);
}
BENCHMARK(BM_MarketOrder_Sweep)->Arg(1)->Arg(10)->Arg(100)->Arg(1000);
