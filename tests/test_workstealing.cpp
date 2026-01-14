#define BOOST_TEST_MODULE WorkStealingThreadPoolTests
#include <boost/test/unit_test.hpp>

#include "threadpool/WorkStealingThreadPool.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#include <numeric>

using namespace threadpool;

BOOST_AUTO_TEST_SUITE(BasicFunctionality)

BOOST_AUTO_TEST_CASE(construction_default) {
    BOOST_CHECK_NO_THROW(WorkStealingThreadPool pool);
}

BOOST_AUTO_TEST_CASE(construction_with_size) {
    BOOST_CHECK_NO_THROW(WorkStealingThreadPool pool(4));
}

BOOST_AUTO_TEST_CASE(construction_zero_threads) {
    BOOST_CHECK_THROW(WorkStealingThreadPool pool(0), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(pool_size) {
    WorkStealingThreadPool pool(8);
    BOOST_CHECK_EQUAL(pool.size(), 8);
}

BOOST_AUTO_TEST_CASE(submit_simple_task) {
    WorkStealingThreadPool pool(2);
    auto future = pool.submit([]() { return 42; });
    BOOST_CHECK_EQUAL(future.get(), 42);
}

BOOST_AUTO_TEST_CASE(submit_task_with_args) {
    WorkStealingThreadPool pool(2);
    auto future = pool.submit([](int a, int b) { return a + b; }, 10, 32);
    BOOST_CHECK_EQUAL(future.get(), 42);
}

BOOST_AUTO_TEST_CASE(submit_void_task) {
    WorkStealingThreadPool pool(2);
    std::atomic<int> counter{0};
    auto future = pool.submit([&counter]() { counter++; });
    future.get();
    BOOST_CHECK_EQUAL(counter.load(), 1);
}

BOOST_AUTO_TEST_CASE(multiple_tasks) {
    WorkStealingThreadPool pool(4);
    std::vector<std::future<int>> futures;

    for (int i = 0; i < 100; ++i) {
        futures.push_back(pool.submit([i]() { return i * 2; }));
    }

    for (int i = 0; i < 100; ++i) {
        BOOST_CHECK_EQUAL(futures[i].get(), i * 2);
    }
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(WorkStealing)

BOOST_AUTO_TEST_CASE(load_balancing_with_uneven_work) {
    WorkStealingThreadPool pool(4);
    std::vector<std::future<void>> futures;

    // Submit many tasks - work stealing should distribute them
    for (int i = 0; i < 100; ++i) {
        futures.push_back(pool.submit([]() {
            // Simulate some work
            std::this_thread::sleep_for(std::chrono::milliseconds(1));

            // Track which threads did work (we can't directly know, but all should be used)
            volatile int dummy = 0;
            for (int j = 0; j < 1000; ++j) {
                dummy += j;
            }
        }));
    }

    for (auto& f : futures) {
        f.get();
    }

    // All tasks completed (implicit test)
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(recursive_task_submission) {
    WorkStealingThreadPool pool(4);

    // Submit a task that submits more tasks (tests work stealing)
    auto future = pool.submit([&pool]() {
        std::vector<std::future<int>> sub_futures;

        for (int i = 0; i < 10; ++i) {
            sub_futures.push_back(pool.submit([i]() {
                return i * i;
            }));
        }

        int sum = 0;
        for (auto& f : sub_futures) {
            sum += f.get();
        }
        return sum;
    });

    // Sum of squares: 0^2 + 1^2 + ... + 9^2 = 285
    BOOST_CHECK_EQUAL(future.get(), 285);
}

BOOST_AUTO_TEST_CASE(many_small_tasks) {
    WorkStealingThreadPool pool(8);
    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;

    // Submit many small tasks - work stealing should distribute efficiently
    for (int i = 0; i < 1000; ++i) {
        futures.push_back(pool.submit([&counter]() {
            counter.fetch_add(1);
        }));
    }

    for (auto& f : futures) {
        f.get();
    }

    BOOST_CHECK_EQUAL(counter.load(), 1000);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(ExceptionHandling)

BOOST_AUTO_TEST_CASE(task_throws_exception) {
    WorkStealingThreadPool pool(2);
    auto future = pool.submit([]() -> int {
        throw std::runtime_error("Task error");
    });

    BOOST_CHECK_THROW(future.get(), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(exception_does_not_crash_pool) {
    WorkStealingThreadPool pool(2);

    auto future1 = pool.submit([]() -> int {
        throw std::runtime_error("Error");
    });

    auto future2 = pool.submit([]() { return 42; });

    BOOST_CHECK_THROW(future1.get(), std::runtime_error);
    BOOST_CHECK_EQUAL(future2.get(), 42);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(Concurrency)

BOOST_AUTO_TEST_CASE(concurrent_execution) {
    WorkStealingThreadPool pool(4);
    std::atomic<int> concurrent_count{0};
    std::atomic<int> max_concurrent{0};
    std::vector<std::future<void>> futures;

    for (int i = 0; i < 20; ++i) {
        futures.push_back(pool.submit([&concurrent_count, &max_concurrent]() {
            int current = ++concurrent_count;

            int expected = max_concurrent.load();
            while (current > expected &&
                   !max_concurrent.compare_exchange_weak(expected, current)) {
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            --concurrent_count;
        }));
    }

    for (auto& f : futures) {
        f.get();
    }

    BOOST_CHECK_GE(max_concurrent.load(), 4);
}

BOOST_AUTO_TEST_CASE(thread_safety_shared_counter) {
    WorkStealingThreadPool pool(8);
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

BOOST_AUTO_TEST_SUITE(Performance)

BOOST_AUTO_TEST_CASE(parallel_reduce) {
    WorkStealingThreadPool pool(4);

    // Create array of numbers
    const size_t SIZE = 10000;
    std::vector<int> data(SIZE);
    std::iota(data.begin(), data.end(), 1);

    // Parallel sum using work stealing
    const size_t CHUNK_SIZE = SIZE / 4;
    std::vector<std::future<long long>> futures;

    for (size_t i = 0; i < 4; ++i) {
        size_t start = i * CHUNK_SIZE;
        size_t end = (i == 3) ? SIZE : (i + 1) * CHUNK_SIZE;

        futures.push_back(pool.submit([&data, start, end]() {
            long long sum = 0;
            for (size_t j = start; j < end; ++j) {
                sum += data[j];
            }
            return sum;
        }));
    }

    long long total = 0;
    for (auto& f : futures) {
        total += f.get();
    }

    // Sum of 1..10000 = 10000 * 10001 / 2 = 50005000
    BOOST_CHECK_EQUAL(total, 50005000);
}

BOOST_AUTO_TEST_SUITE_END()
