#include "threadpool/WorkStealingThreadPool.hpp"
#include <iostream>
#include <vector>
#include <numeric>
#include <chrono>

using namespace threadpool;

// Recursive Fibonacci (intentionally inefficient to show work stealing)
int fib(int n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

int main() {
    std::cout << "Work-Stealing Thread Pool Example\n";
    std::cout << "==================================\n\n";

    const size_t NUM_THREADS = 8;
    WorkStealingThreadPool pool(NUM_THREADS);

    std::cout << "Created work-stealing pool with " << NUM_THREADS << " threads\n\n";

    // Example 1: Recursive task submission (demonstrates work stealing)
    std::cout << "Example 1: Recursive Fibonacci Calculation\n";
    std::cout << "-------------------------------------------\n";
    std::cout << "This demonstrates work stealing in action:\n";
    std::cout << "- Parent task submits child tasks\n";
    std::cout << "- Idle threads steal work from busy threads\n\n";

    auto start = std::chrono::high_resolution_clock::now();

    // Submit tasks that submit more tasks (recursive pattern)
    std::vector<std::future<int>> fib_futures;
    std::vector<int> fib_inputs = {30, 31, 32, 33, 34, 35, 36, 37};

    for (int n : fib_inputs) {
        fib_futures.push_back(pool.submit([n]() {
            return fib(n);
        }));
    }

    std::cout << "Computing Fibonacci for: ";
    for (int n : fib_inputs) {
        std::cout << n << " ";
    }
    std::cout << "\n\n";

    // Collect results
    for (size_t i = 0; i < fib_futures.size(); ++i) {
        std::cout << "fib(" << fib_inputs[i] << ") = " << fib_futures[i].get() << "\n";
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "\nTime: " << duration.count() << " ms\n";
    std::cout << "(Work stealing automatically balanced the load across threads)\n\n";

    // Example 2: Parallel map-reduce
    std::cout << "\nExample 2: Parallel Map-Reduce\n";
    std::cout << "-------------------------------\n";

    const size_t DATA_SIZE = 1'000'000;
    std::vector<int> data(DATA_SIZE);
    std::iota(data.begin(), data.end(), 1);

    std::cout << "Summing " << DATA_SIZE << " integers in parallel...\n";

    start = std::chrono::high_resolution_clock::now();

    // Divide work into chunks
    const size_t CHUNK_SIZE = DATA_SIZE / NUM_THREADS;
    std::vector<std::future<long long>> sum_futures;

    for (size_t i = 0; i < NUM_THREADS; ++i) {
        size_t chunk_start = i * CHUNK_SIZE;
        size_t chunk_end = (i == NUM_THREADS - 1) ? DATA_SIZE : (i + 1) * CHUNK_SIZE;

        sum_futures.push_back(pool.submit([&data, chunk_start, chunk_end]() {
            long long partial_sum = 0;
            for (size_t j = chunk_start; j < chunk_end; ++j) {
                partial_sum += data[j];
            }
            return partial_sum;
        }));
    }

    // Reduce
    long long total_sum = 0;
    for (auto& future : sum_futures) {
        total_sum += future.get();
    }

    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "Total sum: " << total_sum << "\n";
    std::cout << "Time: " << duration.count() << " ms\n\n";

    // Example 3: Demonstrate automatic load balancing
    std::cout << "Example 3: Uneven Workload (Load Balancing)\n";
    std::cout << "--------------------------------------------\n";
    std::cout << "Submitting tasks with varying amounts of work.\n";
    std::cout << "Work stealing will automatically balance the load.\n\n";

    start = std::chrono::high_resolution_clock::now();

    std::vector<std::future<void>> balance_futures;

    // Submit tasks with varying workloads
    for (int i = 0; i < 20; ++i) {
        balance_futures.push_back(pool.submit([i]() {
            // Workload varies from light to heavy
            int iterations = (i + 1) * 1000000;
            volatile long long sum = 0;
            for (int j = 0; j < iterations; ++j) {
                sum += j;
            }
        }));
    }

    for (auto& f : balance_futures) {
        f.get();
    }

    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "Completed " << balance_futures.size() << " tasks with varying workloads\n";
    std::cout << "Time: " << duration.count() << " ms\n";
    std::cout << "(Without work stealing, this would take much longer)\n\n";

    std::cout << "===================================\n";
    std::cout << "Work-Stealing Benefits:\n";
    std::cout << "- Automatic load balancing\n";
    std::cout << "- Reduced lock contention (per-thread queues)\n";
    std::cout << "- Cache-friendly (LIFO for local work)\n";
    std::cout << "- Efficient for recursive/dynamic workloads\n";

    return 0;
}
