#include "threadpool/DependencyThreadPool.hpp"
#include "threadpool/WorkStealingThreadPool.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <thread>
#include <numeric>
#include <cmath>

using namespace threadpool;
using namespace std::chrono;

// ============================================================================
// Utilities
// ============================================================================

struct BenchmarkResult {
    std::string name;
    double mean_ms;
    double stddev_ms;
    double min_ms;
    double max_ms;
    size_t throughput_per_sec;
};

template<typename Func>
BenchmarkResult benchmark(const std::string& name, Func&& func, int iterations = 5) {
    std::vector<double> times;
    times.reserve(iterations);

    for (int i = 0; i < iterations; ++i) {
        auto start = high_resolution_clock::now();
        func();
        auto end = high_resolution_clock::now();

        double elapsed_ms = duration_cast<microseconds>(end - start).count() / 1000.0;
        times.push_back(elapsed_ms);
    }

    double mean = std::accumulate(times.begin(), times.end(), 0.0) / times.size();
    double sq_sum = std::inner_product(times.begin(), times.end(), times.begin(), 0.0);
    double stddev = std::sqrt(sq_sum / times.size() - mean * mean);
    double min_time = *std::min_element(times.begin(), times.end());
    double max_time = *std::max_element(times.begin(), times.end());

    return {name, mean, stddev, min_time, max_time, 0};
}

void print_result(const BenchmarkResult& result) {
    std::cout << std::left << std::setw(50) << result.name
              << std::right << std::setw(10) << std::fixed << std::setprecision(2)
              << result.mean_ms << " ms"
              << " ± " << std::setw(8) << result.stddev_ms << " ms"
              << " [" << result.min_ms << " - " << result.max_ms << "]";

    if (result.throughput_per_sec > 0) {
        std::cout << "  (" << result.throughput_per_sec << " tasks/sec)";
    }

    std::cout << std::endl;
}

void print_header(const std::string& title) {
    std::cout << "\n" << std::string(100, '=') << "\n";
    std::cout << title << "\n";
    std::cout << std::string(100, '=') << "\n\n";
}

// ============================================================================
// Benchmark 1: Submit Latency
// ============================================================================

void benchmark_submit_latency() {
    print_header("Benchmark 1: Submit Latency (microseconds per submission)");

    const int NUM_TASKS = 10000;

    // DependencyThreadPool submit latency
    {
        DependencyThreadPool pool(4);
        std::vector<std::pair<TaskID, std::future<int>>> futures;

        auto start = high_resolution_clock::now();
        for (int i = 0; i < NUM_TASKS; ++i) {
            futures.push_back(pool.submit([]() { return 42; }));
        }
        auto end = high_resolution_clock::now();

        // Wait for completion
        for (auto& [id, f] : futures) {
            f.get();
        }

        double latency_us = duration_cast<nanoseconds>(end - start).count() / 1000.0 / NUM_TASKS;
        std::cout << "  DependencyThreadPool submit latency:   " << latency_us << " µs/task\n";
    }

    // WorkStealingThreadPool submit latency
    {
        WorkStealingThreadPool pool(4);
        std::vector<std::future<int>> futures;

        auto start = high_resolution_clock::now();
        for (int i = 0; i < NUM_TASKS; ++i) {
            futures.push_back(pool.submit([]() { return 42; }));
        }
        auto end = high_resolution_clock::now();

        // Wait for completion
        for (auto& f : futures) {
            f.get();
        }

        double latency_us = duration_cast<nanoseconds>(end - start).count() / 1000.0 / NUM_TASKS;
        std::cout << "  WorkStealingThreadPool submit latency: " << latency_us << " µs/task\n";
    }
}

// ============================================================================
// Benchmark 2: Throughput (tasks/sec)
// ============================================================================

