# ThreadPool - High-Performance C++ Thread Pool Library

A production-quality, header-only C++ thread pool implementation with advanced scheduling and memory management features.

## Features

### Phase 1: Core Thread Pool
- Fixed-size thread pool with configurable worker threads
- Thread-safe task queue with mutex/condition variable synchronization
- `std::future`-based result retrieval
- Exception safety and graceful shutdown (RAII)
- Comprehensive unit tests with Boost.Test

### Phase 2: Advanced Scheduling (In Progress)
- Priority queue support (HIGH/MEDIUM/LOW priority tasks with FIFO within same priority)
- Work-stealing queue for load balancing (Planned)
- Task dependency management (DAG execution) (Planned)

### Phase 3: Performance & Memory (Planned)
- Custom memory pool allocator for reduced allocation overhead
- Thread-local storage for lock-free fast paths
- Cache-aware design to minimize false sharing
- Comprehensive benchmarking suite

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

# Run benchmarks
./build/benchmarks/benchmark_threadpool
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

## Project Structure

```
ThreadPool/
├── include/
│   └── threadpool/
│       └── ThreadPool.hpp        # Main thread pool implementation
├── tests/
│   └── test_threadpool.cpp       # Boost.Test unit tests
├── examples/
│   ├── basic_example.cpp         # Simple usage examples
│   ├── parallel_sum.cpp          # Parallel computation demo
│   └── priority_example.cpp      # Priority scheduling demo
├── benchmarks/
│   └── benchmark_threadpool.cpp  # Performance benchmarks
└── CMakeLists.txt                # Build configuration
```

## Roadmap

- [x] Phase 1: Core thread pool with mutex/condition_variable
- [x] Phase 2.1: Priority queue scheduling (HIGH/MEDIUM/LOW)
- [ ] Phase 2.2: Work-stealing queue for load balancing
- [ ] Phase 2.3: Task dependency management (DAG)
- [ ] Phase 3: Custom memory allocator and performance optimization
- [ ] Phase 4: Integration into web service (image processing API)

## Learning Goals

This project demonstrates understanding of:
- **Memory Management:** Custom allocators, memory pools, cache-aware design
- **Concurrency Primitives:** Mutexes, condition variables, atomics, lock-free structures
- **Scheduling Algorithms:** FIFO, priority-based, work-stealing
- **Performance Analysis:** Lock contention, cache effects, profiling

## Author

Alexi Montesinos
