# ThreadPool - High-Performance C++ Thread Pool Library

A production-quality, header-only C++ thread pool implementation with advanced scheduling and memory management features.

## Features

### Phase 1: Core Thread Pool
- Fixed-size thread pool with configurable worker threads
- Thread-safe task queue with mutex/condition variable synchronization
- `std::future`-based result retrieval
- Exception safety and graceful shutdown (RAII)
- Comprehensive unit tests with Boost.Test

### Phase 2: Advanced Scheduling ✅
- ✅ Priority queue support (HIGH/MEDIUM/LOW priority tasks with FIFO within same priority)
- ✅ Work-stealing thread pool (per-thread queues, automatic load balancing, 3.24x faster throughput)
- ✅ Task dependency management (DAG execution with circular dependency detection)

### Phase 3: Performance & Memory ✅
- ✅ Cache alignment (alignas(64)) to prevent false sharing
- ✅ Thread-local storage for O(1) worker ID lookup (was O(n))
- ✅ Incremental cycle detection (O(k) vs O(n²), 1000x faster for large DAGs)
- ✅ Periodic task cleanup to prevent memory leaks
- ✅ Comprehensive benchmarking suite (see benchmarks/benchmark_phase3.cpp)

**Benchmark Results** (11-core system):
- Submit latency: WorkStealingThreadPool 1.5 µs/task, DependencyThreadPool 4.9 µs/task
- Peak throughput: 680k tasks/sec (4 threads, WorkStealingThreadPool)
- Cycle detection: 1000-task chain in 7.1 ms
- Memory cleanup: Bounded memory usage vs unbounded growth

## Quick Start

### Requirements
- C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
- CMake 3.14+
- Boost (for testing only)

### Building

```bash
# Configure
cmake -S . -B build

# Build
cmake --build build

# Run tests
cd build && ctest

# Run examples
./build/examples/basic_example
./build/examples/parallel_sum
./build/examples/priority_example
./build/examples/workstealing_example
./build/examples/dependency_example

# Run benchmarks
./build/benchmarks/benchmark_threadpool
./build/benchmarks/compare_pools  # Compare Priority vs Work-Stealing
```

### Basic Usage

```cpp
#include "threadpool/ThreadPool.hpp"

int main() {
    // Create pool with 4 worker threads
    threadpool::ThreadPool pool(4);

    // Submit task and get future (default MEDIUM priority)
    auto future = pool.submit([]() {
        return 42;
    });

    // Wait for result
    int result = future.get();  // result == 42

    // Submit task with parameters
    auto future2 = pool.submit([](int a, int b) {
        return a + b;
    }, 10, 32);

    int sum = future2.get();  // sum == 42
}
```

### Priority Queue Usage

```cpp
#include "threadpool/ThreadPool.hpp"

using namespace threadpool;

int main() {
    ThreadPool pool(4);

    // Submit tasks with different priorities
    auto low = pool.submit(Priority::LOW, []() {
        return "background job";
    });

    auto high = pool.submit(Priority::HIGH, []() {
        return "urgent task";
    });

    auto medium = pool.submit(Priority::MEDIUM, []() {
        return "normal request";
    });

    // HIGH priority tasks execute first, even if submitted last
    std::cout << high.get() << std::endl;    // Executes first
    std::cout << medium.get() << std::endl;  // Executes second
    std::cout << low.get() << std::endl;     // Executes last
}
```

### Work-Stealing Thread Pool Usage

```cpp
#include "threadpool/WorkStealingThreadPool.hpp"

using namespace threadpool;

int main() {
    WorkStealingThreadPool pool(8);

    // Per-thread queues reduce lock contention
    // Idle threads automatically steal work from busy threads
    std::vector<std::future<int>> futures;

    for (int i = 0; i < 1000; ++i) {
        futures.push_back(pool.submit([i]() {
            return compute(i);  // Automatic load balancing
        }));
    }

    // 3.24x faster throughput than priority queue for high concurrency!
}
```

### Dependency Thread Pool Usage

```cpp
#include "threadpool/DependencyThreadPool.hpp"

using namespace threadpool;

int main() {
    DependencyThreadPool pool(4);

    // Create tasks with dependencies
    // Task A - no dependencies
    auto [idA, futureA] = pool.submit(Priority::HIGH, []() {
        return "data";
    });

    // Task B - depends on A
    auto [idB, futureB] = pool.submit(Priority::MEDIUM, {idA}, [](std::string data) {
        return data + "_processed";
    }, futureA.get());

    // Task C - also depends on A (runs in parallel with B)
    auto [idC, futureC] = pool.submit(Priority::MEDIUM, {idA}, [](std::string data) {
        return data + "_validated";
    }, futureA.get());

    // Task D - depends on both B and C
    auto [idD, futureD] = pool.submit(Priority::HIGH, {idB, idC},
        [](std::string b, std::string c) {
            return b + "_" + c;
        }, futureB.get(), futureC.get());

    std::cout << futureD.get() << std::endl;  // "data_processed_data_validated"

    // Circular dependencies are automatically detected and rejected!
}
```

## Project Structure

```
ThreadPool/
├── include/
│   └── threadpool/
│       ├── ThreadPool.hpp             # Priority queue thread pool
│       ├── WorkStealingThreadPool.hpp # Work-stealing thread pool
│       └── DependencyThreadPool.hpp   # DAG dependency thread pool
├── tests/
│   ├── test_threadpool.cpp            # Priority pool tests (24 tests)
│   ├── test_workstealing.cpp          # Work-stealing tests (20 tests)
│   ├── test_dependency.cpp            # Dependency pool tests (23 tests)
│   └── test_phase3_optimizations.cpp  # Phase 3 optimization tests (14 tests)
├── examples/
│   ├── basic_example.cpp              # Simple usage examples
│   ├── parallel_sum.cpp               # Parallel computation demo
│   ├── priority_example.cpp           # Priority scheduling demo
│   ├── workstealing_example.cpp       # Work-stealing demo
│   └── dependency_example.cpp         # DAG execution demo
├── benchmarks/
│   ├── benchmark_threadpool.cpp       # Basic benchmarks
│   ├── compare_pools.cpp              # Priority vs Work-Stealing comparison
│   └── benchmark_phase3.cpp           # Phase 3 optimization benchmarks
└── CMakeLists.txt                     # Build configuration
```

## Roadmap

- [x] Phase 1: Core thread pool with mutex/condition_variable
- [x] Phase 2.1: Priority queue scheduling (HIGH/MEDIUM/LOW)
- [x] Phase 2.2: Work-stealing queue for load balancing
- [x] Phase 2.3: Task dependency management (DAG with circular dependency detection)
- [x] Phase 3: Performance optimizations (cache alignment, thread-local storage, incremental algorithms, memory cleanup)
- [ ] Phase 4: Integration into web service (image processing API)

## Learning Goals

This project demonstrates understanding of:
- **Memory Management:** Custom allocators, memory pools, cache-aware design
- **Concurrency Primitives:** Mutexes, condition variables, atomics, lock-free structures
- **Scheduling Algorithms:** FIFO, priority-based, work-stealing
- **Performance Analysis:** Lock contention, cache effects, profiling

## Author

Alexi Montesinos
