#include "threadpool/ThreadPool.hpp"
#include <iostream>
#include <vector>
#include <numeric>
#include <chrono>

// Compute sum of array segment
long long sum_segment(const std::vector<int>& data, size_t start, size_t end) {
    long long sum = 0;
    for (size_t i = start; i < end; ++i) {
        sum += data[i];
    }
    return sum;
}

int main() {
    const size_t DATA_SIZE = 10'000'000;
    const size_t NUM_THREADS = 8;

    std::cout << "Parallel Sum Example\n";
    std::cout << "====================\n\n";

    // Create test data
    std::cout << "Creating " << DATA_SIZE << " integers...\n";
    std::vector<int> data(DATA_SIZE);
    std::iota(data.begin(), data.end(), 1);  // Fill with 1, 2, 3, ...

    // Sequential sum (baseline)
    std::cout << "\nSequential computation...\n";
    auto start = std::chrono::high_resolution_clock::now();

    long long sequential_sum = 0;
    for (int val : data) {
        sequential_sum += val;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto sequential_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "Sequential sum: " << sequential_sum << "\n";
    std::cout << "Time: " << sequential_time.count() << " ms\n";

    // Parallel sum using thread pool
    std::cout << "\nParallel computation with " << NUM_THREADS << " threads...\n";
    threadpool::ThreadPool pool(NUM_THREADS);

    start = std::chrono::high_resolution_clock::now();

    // Divide work into chunks
    size_t chunk_size = DATA_SIZE / NUM_THREADS;
    std::vector<std::future<long long>> futures;

    for (size_t i = 0; i < NUM_THREADS; ++i) {
        size_t start_idx = i * chunk_size;
        size_t end_idx = (i == NUM_THREADS - 1) ? DATA_SIZE : (i + 1) * chunk_size;

        futures.push_back(pool.submit(sum_segment, std::ref(data), start_idx, end_idx));
    }

    // Collect results
    long long parallel_sum = 0;
    for (auto& future : futures) {
        parallel_sum += future.get();
    }

    end = std::chrono::high_resolution_clock::now();
    auto parallel_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "Parallel sum: " << parallel_sum << "\n";
    std::cout << "Time: " << parallel_time.count() << " ms\n";

    // Results
    std::cout << "\n--- Results ---\n";
    std::cout << "Correctness: " << (sequential_sum == parallel_sum ? "PASS" : "FAIL") << "\n";
    std::cout << "Speedup: " << (static_cast<double>(sequential_time.count()) / parallel_time.count())
              << "x\n";

    return 0;
}
