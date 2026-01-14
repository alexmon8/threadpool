#include "threadpool/ThreadPool.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <iomanip>

using namespace threadpool;

// Simulate different types of work
void simulate_work(const std::string& task_name, int ms) {
    std::cout << "[" << std::setw(10) << task_name << "] Starting...\n" << std::flush;
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    std::cout << "[" << std::setw(10) << task_name << "] Completed!\n" << std::flush;
}

int main() {
    std::cout << "Priority Queue Example\n";
    std::cout << "======================\n\n";

    // Create a small thread pool
    ThreadPool pool(2);

    std::cout << "Submitting tasks with different priorities...\n";
    std::cout << "(Pool has only 2 threads, so you'll see priority in action)\n\n";

    std::vector<std::future<void>> futures;

    // Simulate a batch processing system with different priority jobs

    // Background jobs (LOW priority) - data cleanup, analytics
    for (int i = 1; i <= 3; ++i) {
        futures.push_back(pool.submit(Priority::LOW, [i]() {
            simulate_work("LOW-" + std::to_string(i), 200);
        }));
    }
    std::cout << "✓ Submitted 3 LOW priority tasks (background jobs)\n";

    // Regular jobs (MEDIUM priority) - normal user requests
    for (int i = 1; i <= 3; ++i) {
        futures.push_back(pool.submit(Priority::MEDIUM, [i]() {
            simulate_work("MEDIUM-" + std::to_string(i), 200);
        }));
    }
    std::cout << "✓ Submitted 3 MEDIUM priority tasks (regular requests)\n";

    // Critical jobs (HIGH priority) - urgent user actions, alerts
    for (int i = 1; i <= 3; ++i) {
        futures.push_back(pool.submit(Priority::HIGH, [i]() {
            simulate_work("HIGH-" + std::to_string(i), 200);
        }));
    }
    std::cout << "✓ Submitted 3 HIGH priority tasks (critical operations)\n\n";

    std::cout << "Watch the execution order:\n";
    std::cout << "(HIGH priority tasks should execute first, even though submitted last)\n\n";

    // Wait for all tasks
    for (auto& f : futures) {
        f.get();
    }

    std::cout << "\n======================\n";
    std::cout << "All tasks completed!\n\n";

    // Example 2: Real-time scenario
    std::cout << "Example 2: Simulating real-time job queue\n";
    std::cout << "==========================================\n\n";

    futures.clear();

    // Start with some low priority work
    std::cout << "Starting background processing...\n";
    futures.push_back(pool.submit(Priority::LOW, []() {
        simulate_work("Background", 500);
    }));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Urgent task arrives!
    std::cout << "URGENT: Critical task arrives and jumps the queue!\n";
    futures.push_back(pool.submit(Priority::HIGH, []() {
        simulate_work("URGENT", 200);
    }));

    std::cout << "\n";

    for (auto& f : futures) {
        f.get();
    }

    std::cout << "\n✓ Priority scheduling ensures urgent tasks get processed quickly!\n";

    return 0;
}
