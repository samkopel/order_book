#include "day_order_pruner.h"
#include <ctime>

namespace {

// Portable localtime -> tm conversion. localtime_s is Microsoft;
// localtime_r is POSIX.
std::tm toLocalTm(std::time_t t)
{
    std::tm out{};
#if defined(_WIN32)
    localtime_s(&out, &t);
#else
    localtime_r(&t, &out);
#endif
    return out;
}

} // namespace

GoodForDayPruner::GoodForDayPruner(std::function<void()> on_fire,
                                   NextFireFn next_fire)
    : on_fire(std::move(on_fire)),
      next_fire(std::move(next_fire)),
      thread([this] { run(); }) {}

GoodForDayPruner::~GoodForDayPruner()
{
    {
        std::lock_guard lock{ mutex };
        shutdown.store(true, std::memory_order_release);
    }
    cv.notify_all();
    if (thread.joinable())
        thread.join();
}

void GoodForDayPruner::run()
{
    while (!shutdown.load(std::memory_order_acquire))
    {
        const auto fire_at = next_fire(Clock::now());

        std::unique_lock lock{ mutex };
        // wait_until with a predicate returns true if the predicate became
        // satisfied (shutdown was signaled), false on timeout.
        const bool shutdown_signaled = cv.wait_until(lock, fire_at, [this] {
            return shutdown.load(std::memory_order_acquire);
        });
        lock.unlock();

        if (shutdown_signaled)
            return;

        on_fire();
    }
}

GoodForDayPruner::Clock::time_point
GoodForDayPruner::nextDailyCutoff(Clock::time_point now,
                                  std::chrono::hours cutoff_hour)
{
    const auto now_c = Clock::to_time_t(now);
    std::tm parts = toLocalTm(now_c);

    const int hour = static_cast<int>(cutoff_hour.count());
    if (parts.tm_hour >= hour)
        parts.tm_mday += 1;

    parts.tm_hour = hour;
    parts.tm_min  = 0;
    parts.tm_sec  = 0;
    parts.tm_isdst = -1; // let mktime resolve DST for the local zone

    return Clock::from_time_t(std::mktime(&parts));
}
