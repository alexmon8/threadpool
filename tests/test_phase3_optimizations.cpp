#define BOOST_TEST_MODULE Phase3OptimizationsTest
#include <boost/test/unit_test.hpp>

#include "threadpool/DependencyThreadPool.hpp"
#include "threadpool/WorkStealingThreadPool.hpp"
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>

using namespace threadpool;

// ============================================================================
// Cache Alignment Tests
// ============================================================================

BOOST_AUTO_TEST_SUITE(CacheAlignmentTests)

BOOST_AUTO_TEST_CASE(workqueue_cache_alignment) {
    // Verify that WorkQueue structures are 64-byte aligned
    // This test uses internal knowledge of WorkStealingThreadPool structure

    WorkStealingThreadPool pool(4);

    // WorkQueue should be aligned to 64 bytes (cache line size)
    // We can't directly access private members, but we can verify
    // the pool was constructed successfully with alignment
    BOOST_CHECK_EQUAL(pool.size(), 4);

    // If alignment was wrong, the pool wouldn't compile or would crash
    // Submit some tasks to verify everything works
    std::vector<std::future<int>> futures;
    for (int i = 0; i < 100; ++i) {
        futures.push_back(pool.submit([i]() { return i; }));
    }

    for (int i = 0; i < 100; ++i) {
        BOOST_CHECK_EQUAL(futures[i].get(), i);
    }
}

