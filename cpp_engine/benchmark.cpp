#include "../cpp_engine/order_book.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;

// Prevents the compiler from optimising away read-only calls (thread_local so concurrent benchmarks don't race on it)
static thread_local volatile double sink = 0;

static constexpr double MID_PRICE = 100.0;
static constexpr double TICK = 0.01;

// Limit price above any resting ask, so aggressive buys always cross the whole book
static constexpr double AGGRESSIVE_BUY_PRICE = 1e7;

enum class OpType { Add, Cancel, Cross, Read };

struct Op {
    OpType type;
    Order order;
    int cancelIndex;
};

struct Stats {
    double totalSeconds;
    size_t ops;
    std::vector<long long> latenciesNs;
};

static double tickPrice(int ticks) {
    return MID_PRICE + ticks * TICK;
}

// Gives every bot enough cash and stock that the risk checks never block a trade.
// Balance is capped because the engine casts balance / price to int, which overflows above ~2.1e9 shares.
static void fundBots(OrderBook& ob, int firstBot, int lastBot) {
    for (int bot = firstBot; bot <= lastBot; ++bot) {
        ob.getAccount(bot).balance = 1e11;
        ob.getAccount(bot).stockQuantity = 1000000000;
    }
}

static long long percentile(std::vector<long long>& sorted, double p) {
    if (sorted.empty()) return 0;
    size_t idx = std::min(sorted.size() - 1, (size_t)(p / 100.0 * sorted.size()));
    return sorted[idx];
}

static void printHeader() {
    std::cout << std::left << std::setw(34) << "Benchmark"
              << std::right << std::setw(12) << "ops"
              << std::setw(14) << "ops/sec"
              << std::setw(10) << "mean"
              << std::setw(10) << "p50"
              << std::setw(10) << "p99"
              << std::setw(10) << "p99.9"
              << std::setw(10) << "max" << "\n";
    std::cout << std::string(110, '-') << "\n";
}

static void printStats(const std::string& name, Stats& s) {
    std::sort(s.latenciesNs.begin(), s.latenciesNs.end());
    double mean = 0;
    for (long long l : s.latenciesNs) mean += l;
    if (!s.latenciesNs.empty()) mean /= s.latenciesNs.size();

    auto ns = [](double v) { return std::to_string((long long)v) + "ns"; };

    std::cout << std::left << std::setw(34) << name
              << std::right << std::setw(12) << s.ops
              << std::setw(14) << (long long)(s.ops / s.totalSeconds)
              << std::setw(10) << (s.latenciesNs.empty() ? "-" : ns(mean))
              << std::setw(10) << (s.latenciesNs.empty() ? "-" : ns(percentile(s.latenciesNs, 50)))
              << std::setw(10) << (s.latenciesNs.empty() ? "-" : ns(percentile(s.latenciesNs, 99)))
              << std::setw(10) << (s.latenciesNs.empty() ? "-" : ns(percentile(s.latenciesNs, 99.9)))
              << std::setw(10) << (s.latenciesNs.empty() ? "-" : ns(s.latenciesNs.back())) << "\n";
}

// Runs fn(i) for every i, measuring both batch throughput and per-op latency on separate fresh books.
// Per-op timing adds clock overhead (~20-40ns), so throughput comes from the untimed pass.
template <typename Setup, typename Fn>
static Stats runBenchmark(size_t n, Setup setup, Fn fn) {
    Stats stats{0, n, {}};
    {
        OrderBook ob(MID_PRICE);
        setup(ob);
        auto start = Clock::now();
        for (size_t i = 0; i < n; ++i) fn(ob, i);
        stats.totalSeconds = std::chrono::duration<double>(Clock::now() - start).count();
    }
    {
        OrderBook ob(MID_PRICE);
        setup(ob);
        stats.latenciesNs.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            auto t0 = Clock::now();
            fn(ob, i);
            auto t1 = Clock::now();
            stats.latenciesNs.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        }
    }
    return stats;
}

