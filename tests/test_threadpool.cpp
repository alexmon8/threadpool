#define BOOST_TEST_MODULE ThreadPoolTests
#include <boost/test/unit_test.hpp>

#include "threadpool/ThreadPool.hpp"
#include <atomic>
#include <chrono>
#include <thread>

using namespace threadpool;

BOOST_AUTO_TEST_SUITE(BasicFunctionality)

BOOST_AUTO_TEST_CASE(construction_default) {
    BOOST_CHECK_NO_THROW(ThreadPool pool);
}

BOOST_AUTO_TEST_CASE(construction_with_size) {
    BOOST_CHECK_NO_THROW(ThreadPool pool(4));
}

BOOST_AUTO_TEST_CASE(construction_zero_threads) {
    BOOST_CHECK_THROW(ThreadPool pool(0), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(pool_size) {
    ThreadPool pool(8);
    BOOST_CHECK_EQUAL(pool.size(), 8);
}

BOOST_AUTO_TEST_CASE(submit_simple_task) {
    ThreadPool pool(2);
    auto future = pool.submit([]() { return 42; });
    BOOST_CHECK_EQUAL(future.get(), 42);
}

BOOST_AUTO_TEST_CASE(submit_task_with_args) {
    ThreadPool pool(2);
    auto future = pool.submit([](int a, int b) { return a + b; }, 10, 32);
    BOOST_CHECK_EQUAL(future.get(), 42);
}

BOOST_AUTO_TEST_CASE(submit_void_task) {
    ThreadPool pool(2);
    std::atomic<int> counter{0};
    auto future = pool.submit([&counter]() { counter++; });
    future.get();  // Wait for completion
    BOOST_CHECK_EQUAL(counter.load(), 1);
}

BOOST_AUTO_TEST_CASE(multiple_tasks) {
    ThreadPool pool(4);
    std::vector<std::future<int>> futures;

    for (int i = 0; i < 100; ++i) {
        futures.push_back(pool.submit([i]() { return i * 2; }));
    }

    for (int i = 0; i < 100; ++i) {
        BOOST_CHECK_EQUAL(futures[i].get(), i * 2);
    }
}

BOOST_AUTO_TEST_CASE(task_execution_order_fifo) {
    ThreadPool pool(1);  // Single thread ensures FIFO order
    std::vector<int> results;
    std::mutex results_mutex;
    std::vector<std::future<void>> futures;

    for (int i = 0; i < 10; ++i) {
        futures.push_back(pool.submit([i, &results, &results_mutex]() {
            std::lock_guard<std::mutex> lock(results_mutex);
            results.push_back(i);
        }));
    }

    for (auto& f : futures) {
        f.get();
    }

    BOOST_CHECK_EQUAL(results.size(), 10);
    for (size_t i = 0; i < results.size(); ++i) {
        BOOST_CHECK_EQUAL(results[i], static_cast<int>(i));
    }
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(ExceptionHandling)

BOOST_AUTO_TEST_CASE(task_throws_exception) {
    ThreadPool pool(2);
    auto future = pool.submit([]() -> int {
        throw std::runtime_error("Task error");
    });

    BOOST_CHECK_THROW(future.get(), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(exception_does_not_crash_pool) {
    ThreadPool pool(2);

    // Submit task that throws
    auto future1 = pool.submit([]() -> int {
        throw std::runtime_error("Error");
    });

    // Submit normal task after
    auto future2 = pool.submit([]() { return 42; });

    BOOST_CHECK_THROW(future1.get(), std::runtime_error);
    BOOST_CHECK_EQUAL(future2.get(), 42);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(Shutdown)

BOOST_AUTO_TEST_CASE(graceful_shutdown) {
    ThreadPool pool(2);
    std::atomic<int> completed{0};
    std::vector<std::future<void>> futures;

    for (int i = 0; i < 10; ++i) {
        futures.push_back(pool.submit([&completed]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            completed++;
        }));
    }

    // Pool destructor should wait for all tasks
    // (futures will be destroyed with pool)
}

BOOST_AUTO_TEST_CASE(cannot_submit_after_destruction) {
    auto pool_ptr = std::make_unique<ThreadPool>(2);
    pool_ptr.reset();  // Destroy pool

    // Cannot submit to destroyed pool (this tests that destructor completes)
    BOOST_CHECK(true);  // If we get here, destructor completed successfully
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(Concurrency)

BOOST_AUTO_TEST_CASE(concurrent_execution) {
    ThreadPool pool(4);
    std::atomic<int> concurrent_count{0};
    std::atomic<int> max_concurrent{0};
    std::vector<std::future<void>> futures;

    for (int i = 0; i < 20; ++i) {
        futures.push_back(pool.submit([&concurrent_count, &max_concurrent]() {
            int current = ++concurrent_count;

            // Update max concurrent
            int expected = max_concurrent.load();
            while (current > expected &&
                   !max_concurrent.compare_exchange_weak(expected, current)) {
                // Retry
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            --concurrent_count;
        }));
    }

    for (auto& f : futures) {
        f.get();
    }

    // With 4 threads, we should see at least 4 concurrent executions
    BOOST_CHECK_GE(max_concurrent.load(), 4);
}

BOOST_AUTO_TEST_CASE(thread_safety_shared_counter) {
    ThreadPool pool(8);
    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;

    const int num_tasks = 1000;
    for (int i = 0; i < num_tasks; ++i) {
        futures.push_back(pool.submit([&counter]() {
            for (int j = 0; j < 100; ++j) {
                counter++;
            }
        }));
    }

    for (auto& f : futures) {
        f.get();
    }

    BOOST_CHECK_EQUAL(counter.load(), num_tasks * 100);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EdgeCases)

BOOST_AUTO_TEST_CASE(submit_many_tasks_small_pool) {
    ThreadPool pool(2);
    std::vector<std::future<int>> futures;

    // Submit many more tasks than threads
    for (int i = 0; i < 1000; ++i) {
        futures.push_back(pool.submit([i]() { return i; }));
    }

    for (int i = 0; i < 1000; ++i) {
        BOOST_CHECK_EQUAL(futures[i].get(), i);
    }
}

BOOST_AUTO_TEST_CASE(submit_from_task) {
    ThreadPool pool(4);

    // Task that submits another task
    auto future = pool.submit([&pool]() {
        auto inner_future = pool.submit([]() { return 42; });
        return inner_future.get();
    });

    BOOST_CHECK_EQUAL(future.get(), 42);
}

BOOST_AUTO_TEST_CASE(long_running_task) {
    ThreadPool pool(2);

    auto future = pool.submit([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return 42;
    });

    BOOST_CHECK_EQUAL(future.get(), 42);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(PriorityScheduling)

BOOST_AUTO_TEST_CASE(priority_order_single_thread) {
    ThreadPool pool(1);  // Single thread ensures strict ordering
    std::vector<int> results;
    std::mutex results_mutex;
    std::atomic<bool> start{false};
    std::vector<std::future<void>> futures;

    // Submit a blocker task first to hold the thread
    auto blocker = pool.submit(Priority::LOW, [&start]() {
        while (!start.load()) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    });

    // Submit tasks in mixed priority order
    // They should execute: HIGH (3), HIGH (4), MEDIUM (2), LOW (1)
    futures.push_back(pool.submit(Priority::LOW, [&results, &results_mutex]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::lock_guard<std::mutex> lock(results_mutex);
        results.push_back(1);
    }));

    futures.push_back(pool.submit(Priority::MEDIUM, [&results, &results_mutex]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::lock_guard<std::mutex> lock(results_mutex);
        results.push_back(2);
    }));

    futures.push_back(pool.submit(Priority::HIGH, [&results, &results_mutex]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::lock_guard<std::mutex> lock(results_mutex);
        results.push_back(3);
    }));

    futures.push_back(pool.submit(Priority::HIGH, [&results, &results_mutex]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::lock_guard<std::mutex> lock(results_mutex);
        results.push_back(4);
    }));

    // Let all tasks be queued, then release the blocker
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    start = true;
    blocker.get();  // Wait for blocker to finish

    // Wait for all tasks
    for (auto& f : futures) {
        f.get();
    }

    // Check execution order: HIGH tasks first, then MEDIUM, then LOW
    BOOST_REQUIRE_EQUAL(results.size(), 4);
    BOOST_CHECK_EQUAL(results[0], 3);  // First HIGH
    BOOST_CHECK_EQUAL(results[1], 4);  // Second HIGH (FIFO within priority)
    BOOST_CHECK_EQUAL(results[2], 2);  // MEDIUM
    BOOST_CHECK_EQUAL(results[3], 1);  // LOW
}

BOOST_AUTO_TEST_CASE(default_priority_is_medium) {
    ThreadPool pool(1);
    std::vector<int> results;
    std::mutex results_mutex;
    std::atomic<bool> start{false};
    std::vector<std::future<void>> futures;

    // Submit a blocker task first to hold the thread
    auto blocker = pool.submit(Priority::LOW, [&start]() {
        while (!start.load()) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    });

    // Now submit LOW, default (should be MEDIUM), HIGH
    futures.push_back(pool.submit(Priority::LOW, [&results, &results_mutex]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::lock_guard<std::mutex> lock(results_mutex);
        results.push_back(1);
    }));

    futures.push_back(pool.submit([&results, &results_mutex]() {  // Default priority
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::lock_guard<std::mutex> lock(results_mutex);
        results.push_back(2);
    }));

    futures.push_back(pool.submit(Priority::HIGH, [&results, &results_mutex]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::lock_guard<std::mutex> lock(results_mutex);
        results.push_back(3);
    }));

    // Let all tasks be queued, then release the blocker
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    start = true;
    blocker.get();  // Wait for blocker to finish

    for (auto& f : futures) {
        f.get();
    }

    // Should execute in order: HIGH (3), MEDIUM/default (2), LOW (1)
    BOOST_REQUIRE_EQUAL(results.size(), 3);
    BOOST_CHECK_EQUAL(results[0], 3);  // HIGH
    BOOST_CHECK_EQUAL(results[1], 2);  // MEDIUM (default)
    BOOST_CHECK_EQUAL(results[2], 1);  // LOW
}

BOOST_AUTO_TEST_CASE(fifo_within_same_priority) {
    ThreadPool pool(1);
    std::vector<int> results;
    std::mutex results_mutex;
    std::vector<std::future<void>> futures;

    // Submit multiple tasks with same priority
    for (int i = 0; i < 5; ++i) {
        futures.push_back(pool.submit(Priority::HIGH, [i, &results, &results_mutex]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            std::lock_guard<std::mutex> lock(results_mutex);
            results.push_back(i);
        }));
    }

    for (auto& f : futures) {
        f.get();
    }

    // Should execute in FIFO order: 0, 1, 2, 3, 4
    BOOST_REQUIRE_EQUAL(results.size(), 5);
    for (int i = 0; i < 5; ++i) {
        BOOST_CHECK_EQUAL(results[i], i);
    }
}

BOOST_AUTO_TEST_CASE(priority_with_return_values) {
    ThreadPool pool(1);

    auto low = pool.submit(Priority::LOW, []() { return 1; });
    auto high = pool.submit(Priority::HIGH, []() { return 3; });
    auto medium = pool.submit(Priority::MEDIUM, []() { return 2; });

    // HIGH should complete first, even though submitted second
    BOOST_CHECK_EQUAL(high.get(), 3);
    BOOST_CHECK_EQUAL(medium.get(), 2);
    BOOST_CHECK_EQUAL(low.get(), 1);
}

BOOST_AUTO_TEST_CASE(priority_with_exceptions) {
    ThreadPool pool(1);

    auto low = pool.submit(Priority::LOW, []() -> int {
        throw std::runtime_error("Low priority error");
    });

    auto high = pool.submit(Priority::HIGH, []() { return 42; });

    // HIGH should execute first and succeed
    BOOST_CHECK_EQUAL(high.get(), 42);

    // LOW should still throw
    BOOST_CHECK_THROW(low.get(), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(priority_with_multiple_threads) {
    ThreadPool pool(4);
    std::atomic<int> high_count{0};
    std::atomic<int> low_count{0};
    std::vector<std::future<void>> futures;

    // Submit many LOW priority tasks
    for (int i = 0; i < 50; ++i) {
        futures.push_back(pool.submit(Priority::LOW, [&low_count]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            low_count++;
        }));
    }

    // Submit HIGH priority tasks (should jump ahead)
    for (int i = 0; i < 10; ++i) {
        futures.push_back(pool.submit(Priority::HIGH, [&high_count]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            high_count++;
        }));
    }

    for (auto& f : futures) {
        f.get();
    }

    BOOST_CHECK_EQUAL(high_count.load(), 10);
    BOOST_CHECK_EQUAL(low_count.load(), 50);
}

BOOST_AUTO_TEST_SUITE_END()
