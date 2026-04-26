#include <benchmark/benchmark.h>
#include "order_book.h"
#include <limits>
#include <vector>

// ---- Add ----

// Measures the steady-state cost of adding one order to a book with ~200 price levels.
static void BM_Add(benchmark::State& state) {
    OrderBook ob;
    OrderId id = 0;
    for (auto _ : state) {
        ob.add({id, (id % 200) + 100, 10, Side::BID});
        ++id;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Add);

// Measures add to a fresh, empty book (cold path, each iteration creates a new book).
static void BM_AddBatch(benchmark::State& state) {
    const int N = static_cast<int>(state.range(0));
    std::vector<Order> orders;
    orders.reserve(N);
    for (int i = 0; i < N; ++i)
        orders.push_back({i, (i % 200) + 100, 10, Side::BID});

    for (auto _ : state) {
        state.PauseTiming();
        OrderBook ob;
        state.ResumeTiming();
        for (auto& o : orders) ob.add(o);
    }
    state.SetItemsProcessed(state.iterations() * N);
}
BENCHMARK(BM_AddBatch)->Arg(1000)->Arg(10000)->Arg(100000);

// ---- Cancel ----

// Measures cancel latency against a book with ~200 warm price levels.
static void BM_Cancel(benchmark::State& state) {
    const int kWarm = 10000;
    OrderBook ob;
    for (int i = 0; i < kWarm; ++i)
        ob.add({i, (i % 200) + 100, 10, Side::BID});

    OrderId next = kWarm;
    for (auto _ : state) {
        state.PauseTiming();
        ob.add({next, (next % 200) + 100, 10, Side::BID});
        state.ResumeTiming();
        ob.cancel(next);
        ++next;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Cancel);

// ---- Trade: partial fill (hot path) ----

// The resting order is never exhausted; measures pure matching logic per call.
static void BM_TradePartialFill(benchmark::State& state) {
    OrderBook ob;
    ob.add({1, 100, std::numeric_limits<Quantity>::max() / 2, Side::ASK});
    for (auto _ : state) {
        benchmark::DoNotOptimize(ob.tradeLimitOrder({0, 100, 10, Side::BID}));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_TradePartialFill);

// ---- Trade: sweep N price levels ----

// Each iteration rebuilds the book and sweeps all N levels in one buy order.
// Shows how matching cost scales with the number of touched price levels.
static void BM_TradeSweep(benchmark::State& state) {
    const int N = static_cast<int>(state.range(0));
    for (auto _ : state) {
        state.PauseTiming();
        OrderBook ob;
        for (int i = 0; i < N; ++i)
            ob.add({i, 100 + i, 10, Side::ASK});
        state.ResumeTiming();
        benchmark::DoNotOptimize(
            ob.tradeLimitOrder({N, 100 + N, static_cast<Quantity>(10 * N), Side::BID})
        );
    }
    state.SetItemsProcessed(state.iterations() * N);
}
BENCHMARK(BM_TradeSweep)->Arg(1)->Arg(10)->Arg(100)->Arg(1000);

// ---- Mixed: interleaved add + trade ----

// Simulates a market-maker that continuously posts new asks while market buys arrive.
// Each iteration: post one new ask, then execute a small market buy.
static void BM_Mixed(benchmark::State& state) {
    OrderBook ob;
    // Seed asks at 10 price levels with large quantity so they don't get exhausted.
    for (int i = 0; i < 10; ++i)
        ob.add({i, 100 + i, 1'000'000, Side::ASK});

    OrderId next_ask = 10;
    for (auto _ : state) {
        ob.add({next_ask, 100 + (next_ask % 10), 1'000'000, Side::ASK});
        // market buy sweeps the best ask level partially
        benchmark::DoNotOptimize(ob.tradeLimitOrder({0, 100, 5, Side::BID}));
        // cancel the ask we just added so the book stays bounded
        ob.cancel(next_ask);
        ++next_ask;
    }
    state.SetItemsProcessed(state.iterations() * 2);  // 1 add + 1 trade per iter
}
BENCHMARK(BM_Mixed);
