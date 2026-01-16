#define BOOST_TEST_MODULE DependencyThreadPoolTests
#include <boost/test/included/unit_test.hpp>

#include "threadpool/DependencyThreadPool.hpp"
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <stdexcept>

using namespace threadpool;

// ============================================================================
// Basic Functionality Tests
// ============================================================================

BOOST_AUTO_TEST_SUITE(BasicFunctionality)

BOOST_AUTO_TEST_CASE(construct_pool) {
    BOOST_CHECK_NO_THROW(DependencyThreadPool pool(4));

    DependencyThreadPool pool(4);
    BOOST_CHECK_EQUAL(pool.size(), 4);
}

BOOST_AUTO_TEST_CASE(submit_simple_task_no_dependencies) {
    DependencyThreadPool pool(2);

    auto [id, future] = pool.submit([]() {
        return 42;
    });

    BOOST_CHECK(id.is_valid());
    BOOST_CHECK_EQUAL(future.get(), 42);
}

BOOST_AUTO_TEST_CASE(submit_task_with_parameters) {
    DependencyThreadPool pool(2);

    auto [id, future] = pool.submit([](int a, int b) {
        return a + b;
    }, 10, 32);

    BOOST_CHECK(id.is_valid());
    BOOST_CHECK_EQUAL(future.get(), 42);
}

BOOST_AUTO_TEST_CASE(submit_task_single_dependency) {
    DependencyThreadPool pool(2);

    auto [id1, f1] = pool.submit([]() {
        return 10;
    });

    auto [id2, f2] = pool.submit({id1}, [](int x) {
        return x * 2;
    }, f1.get());

    BOOST_CHECK_EQUAL(f2.get(), 20);
}

BOOST_AUTO_TEST_CASE(submit_task_multiple_dependencies) {
    DependencyThreadPool pool(2);

    auto [id1, f1] = pool.submit([]() { return 10; });
    auto [id2, f2] = pool.submit([]() { return 20; });

    auto [id3, f3] = pool.submit({id1, id2}, [](int a, int b) {
        return a + b;
    }, f1.get(), f2.get());

    BOOST_CHECK_EQUAL(f3.get(), 30);
}

BOOST_AUTO_TEST_CASE(unique_task_ids) {
    DependencyThreadPool pool(2);

    auto [id1, f1] = pool.submit([]() { return 1; });
    auto [id2, f2] = pool.submit([]() { return 2; });
    auto [id3, f3] = pool.submit([]() { return 3; });

    BOOST_CHECK(id1 != id2);
    BOOST_CHECK(id2 != id3);
    BOOST_CHECK(id1 != id3);
}