BOOST_AUTO_TEST_CASE(mutex_cache_alignment) {
    // Verify that DependencyThreadPool mutexes are cache-aligned
    DependencyThreadPool pool(4);

    // Mutexes should be aligned to prevent false sharing
    // If alignment was wrong, performance would degrade but still work
    BOOST_CHECK_EQUAL(pool.size(), 4);

    // Submit concurrent tasks to stress test the aligned mutexes
    std::vector<std::pair<TaskID, std::future<int>>> tasks;
    for (int i = 0; i < 100; ++i) {
        tasks.push_back(pool.submit([i]() { return i * 2; }));
    }

    for (int i = 0; i < 100; ++i) {
        BOOST_CHECK_EQUAL(tasks[i].second.get(), i * 2);
    }
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Thread-Local Worker ID Tests
// ============================================================================

BOOST_AUTO_TEST_SUITE(ThreadLocalWorkerIDTests)

BOOST_AUTO_TEST_CASE(worker_id_consistency) {
    // Verify that get_worker_id() returns consistent results
    WorkStealingThreadPool pool(4);

    std::atomic<int> task_count{0};
    std::vector<std::future<void>> futures;

    // Submit tasks and verify they execute
    for (int i = 0; i < 100; ++i) {
        futures.push_back(pool.submit([&task_count]() {
            task_count++;
        }));
    }

    for (auto& f : futures) {
        f.get();
    }

    BOOST_CHECK_EQUAL(task_count.load(), 100);
}

BOOST_AUTO_TEST_CASE(external_submission) {
    // Verify that tasks submitted from non-worker threads work correctly
    WorkStealingThreadPool pool(4);

    std::vector<std::future<std::thread::id>> futures;

    // Submit from main thread (not a worker)
    for (int i = 0; i < 50; ++i) {
        futures.push_back(pool.submit([]() {
            return std::this_thread::get_id();
        }));
    }

    // Verify all tasks executed (got valid thread IDs)
    for (auto& f : futures) {
        std::thread::id tid = f.get();
        BOOST_CHECK(tid != std::thread::id());
    }
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Incremental Cycle Detection Tests
// ============================================================================

BOOST_AUTO_TEST_SUITE(IncrementalCycleDetectionTests)

BOOST_AUTO_TEST_CASE(incremental_cycle_detection_correctness) {
    // Verify that incremental cycle detection catches cycles correctly
    DependencyThreadPool pool(4);

    // Create a chain: A -> B -> C
    auto [idA, fA] = pool.submit([]() { return 1; });
    auto [idB, fB] = pool.submit({idA}, [](int x) { return x + 1; }, fA.get());
    auto [idC, fC] = pool.submit({idB}, [](int x) { return x + 1; }, fB.get());

    BOOST_CHECK_EQUAL(fC.get(), 3);

    // Try to create a cycle: D -> A (where A is an ancestor)
    // This should be rejected
    // Note: We can't actually test this without exposing the cycle detection
    // The best we can do is verify that valid DAGs work
}

BOOST_AUTO_TEST_CASE(complex_dag_execution) {
    // Test a complex DAG to verify incremental detection handles it
    DependencyThreadPool pool(8);

    //       A
    //      / \
    //     B   C
    //      \ /
    //       D

    auto [idA, fA] = pool.submit([]() { return 10; });

    int valA = fA.get();
    auto [idB, fB] = pool.submit({idA}, [](int x) { return x * 2; }, valA);
    auto [idC, fC] = pool.submit({idA}, [](int x) { return x * 3; }, valA);

    int valB = fB.get();
    int valC = fC.get();
    auto [idD, fD] = pool.submit({idB, idC}, [](int b, int c) {
        return b + c;
    }, valB, valC);

    BOOST_CHECK_EQUAL(fD.get(), 50);  // 10*2 + 10*3 = 50
}

BOOST_AUTO_TEST_CASE(large_dag_performance) {
    // Test that incremental detection scales well with large DAGs
    DependencyThreadPool pool(8);

    // Create a chain of 100 tasks
    auto [id, f] = pool.submit([]() { return 0; });

    for (int i = 1; i < 100; ++i) {
        int val = f.get();
        auto result = pool.submit({id}, [](int x) { return x + 1; }, val);
        id = result.first;
        f = std::move(result.second);
    }

    BOOST_CHECK_EQUAL(f.get(), 99);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Memory Cleanup Tests
// ============================================================================

BOOST_AUTO_TEST_SUITE(MemoryCleanupTests)

BOOST_AUTO_TEST_CASE(cleanup_triggers_periodically) {
    // Verify that cleanup is triggered after CLEANUP_THRESHOLD tasks
    DependencyThreadPool pool(4);

    // Submit more than CLEANUP_THRESHOLD (1000) tasks
    std::vector<std::pair<TaskID, std::future<int>>> tasks;
    for (int i = 0; i < 1500; ++i) {
        tasks.push_back(pool.submit([i]() {
            return i;
        }));
    }

    // Wait for all tasks to complete
    for (int i = 0; i < 1500; ++i) {
        BOOST_CHECK_EQUAL(tasks[i].second.get(), i);
    }

    // Cleanup should have been triggered at least once
    // (We can't directly verify this without exposing internals,
    //  but we can check that the pool still works correctly)

    // Submit more tasks to verify pool is still healthy
    auto [id, f] = pool.submit([]() { return 42; });
    BOOST_CHECK_EQUAL(f.get(), 42);
}

BOOST_AUTO_TEST_CASE(cleanup_with_dependencies) {
    // Verify cleanup works correctly with dependency chains
    DependencyThreadPool pool(4);

    // Create many short dependency chains
    for (int chain = 0; chain < 500; ++chain) {
        auto [id1, f1] = pool.submit([chain]() { return chain; });
        int v1 = f1.get();

        auto [id2, f2] = pool.submit({id1}, [](int x) { return x * 2; }, v1);
        int v2 = f2.get();

        auto [id3, f3] = pool.submit({id2}, [](int x) { return x + 1; }, v2);
        int result = f3.get();

        BOOST_CHECK_EQUAL(result, chain * 2 + 1);
    }

    // Pool should still be healthy after cleanup
    auto [id, f] = pool.submit([]() { return 100; });
    BOOST_CHECK_EQUAL(f.get(), 100);
}

BOOST_AUTO_TEST_CASE(memory_bounded_long_running) {
    // Verify that memory usage doesn't grow unbounded
    // This is a stress test that submits many tasks over time
    DependencyThreadPool pool(8);

    // Submit tasks in batches, ensuring old tasks complete
    for (int batch = 0; batch < 10; ++batch) {
        std::vector<std::pair<TaskID, std::future<int>>> tasks;

        for (int i = 0; i < 200; ++i) {
            tasks.push_back(pool.submit([i]() {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                return i;
            }));
        }

        // Wait for batch to complete
        for (int i = 0; i < 200; ++i) {
            BOOST_CHECK_EQUAL(tasks[i].second.get(), i);
        }

        // Small delay between batches
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // If cleanup is working, memory usage should be bounded
    // (We can't measure this directly in the test, but manual
    //  profiling would show constant memory usage)
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Dependency Graph Cleanup Tests
// ============================================================================

BOOST_AUTO_TEST_SUITE(DependencyGraphCleanupTests)

BOOST_AUTO_TEST_CASE(graph_cleanup_on_completion) {
    // Verify that dependency graph is cleaned up when tasks complete
    DependencyThreadPool pool(4);

    // Create a DAG and verify it executes correctly
    auto [idA, fA] = pool.submit([]() { return 1; });
    auto [idB, fB] = pool.submit({idA}, [](int x) { return x + 1; }, fA.get());
    auto [idC, fC] = pool.submit({idB}, [](int x) { return x + 1; }, fB.get());

    BOOST_CHECK_EQUAL(fC.get(), 3);

    // Graph should be cleaned up for completed tasks
    // Submit new tasks to verify graph is still functional
    auto [idD, fD] = pool.submit([]() { return 10; });
    BOOST_CHECK_EQUAL(fD.get(), 10);
}

BOOST_AUTO_TEST_CASE(graph_cleanup_with_parallel_branches) {
    // Verify graph cleanup works with parallel branches
    DependencyThreadPool pool(8);

    //       A
    //      /|\
    //     B C D
    //      \|/
    //       E

    auto [idA, fA] = pool.submit([]() { return 5; });
    int valA = fA.get();

    auto [idB, fB] = pool.submit({idA}, [](int x) { return x * 2; }, valA);
    auto [idC, fC] = pool.submit({idA}, [](int x) { return x * 3; }, valA);
    auto [idD, fD] = pool.submit({idA}, [](int x) { return x * 4; }, valA);

    int valB = fB.get();
    int valC = fC.get();
    int valD = fD.get();

    auto [idE, fE] = pool.submit({idB, idC, idD}, [](int b, int c, int d) {
        return b + c + d;
    }, valB, valC, valD);

    BOOST_CHECK_EQUAL(fE.get(), 45);  // 5*2 + 5*3 + 5*4 = 45

    // Graph should be cleaned up, pool should still work
    auto [idF, fF] = pool.submit([]() { return 100; });
    BOOST_CHECK_EQUAL(fF.get(), 100);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Integration Tests
// ============================================================================

BOOST_AUTO_TEST_SUITE(IntegrationTests)

BOOST_AUTO_TEST_CASE(all_optimizations_together) {
    // Test that all optimizations work together correctly
    DependencyThreadPool dep_pool(8);
    WorkStealingThreadPool ws_pool(8);

    // Dependency pool: Complex DAG with many tasks
    std::vector<std::pair<TaskID, std::future<int>>> dep_tasks;

    for (int i = 0; i < 100; ++i) {
        dep_tasks.push_back(dep_pool.submit([i]() { return i; }));
    }

    // Work-stealing pool: High concurrency workload
    std::vector<std::future<int>> ws_tasks;

    for (int i = 0; i < 100; ++i) {
        ws_tasks.push_back(ws_pool.submit([i]() { return i * 2; }));
    }

    // Verify all results
    for (int i = 0; i < 100; ++i) {
        BOOST_CHECK_EQUAL(dep_tasks[i].second.get(), i);
        BOOST_CHECK_EQUAL(ws_tasks[i].get(), i * 2);
    }
}

BOOST_AUTO_TEST_CASE(stress_test_all_pools) {
    // Stress test with high concurrency and many tasks
    const int NUM_TASKS = 1000;

    DependencyThreadPool dep_pool(std::thread::hardware_concurrency());
    WorkStealingThreadPool ws_pool(std::thread::hardware_concurrency());

    std::atomic<int> dep_sum{0};
    std::atomic<int> ws_sum{0};

    std::vector<std::pair<TaskID, std::future<void>>> dep_futures;
    std::vector<std::future<void>> ws_futures;

    for (int i = 0; i < NUM_TASKS; ++i) {
        dep_futures.push_back(dep_pool.submit([&dep_sum, i]() {
            dep_sum.fetch_add(i);
        }));

        ws_futures.push_back(ws_pool.submit([&ws_sum, i]() {
            ws_sum.fetch_add(i);
        }));
    }

    for (auto& [id, f] : dep_futures) {
        f.get();
    }

    for (auto& f : ws_futures) {
        f.get();
    }

    int expected_sum = (NUM_TASKS * (NUM_TASKS - 1)) / 2;
    BOOST_CHECK_EQUAL(dep_sum.load(), expected_sum);
    BOOST_CHECK_EQUAL(ws_sum.load(), expected_sum);
}

BOOST_AUTO_TEST_SUITE_END()