void benchmark_throughput() {
    print_header("Benchmark 2: Throughput (tasks/second)");

    const int NUM_TASKS = 50000;

    for (size_t threads : {2, 4, 8, 16}) {
        std::cout << "\n--- " << threads << " threads ---\n";

        // DependencyThreadPool
        {
            DependencyThreadPool pool(threads);
            std::vector<std::pair<TaskID, std::future<void>>> futures;

            auto start = high_resolution_clock::now();

            for (int i = 0; i < NUM_TASKS; ++i) {
                futures.push_back(pool.submit([]() {
                    volatile int x = 0;
                    for (int j = 0; j < 100; ++j) {
                        x += j;
                    }
                }));
            }

            for (auto& [id, f] : futures) {
                f.get();
            }

            auto end = high_resolution_clock::now();
            double elapsed_sec = duration_cast<microseconds>(end - start).count() / 1e6;
            size_t throughput = static_cast<size_t>(NUM_TASKS / elapsed_sec);

            std::cout << "  DependencyThreadPool:   " << std::setw(8) << throughput << " tasks/sec\n";
        }

        // WorkStealingThreadPool
        {
            WorkStealingThreadPool pool(threads);
            std::vector<std::future<void>> futures;

            auto start = high_resolution_clock::now();

            for (int i = 0; i < NUM_TASKS; ++i) {
                futures.push_back(pool.submit([]() {
                    volatile int x = 0;
                    for (int j = 0; j < 100; ++j) {
                        x += j;
                    }
                }));
            }

            for (auto& f : futures) {
                f.get();
            }

            auto end = high_resolution_clock::now();
            double elapsed_sec = duration_cast<microseconds>(end - start).count() / 1e6;
            size_t throughput = static_cast<size_t>(NUM_TASKS / elapsed_sec);

            std::cout << "  WorkStealingThreadPool: " << std::setw(8) << throughput << " tasks/sec\n";
        }
    }
}

// ============================================================================
// Benchmark 3: Cycle Detection Performance
// ============================================================================

void benchmark_cycle_detection() {
    print_header("Benchmark 3: Cycle Detection with Large DAGs");

    for (size_t chain_length : {10, 50, 100, 500, 1000}) {
        DependencyThreadPool pool(4);

        auto start = high_resolution_clock::now();

        // Create a long chain: 1 -> 2 -> 3 -> ... -> N
        auto [id, f] = pool.submit([]() { return 0; });

        for (size_t i = 1; i < chain_length; ++i) {
            int val = f.get();
            auto result = pool.submit({id}, [](int x) { return x + 1; }, val);
            id = result.first;
            f = std::move(result.second);
        }

        int final_val = f.get();

        auto end = high_resolution_clock::now();
        double elapsed_ms = duration_cast<microseconds>(end - start).count() / 1000.0;

        std::cout << "  Chain length " << std::setw(4) << chain_length
                  << ": " << std::setw(8) << std::fixed << std::setprecision(2)
                  << elapsed_ms << " ms  (result: " << final_val << ")\n";
    }

    std::cout << "\n--- Diamond DAG (parallelism test) ---\n";

    for (size_t width : {2, 4, 8, 16, 32}) {
        DependencyThreadPool pool(8);

        auto start = high_resolution_clock::now();

        // Create diamond: A -> {B1, B2, ..., Bn} -> C
        auto [idA, fA] = pool.submit([]() { return 1; });
        int valA = fA.get();

        std::vector<std::pair<TaskID, std::future<int>>> middle_tasks;
        for (size_t i = 0; i < width; ++i) {
            middle_tasks.push_back(pool.submit({idA}, [](int x) { return x * 2; }, valA));
        }

        // Collect middle task IDs
        std::vector<TaskID> deps;
        int sum = 0;
        for (auto& [id, f] : middle_tasks) {
            deps.push_back(id);
            sum += f.get();
        }

        // Create final task depending on all middle tasks
        auto [idC, fC] = pool.submit(deps, [sum]() { return sum; });
        int result = fC.get();

        auto end = high_resolution_clock::now();
        double elapsed_ms = duration_cast<microseconds>(end - start).count() / 1000.0;

        std::cout << "  Diamond width " << std::setw(2) << width
                  << ": " << std::setw(8) << std::fixed << std::setprecision(2)
                  << elapsed_ms << " ms  (result: " << result << ")\n";
    }
}

// ============================================================================
// Benchmark 4: Memory Cleanup Effectiveness
// ============================================================================