// Orders that rest on the book without matching, spread over 100 price levels per side
static void benchPassiveAdd(size_t n) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> level(1, 100);
    std::vector<Order> orders(n);
    for (size_t i = 0; i < n; ++i) {
        bool isBuy = i % 2 == 0;
        int ticks = isBuy ? -level(rng) : level(rng);
        orders[i] = {(int)i + 1, 1 + (int)(i % 10), isBuy, tickPrice(ticks), 10};
    }

    auto stats = runBenchmark(n,
        [](OrderBook& ob) { fundBots(ob, 1, 10); },
        [&](OrderBook& ob, size_t i) { ob.addOrder(orders[i]); });
    printStats("addOrder (passive, no match)", stats);
}

// Cancels orders in random order from a pre-populated book
static void benchCancel(size_t n) {
    std::mt19937 rng(43);
    std::uniform_int_distribution<int> level(1, 100);
    std::vector<Order> orders(n);
    for (size_t i = 0; i < n; ++i) {
        bool isBuy = i % 2 == 0;
        int ticks = isBuy ? -level(rng) : level(rng);
        orders[i] = {(int)i + 1, 1 + (int)(i % 10), isBuy, tickPrice(ticks), 10};
    }
    std::vector<int> cancelIds(n);
    for (size_t i = 0; i < n; ++i) cancelIds[i] = (int)i + 1;
    std::shuffle(cancelIds.begin(), cancelIds.end(), rng);

    auto stats = runBenchmark(n,
        [&](OrderBook& ob) {
            fundBots(ob, 1, 10);
            for (const auto& o : orders) ob.addOrder(o);
        },
        [&](OrderBook& ob, size_t i) { ob.cancelOrder(cancelIds[i]); });
    printStats("cancelOrder (random)", stats);
}

// Each aggressive buy fully fills exactly one resting sell order
static void benchAggressiveMatch(size_t n) {
    std::vector<Order> buys(n);
    for (size_t i = 0; i < n; ++i) {
        buys[i] = {(int)(n + i + 1), 2, true, AGGRESSIVE_BUY_PRICE, 10};
    }

    auto stats = runBenchmark(n,
        [n](OrderBook& ob) {
            fundBots(ob, 1, 2);
            for (size_t i = 0; i < n; ++i) {
                // 10 orders per price level, so the walk clears levels as well as individual orders
                int ticks = 1 + (int)(i / 10);
                ob.addOrder({(int)i + 1, 1, false, tickPrice(ticks), 10});
            }
        },
        [&](OrderBook& ob, size_t i) { ob.addOrder(buys[i]); });
    printStats("addOrder (aggressive, 1 fill)", stats);
}

// A single aggressive order sweeping through many resting orders
static void benchSweep(size_t n) {
    const int ordersPerSweep = 50;
    size_t sweeps = std::max<size_t>(1, n / ordersPerSweep);

    auto stats = runBenchmark(sweeps,
        [&](OrderBook& ob) {
            fundBots(ob, 1, 2);
            for (size_t i = 0; i < sweeps * ordersPerSweep; ++i) {
                ob.addOrder({(int)i + 1, 1, false, tickPrice(1 + (int)(i % 200)), 10});
            }
        },
        [&](OrderBook& ob, size_t i) {
            ob.addOrder({(int)(sweeps * ordersPerSweep + i + 1), 2, true, AGGRESSIVE_BUY_PRICE, 10 * ordersPerSweep});
        });
    printStats("addOrder (sweep 50 orders)", stats);
}

static void benchReads(size_t n) {
    auto stats = runBenchmark(n,
        [](OrderBook& ob) {
            fundBots(ob, 1, 2);
            for (int i = 0; i < 1000; ++i) {
                ob.addOrder({i * 2 + 1, 1, false, tickPrice(1 + i % 50), 10});
                ob.addOrder({i * 2 + 2, 2, true, tickPrice(-1 - i % 50), 10});
            }
        },
        [](OrderBook& ob, size_t) { sink = sink + ob.getMarketState(1).bestBid; });
    printStats("getMarketState", stats);
}

