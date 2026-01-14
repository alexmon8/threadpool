#include "threadpool/ThreadPool.hpp"
#include <iostream>
#include <vector>

int main() {
    // Create a thread pool with 4 worker threads
    threadpool::ThreadPool pool(4);

    std::cout << "ThreadPool created with " << pool.size() << " threads\n\n";

    // Example 1: Simple task with return value
    std::cout << "Example 1: Simple calculation\n";
    auto future1 = pool.submit([]() {
        return 2 + 2;
    });
    std::cout << "Result: " << future1.get() << "\n\n";

    // Example 2: Task with parameters
    std::cout << "Example 2: Task with parameters\n";
    auto future2 = pool.submit([](int a, int b) {
        return a * b;
    }, 6, 7);
    std::cout << "6 * 7 = " << future2.get() << "\n\n";

    // Example 3: Multiple concurrent tasks
    std::cout << "Example 3: Multiple concurrent tasks\n";
    std::vector<std::future<int>> futures;

    for (int i = 0; i < 10; ++i) {
        futures.push_back(pool.submit([i]() {
            // Simulate some work
            int sum = 0;
            for (int j = 0; j <= i; ++j) {
                sum += j;
            }
            return sum;
        }));
    }

    for (size_t i = 0; i < futures.size(); ++i) {
        std::cout << "Sum(0.." << i << ") = " << futures[i].get() << "\n";
    }

    std::cout << "\nAll tasks completed!\n";
    return 0;
}