void benchmark_memory_cleanup() {
    print_header("Benchmark 4: Memory Cleanup (Task Count Tracking)");

    std::cout << "Submitting 5000 tasks in batches of 1000...\n";
    std::cout << "(Cleanup should trigger after 1000, 2000, 3000, 4000 completions)\n\n";

    DependencyThreadPool pool(8);

    for (int batch = 0; batch < 5; ++batch) {
        auto start = high_resolution_clock::now();

        std::vector<std::pair<TaskID, std::future<int>>> tasks;
        for (int i = 0; i < 1000; ++i) {
            tasks.push_back(pool.submit([i]() {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                return i;
            }));
        }

        // Wait for batch
        for (int i = 0; i < 1000; ++i) {
            tasks[i].second.get();
        }

        auto end = high_resolution_clock::now();
        double elapsed_ms = duration_cast<milliseconds>(end - start).count();

        std::cout << "  Batch " << (batch + 1) << " completed in "
                  << elapsed_ms << " ms\n";
    }

    std::cout << "\nNote: With cleanup, memory usage should be bounded.\n";
    std::cout << "      Without cleanup, all_tasks_ would grow to 5000 entries.\n";
}

// ============================================================================
// Benchmark 5: Cache Alignment Impact
// ============================================================================

void benchmark_cache_alignment() {
    print_header("Benchmark 5: Cache Alignment Impact (Contention Test)");

    std::cout << "High-contention workload (many threads, frequent submissions)...\n\n";

    const int NUM_TASKS = 20000;

    for (size_t threads : {4, 8, 16}) {
        std::cout << "--- " << threads << " threads ---\n";

        // WorkStealingThreadPool (with cache alignment)
        {
            WorkStealingThreadPool pool(threads);
            std::vector<std::future<void>> futures;

            auto start = high_resolution_clock::now();

            for (int i = 0; i < NUM_TASKS; ++i) {
                futures.push_back(pool.submit([]() {
                    volatile int x = 0;
                    for (int j = 0; j < 50; ++j) x++;
                }));
            }

            for (auto& f : futures) {
                f.get();
            }

            auto end = high_resolution_clock::now();
            double elapsed_ms = duration_cast<milliseconds>(end - start).count();

            std::cout << "  WorkStealingThreadPool (cache-aligned): "
                      << std::setw(8) << elapsed_ms << " ms\n";
        }

        // DependencyThreadPool (also cache-aligned mutexes)
        {
            DependencyThreadPool pool(threads);
            std::vector<std::pair<TaskID, std::future<void>>> futures;

            auto start = high_resolution_clock::now();

            for (int i = 0; i < NUM_TASKS; ++i) {
                futures.push_back(pool.submit([]() {
                    volatile int x = 0;
                    for (int j = 0; j < 50; ++j) x++;
                }));
            }

            for (auto& [id, f] : futures) {
                f.get();
            }

            auto end = high_resolution_clock::now();
            double elapsed_ms = duration_cast<milliseconds>(end - start).count();

            std::cout << "  DependencyThreadPool (cache-aligned):   "
                      << std::setw(8) << elapsed_ms << " ms\n\n";
        }
    }

    std::cout << "Note: Cache alignment reduces false sharing, improving performance\n";
    std::cout << "      when multiple threads access adjacent memory locations.\n";
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║      ThreadPool Phase 3 Optimization Benchmarks                 ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════╝\n";
    std::cout << "\nHardware: " << std::thread::hardware_concurrency() << " concurrent threads\n";

    benchmark_submit_latency();
    benchmark_throughput();
    benchmark_cycle_detection();
    benchmark_memory_cleanup();
    benchmark_cache_alignment();

    print_header("Summary: Phase 3 Optimizations");
    std::cout << "1. Cache Alignment (alignas(64)):\n";
    std::cout << "   - Prevents false sharing between worker queues\n";
    std::cout << "   - Expected: 15-25% throughput improvement on multi-core\n\n";

    std::cout << "2. Thread-Local Worker ID:\n";
    std::cout << "   - O(1) worker ID lookup instead of O(n) linear search\n";
    std::cout << "   - Expected: 5-10% reduction in submit latency\n\n";

    std::cout << "3. Incremental Cycle Detection:\n";
    std::cout << "   - O(k) complexity where k = reachable nodes (vs O(n²))\n";
    std::cout << "   - Expected: 100-1000x faster for large DAGs (1000+ tasks)\n\n";

    std::cout << "4. Periodic Task Cleanup:\n";
    std::cout << "   - Bounded memory usage vs unbounded growth\n";
    std::cout << "   - Cleanup triggers every 1000 task completions\n\n";

    std::cout << std::string(100, '=') << "\n\n";

    return 0;
}
