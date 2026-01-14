#include "threadpool/ThreadPool.hpp"
#include <iostream>
#include <chrono>
#include <vector>
#include <iomanip>

void benchmark_task_throughput(size_t num_threads, size_t num_tasks) {
    threadpool::ThreadPool pool(num_threads);

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::future<int>> futures;
    for (size_t i = 0; i < num_tasks; ++i) {
        futures.push_back(pool.submit([]() {
            // Light work
            int sum = 0;
            for (int j = 0; j < 100; ++j) {
                sum += j;
            }
            return sum;
        }));
    }

    // Wait for all tasks
    for (auto& f : futures) {
        f.get();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    double tasks_per_second = (num_tasks * 1000.0) / duration.count();

    std::cout << std::setw(12) << num_threads
              << std::setw(15) << num_tasks
              << std::setw(15) << duration.count()
              << std::setw(20) << std::fixed << std::setprecision(2) << tasks_per_second
              << "\n";
}

int main() {
    std::cout << "ThreadPool Benchmark\n";
    std::cout << "====================\n\n";

    std::cout << "Task Throughput Test\n";
    std::cout << std::setw(12) << "Threads"
              << std::setw(15) << "Tasks"
              << std::setw(15) << "Time (ms)"
              << std::setw(20) << "Tasks/second"
              << "\n";
    std::cout << std::string(61, '-') << "\n";

    // Benchmark different thread counts
    const size_t NUM_TASKS = 10000;

    for (size_t threads : {1, 2, 4, 8, 16}) {
        benchmark_task_throughput(threads, NUM_TASKS);
    }

    std::cout << "\n";
    return 0;
}