// Market-maker style flow: mostly passive quotes, plenty of cancels, some crossing orders and reads
static std::vector<Op> generateMixedOps(size_t n, int botId, unsigned seed, int idBase) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> pick(0, 99);
    std::uniform_int_distribution<int> level(1, 20);
    std::uniform_int_distribution<int> qty(1, 20);
    std::uniform_int_distribution<int> anyIndex(0, 1 << 30);

    std::vector<Op> ops(n);
    for (size_t i = 0; i < n; ++i) {
        int p = pick(rng);
        bool isBuy = rng() & 1;
        int id = idBase + (int)i;
        if (p < 55) {
            int ticks = isBuy ? -level(rng) : level(rng);
            ops[i] = {OpType::Add, {id, botId, isBuy, tickPrice(ticks), qty(rng)}, 0};
        } else if (p < 85) {
            ops[i] = {OpType::Cancel, {}, anyIndex(rng)};
        } else if (p < 95) {
            int ticks = isBuy ? level(rng) : -level(rng);
            ops[i] = {OpType::Cross, {id, botId, isBuy, tickPrice(ticks), qty(rng)}, 0};
        } else {
            ops[i] = {OpType::Read, {}, 0};
        }
    }
    return ops;
}

static void runMixedOp(OrderBook& ob, const Op& op, std::vector<int>& liveIds) {
    switch (op.type) {
        case OpType::Add:
        case OpType::Cross:
            ob.addOrder(op.order);
            liveIds.push_back(op.order.orderId);
            break;
        case OpType::Cancel:
            if (!liveIds.empty()) {
                size_t idx = op.cancelIndex % liveIds.size();
                ob.cancelOrder(liveIds[idx]);
                liveIds[idx] = liveIds.back();
                liveIds.pop_back();
            }
            break;
        case OpType::Read:
            sink = sink + ob.getMarketState(op.order.botId).bestAsk;
            break;
    }
}

static void benchMixed(size_t n) {
    auto ops = generateMixedOps(n, 1, 44, 1);
    std::vector<int> liveIds;
    liveIds.reserve(n);

    auto stats = runBenchmark(n,
        [&](OrderBook& ob) {
            fundBots(ob, 1, 1);
            liveIds.clear();
        },
        [&](OrderBook& ob, size_t i) { runMixedOp(ob, ops[i], liveIds); });
    printStats("mixed workload (1 thread)", stats);
}

// Several bots hammering one shared book concurrently; reports aggregate throughput
static void benchConcurrent(size_t n, int threads) {
    size_t perThread = n / threads;
    std::vector<std::vector<Op>> ops(threads);
    for (int t = 0; t < threads; ++t) {
        ops[t] = generateMixedOps(perThread, t + 1, 100 + t, 1 + t * (int)perThread);
    }

    OrderBook ob(MID_PRICE);
    fundBots(ob, 1, threads);

    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    std::vector<std::thread> workers;
    for (int t = 0; t < threads; ++t) {
        workers.emplace_back([&, t]() {
            std::vector<int> liveIds;
            liveIds.reserve(perThread);
            ready.fetch_add(1);
            while (!go.load()) {}
            for (const auto& op : ops[t]) runMixedOp(ob, op, liveIds);
        });
    }

    while (ready.load() < threads) {}
    auto start = Clock::now();
    go.store(true);
    for (auto& w : workers) w.join();
    double seconds = std::chrono::duration<double>(Clock::now() - start).count();

    Stats stats{seconds, perThread * threads, {}};
    printStats("mixed workload (" + std::to_string(threads) + " threads)", stats);
}

int main(int argc, char* argv[]) {
    size_t n = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1000000;
    if (n == 0) {
        std::cerr << "Usage: " << argv[0] << " [ops_per_benchmark]\n";
        return 1;
    }

#if !defined(__OPTIMIZE__) && !defined(_MSC_VER)
    std::cout << "WARNING: built without optimisations, results will not be representative\n\n";
#endif

    std::cout << "Order book benchmark (" << n << " ops per benchmark)\n\n";
    printHeader();

    benchPassiveAdd(n);
    benchCancel(n);
    benchAggressiveMatch(n);
    benchSweep(n);
    benchReads(n);
    benchMixed(n);

    unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    for (int threads : {2, 4, 8}) {
        if ((unsigned)threads > hw) break;
        benchConcurrent(n, threads);
    }

    std::cout << "\nLatencies include ~20-40ns of clock overhead per op. ops/sec is measured on a separate untimed run.\n";
    return 0;
}
