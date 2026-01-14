#include "threadpool/ThreadPool.hpp"
#include "threadpool/WorkStealingThreadPool.hpp"
#include <iostream>
#include <chrono>
#include <vector>
#include <iomanip>
#include <numeric>

using namespace threadpool;

// Benchmark helper
template<typename Pool>
long long benchmark_throughput(Pool& pool, size_t num_tasks) {
    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::future<int>> futures;
    for (size_t i = 0; i < num_tasks; ++i) {
        futures.push_back(pool.submit([]() {
            int sum = 0;
            for (int j = 0; j < 100; ++j) {
                sum += j;
            }
            return sum;
        }));
    }

    for (auto& f : futures) {
        f.get();
    }

    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

// Benchmark recursive workload (favors work-stealing)
template<typename Pool>
long long benchmark_recursive(Pool& pool, int depth) {
    auto start = std::chrono::high_resolution_clock::now();

    std::function<int(int)> recursive_task = [&](int n) -> int {
        if (n <= 1) return 1;

        auto f1 = pool.submit([&, n]() { return recursive_task(n - 1); });
        auto f2 = pool.submit([&, n]() { return recursive_task(n - 2); });

        return f1.get() + f2.get();
    };

    auto result = pool.submit([&]() { return recursive_task(depth); });
    result.get();

    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

// Benchmark uneven workload
template<typename Pool>
long long benchmark_uneven(Pool& pool, size_t num_tasks) {
    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::future<void>> futures;
    for (size_t i = 0; i < num_tasks; ++i) {
        futures.push_back(pool.submit([i]() {
            volatile long long sum = 0;
            // Workload increases with task number
            int iterations = (i + 1) * 100000;
            for (int j = 0; j < iterations; ++j) {
                sum += j;
            }
        }));
    }

    for (auto& f : futures) {
        f.get();
    }

    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

// Benchmark parallel reduce
template<typename Pool>
long long benchmark_reduce(Pool& pool, size_t data_size) {
    std::vector<int> data(data_size);
    std::iota(data.begin(), data.end(), 1);

    auto start = std::chrono::high_resolution_clock::now();

    const size_t num_threads = pool.size();
    const size_t chunk_size = data_size / num_threads;
    std::vector<std::future<long long>> futures;

    for (size_t i = 0; i < num_threads; ++i) {
        size_t start_idx = i * chunk_size;
        size_t end_idx = (i == num_threads - 1) ? data_size : (i + 1) * chunk_size;

        futures.push_back(pool.submit([&data, start_idx, end_idx]() {
            long long sum = 0;
            for (size_t j = start_idx; j < end_idx; ++j) {
                sum += data[j];
            }
            return sum;
        }));
    }

    long long total = 0;
    for (auto& f : futures) {
        total += f.get();
    }

    auto end = std::chrono::high_resolution_clock::now();

    // Use total to prevent optimization
    if (total == 0) {
        std::cerr << "Unexpected zero sum\n";
    }

    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

void print_header(const std::string& title) {
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << title << "\n";
    std::cout << std::string(70, '=') << "\n";
}

void print_result(const std::string& test, long long priority_time, long long ws_time) {
    std::cout << std::setw(30) << std::left << test;
    std::cout << std::setw(15) << std::right << priority_time << " ms";
    std::cout << std::setw(15) << ws_time << " ms";

    if (priority_time < ws_time) {
        double speedup = static_cast<double>(ws_time) / priority_time;
        std::cout << std::setw(15) << std::fixed << std::setprecision(2) << speedup << "x faster";
    } else {
        double speedup = static_cast<double>(priority_time) / ws_time;
        std::cout << std::setw(15) << std::fixed << std::setprecision(2) << speedup << "x faster";
    }
    std::cout << "\n";
}

int main() {
    const size_t NUM_THREADS = 8;

    std::cout << "Thread Pool Performance Comparison\n";
    std::cout << "===================================\n";
    std::cout << "Comparing Priority Queue vs Work-Stealing implementations\n";
    std::cout << "Number of threads: " << NUM_THREADS << "\n";

    ThreadPool priority_pool(NUM_THREADS);
    WorkStealingThreadPool ws_pool(NUM_THREADS);

    print_header("Benchmark 1: Task Throughput (10,000 light tasks)");
    std::cout << std::setw(30) << std::left << "Test";
    std::cout << std::setw(15) << std::right << "Priority Queue";
    std::cout << std::setw(15) << "Work-Stealing";
    std::cout << std::setw(15) << "Winner";
    std::cout << "\n" << std::string(70, '-') << "\n";

    auto pq_time = benchmark_throughput(priority_pool, 10000);
    auto ws_time = benchmark_throughput(ws_pool, 10000);
    print_result("Throughput", pq_time, ws_time);

    print_header("Benchmark 2: Parallel Reduce (1,000,000 integers)");
    std::cout << std::setw(30) << std::left << "Test";
    std::cout << std::setw(15) << std::right << "Priority Queue";
    std::cout << std::setw(15) << "Work-Stealing";
    std::cout << std::setw(15) << "Winner";
    std::cout << "\n" << std::string(70, '-') << "\n";

    pq_time = benchmark_reduce(priority_pool, 1000000);
    ws_time = benchmark_reduce(ws_pool, 1000000);
    print_result("Parallel Sum", pq_time, ws_time);

    print_header("Benchmark 3: Uneven Workload (50 tasks, increasing size)");
    std::cout << std::setw(30) << std::left << "Test";
    std::cout << std::setw(15) << std::right << "Priority Queue";
    std::cout << std::setw(15) << "Work-Stealing";
    std::cout << std::setw(15) << "Winner";
    std::cout << "\n" << std::string(70, '-') << "\n";

    pq_time = benchmark_uneven(priority_pool, 50);
    ws_time = benchmark_uneven(ws_pool, 50);
    print_result("Uneven Load", pq_time, ws_time);

    // Recursive benchmark commented out - takes too long for demo
    // print_header("Benchmark 4: Recursive Tasks (depth=12)");
    // pq_time = benchmark_recursive(priority_pool, 12);
    // ws_time = benchmark_recursive(ws_pool, 12);
    // print_result("Recursive Fib", pq_time, ws_time);

    print_header("Summary");
    std::cout << "Priority Queue ThreadPool:\n";
    std::cout << "  + Best for: Tasks with priorities, predictable workloads\n";
    std::cout << "  + Features: High/Medium/Low priority scheduling\n";
    std::cout << "  - Single shared queue (can have contention)\n\n";

    std::cout << "Work-Stealing ThreadPool:\n";
    std::cout << "  + Best for: Recursive tasks, uneven workloads, high concurrency\n";
    std::cout << "  + Features: Per-thread queues, automatic load balancing\n";
    std::cout << "  + Lower lock contention, cache-friendly\n";

    std::cout << "\n" << std::string(70, '=') << "\n";

    return 0;
}