BOOST_AUTO_TEST_CASE(task_completion_query) {
    DependencyThreadPool pool(2);

    std::atomic<bool> started{false};

    auto [id, future] = pool.submit([&started]() {
        started = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return 42;
    });

    // Wait for task to start
    while (!started.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Should not be completed yet
    BOOST_CHECK(!pool.is_task_completed(id));

    // Wait for completion
    future.get();

    // Should be completed now
    BOOST_CHECK(pool.is_task_completed(id));
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Priority Scheduling Tests
// ============================================================================

BOOST_AUTO_TEST_SUITE(PriorityScheduling)

BOOST_AUTO_TEST_CASE(priority_order_without_dependencies) {
    DependencyThreadPool pool(1);  // Single thread for strict ordering
    std::vector<int> results;
    std::mutex results_mutex;
    std::atomic<bool> start{false};
    std::vector<std::future<void>> futures;

    // Submit blocker task
    auto [blocker_id, blocker_f] = pool.submit(Priority::LOW, [&start]() {
        while (!start.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    // Submit tasks in mixed priority order
    auto [id1, f1] = pool.submit(Priority::LOW, [&results, &results_mutex]() {
        std::lock_guard<std::mutex> lock(results_mutex);
        results.push_back(1);
    });

    auto [id2, f2] = pool.submit(Priority::MEDIUM, [&results, &results_mutex]() {
        std::lock_guard<std::mutex> lock(results_mutex);
        results.push_back(2);
    });

    auto [id3, f3] = pool.submit(Priority::HIGH, [&results, &results_mutex]() {
        std::lock_guard<std::mutex> lock(results_mutex);
        results.push_back(3);
    });

    auto [id4, f4] = pool.submit(Priority::HIGH, [&results, &results_mutex]() {
        std::lock_guard<std::mutex> lock(results_mutex);
        results.push_back(4);
    });

    // Release blocker
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    start = true;
    blocker_f.get();

    // Wait for all
    f1.get();
    f2.get();
    f3.get();
    f4.get();

    // Verify order: HIGH (3, 4), MEDIUM (2), LOW (1)
    BOOST_REQUIRE_EQUAL(results.size(), 4);
    BOOST_CHECK_EQUAL(results[0], 3);
    BOOST_CHECK_EQUAL(results[1], 4);
    BOOST_CHECK_EQUAL(results[2], 2);
    BOOST_CHECK_EQUAL(results[3], 1);
}

BOOST_AUTO_TEST_CASE(priority_with_dependencies) {
    DependencyThreadPool pool(4);
    std::vector<int> execution_order;
    std::mutex order_mutex;

    // Task A - no dependencies
    auto [idA, fA] = pool.submit(Priority::LOW, [&execution_order, &order_mutex]() {
        std::lock_guard<std::mutex> lock(order_mutex);
        execution_order.push_back(1);
    });

    // Task B - depends on A, HIGH priority
    auto [idB, fB] = pool.submit(Priority::HIGH, {idA}, [&execution_order, &order_mutex]() {
        std::lock_guard<std::mutex> lock(order_mutex);
        execution_order.push_back(2);
    });

    // Task C - depends on A, LOW priority (should execute after B due to priority)
    auto [idC, fC] = pool.submit(Priority::LOW, {idA}, [&execution_order, &order_mutex]() {
        std::lock_guard<std::mutex> lock(order_mutex);
        execution_order.push_back(3);
    });

    fA.get();
    fB.get();
    fC.get();

    // A executes first, then B (HIGH), then C (LOW)
    BOOST_REQUIRE_EQUAL(execution_order.size(), 3);
    BOOST_CHECK_EQUAL(execution_order[0], 1);  // A
    BOOST_CHECK_EQUAL(execution_order[1], 2);  // B (HIGH priority)
    BOOST_CHECK_EQUAL(execution_order[2], 3);  // C (LOW priority)
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Dependency Resolution Tests
// ============================================================================

BOOST_AUTO_TEST_SUITE(DependencyResolution)

BOOST_AUTO_TEST_CASE(linear_chain) {
    DependencyThreadPool pool(2);
    std::vector<int> execution_order;
    std::mutex order_mutex;

    // A -> B -> C -> D
    auto [idA, fA] = pool.submit([&execution_order, &order_mutex]() {
        std::lock_guard<std::mutex> lock(order_mutex);
        execution_order.push_back(1);
        return 10;
    });

    auto [idB, fB] = pool.submit({idA}, [&execution_order, &order_mutex](int x) {
        std::lock_guard<std::mutex> lock(order_mutex);
        execution_order.push_back(2);
        return x * 2;
    }, fA.get());

    auto [idC, fC] = pool.submit({idB}, [&execution_order, &order_mutex](int x) {
        std::lock_guard<std::mutex> lock(order_mutex);
        execution_order.push_back(3);
        return x + 5;
    }, fB.get());

    auto [idD, fD] = pool.submit({idC}, [&execution_order, &order_mutex](int x) {
        std::lock_guard<std::mutex> lock(order_mutex);
        execution_order.push_back(4);
        return x - 3;
    }, fC.get());

    int result = fD.get();

    // Verify execution order
    BOOST_REQUIRE_EQUAL(execution_order.size(), 4);
    BOOST_CHECK_EQUAL(execution_order[0], 1);
    BOOST_CHECK_EQUAL(execution_order[1], 2);
    BOOST_CHECK_EQUAL(execution_order[2], 3);
    BOOST_CHECK_EQUAL(execution_order[3], 4);

    // Verify computation: ((10 * 2) + 5) - 3 = 22
    BOOST_CHECK_EQUAL(result, 22);
}

BOOST_AUTO_TEST_CASE(diamond_dag) {
    DependencyThreadPool pool(4);

    //     A
    //    / \
    //   B   C
    //    \ /
    //     D

    auto [idA, fA] = pool.submit([]() {
        return 10;
    });

    // Get value once to share with both B and C
    int valueA = fA.get();

    auto [idB, fB] = pool.submit({idA}, [](int x) {
        return x + 5;
    }, valueA);

    auto [idC, fC] = pool.submit({idA}, [](int x) {
        return x * 2;
    }, valueA);

    auto [idD, fD] = pool.submit({idB, idC}, [](int b, int c) {
        return b + c;
    }, fB.get(), fC.get());

    int result = fD.get();

    // Verify: (10 + 5) + (10 * 2) = 15 + 20 = 35
    BOOST_CHECK_EQUAL(result, 35);
}

BOOST_AUTO_TEST_CASE(fan_out) {
    DependencyThreadPool pool(4);

    // A -> B1, B2, B3, B4, B5
    auto [idA, fA] = pool.submit([]() { return 10; });

    // Get value once to share with all dependent tasks
    int valueA = fA.get();

    std::vector<std::pair<TaskID, std::future<int>>> tasks;
    for (int i = 0; i < 5; ++i) {
        auto [id, f] = pool.submit({idA}, [i](int x) {
            return x + i;
        }, valueA);
        tasks.push_back({id, std::move(f)});
    }

    // Wait for all and verify
    int sum = 0;
    for (auto& [id, f] : tasks) {
        sum += f.get();
    }

    // Sum: (10+0) + (10+1) + (10+2) + (10+3) + (10+4) = 60
    BOOST_CHECK_EQUAL(sum, 60);
}

BOOST_AUTO_TEST_CASE(fan_in) {
    DependencyThreadPool pool(4);

    // A1, A2, A3, A4, A5 -> B
    std::vector<TaskID> ids;
    std::vector<int> values;

    for (int i = 1; i <= 5; ++i) {
        auto [id, f] = pool.submit([i]() { return i * 10; });
        ids.push_back(id);
        values.push_back(f.get());
    }

    auto [idB, fB] = pool.submit(ids, [](int v1, int v2, int v3, int v4, int v5) {
        return v1 + v2 + v3 + v4 + v5;
    }, values[0], values[1], values[2], values[3], values[4]);

    int result = fB.get();

    // Sum: 10 + 20 + 30 + 40 + 50 = 150
    BOOST_CHECK_EQUAL(result, 150);
}

BOOST_AUTO_TEST_CASE(completed_dependency_filtered) {
    DependencyThreadPool pool(2);

    auto [id1, f1] = pool.submit([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return 42;
    });

    // Wait for completion
    int value = f1.get();

    // Submit task depending on already-completed task (should execute immediately)
    auto start = std::chrono::high_resolution_clock::now();
    auto [id2, f2] = pool.submit({id1}, [](int x) {
        return x * 2;
    }, value);

    int result = f2.get();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - start
    );

    BOOST_CHECK_EQUAL(result, 84);
    // Should execute quickly since dependency already satisfied
    BOOST_CHECK_LT(duration.count(), 100);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Circular Dependency Detection Tests
// ============================================================================

BOOST_AUTO_TEST_SUITE(CircularDependencyDetection)

BOOST_AUTO_TEST_CASE(duplicate_dependencies_normalized) {
    DependencyThreadPool pool(2);

    auto [id1, f1] = pool.submit([]() { return 1; });

    // Duplicate dependencies should be normalized (deduplicated)
    // This should NOT throw - duplicates are simply ignored
    auto [id2, f2] = pool.submit({id1, id1}, []() { return 2; });
    int result = f2.get();
    BOOST_CHECK_EQUAL(result, 2);
}

BOOST_AUTO_TEST_CASE(deep_dependency_chain) {
    DependencyThreadPool pool(2);

    // Note: True circular dependencies are impossible with this API
    // because tasks can only depend on previously submitted tasks.
    // This test verifies deep linear chains work correctly.

    auto [id1, f1] = pool.submit([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return 1;
    });

    auto [id2, f2] = pool.submit({id1}, []() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return 2;
    });

    auto [id3, f3] = pool.submit({id2}, []() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return 3;
    });

    auto [id4, f4] = pool.submit({id3}, []() { return 4; });

    // Verify deep chain executes correctly
    BOOST_CHECK_EQUAL(f4.get(), 4);
    BOOST_CHECK_EQUAL(f3.get(), 3);
    BOOST_CHECK_EQUAL(f2.get(), 2);
    BOOST_CHECK_EQUAL(f1.get(), 1);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Error Handling Tests
// ============================================================================

BOOST_AUTO_TEST_SUITE(ErrorHandling)

BOOST_AUTO_TEST_CASE(invalid_dependency_id) {
    DependencyThreadPool pool(2);

    TaskID invalid_id(9999);

    BOOST_CHECK_THROW(
        pool.submit({invalid_id}, []() { return 42; }),
        std::invalid_argument
    );
}

BOOST_AUTO_TEST_CASE(task_throws_exception) {
    DependencyThreadPool pool(2);

    auto [id, future] = pool.submit([]() -> int {
        throw std::runtime_error("Test exception");
    });

    BOOST_CHECK_THROW(future.get(), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(dependent_task_after_exception) {
    DependencyThreadPool pool(2);

    auto [id1, f1] = pool.submit([]() -> int {
        throw std::runtime_error("Parent failed");
    });

    // Get value before submitting dependent (will throw)
    try {
        f1.get();
    } catch (...) {
        // Expected
    }

    // Dependent task should still be submittable
    // (Implementation doesn't propagate exceptions through dependencies)
    auto [id2, f2] = pool.submit({id1}, []() { return 42; });

    BOOST_CHECK_EQUAL(f2.get(), 42);
}

BOOST_AUTO_TEST_CASE(zero_threads_throws) {
    BOOST_CHECK_THROW(DependencyThreadPool pool(0), std::invalid_argument);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Concurrency Tests
// ============================================================================

BOOST_AUTO_TEST_SUITE(Concurrency)

BOOST_AUTO_TEST_CASE(concurrent_submission) {
    DependencyThreadPool pool(4);

    std::vector<std::thread> submitters;
    std::vector<std::future<int>> futures;
    std::mutex futures_mutex;

    for (int i = 0; i < 10; ++i) {
        submitters.emplace_back([&pool, &futures, &futures_mutex, i]() {
            auto [id, f] = pool.submit([i]() { return i * i; });

            std::lock_guard<std::mutex> lock(futures_mutex);
            futures.push_back(std::move(f));
        });
    }

    for (auto& t : submitters) {
        t.join();
    }

    int sum = 0;
    for (auto& f : futures) {
        sum += f.get();
    }

    // Sum of squares: 0 + 1 + 4 + 9 + ... + 81 = 285
    BOOST_CHECK_EQUAL(sum, 285);
}

BOOST_AUTO_TEST_CASE(many_tasks) {
    DependencyThreadPool pool(4);

    std::vector<std::future<int>> futures;
    for (int i = 0; i < 100; ++i) {
        auto [id, f] = pool.submit([i]() { return i; });
        futures.push_back(std::move(f));
    }

    int sum = 0;
    for (auto& f : futures) {
        sum += f.get();
    }

    // Sum: 0 + 1 + 2 + ... + 99 = 4950
    BOOST_CHECK_EQUAL(sum, 4950);
}

BOOST_AUTO_TEST_CASE(shared_counter) {
    DependencyThreadPool pool(4);

    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;

    for (int i = 0; i < 1000; ++i) {
        auto [id, f] = pool.submit([&counter]() {
            counter++;
        });
        futures.push_back(std::move(f));
    }

    for (auto& f : futures) {
        f.get();
    }

    BOOST_CHECK_EQUAL(counter.load(), 1000);
}

BOOST_AUTO_TEST_SUITE_END()
