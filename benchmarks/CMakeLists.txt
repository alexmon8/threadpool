# Simple custom benchmark (we'll add Google Benchmark later if needed)
add_executable(benchmark_threadpool benchmark_threadpool.cpp)
target_link_libraries(benchmark_threadpool threadpool)

# Comparison benchmark: Priority Queue vs Work-Stealing
add_executable(compare_pools compare_pools.cpp)
target_link_libraries(compare_pools threadpool)
