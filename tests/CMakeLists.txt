# Find Boost Test framework
find_package(Boost COMPONENTS unit_test_framework REQUIRED)

# Test executable for priority queue thread pool
add_executable(threadpool_tests
    test_threadpool.cpp
)

target_link_libraries(threadpool_tests
    threadpool
    Boost::unit_test_framework
)

# Test executable for work-stealing thread pool
add_executable(workstealing_tests
    test_workstealing.cpp
)

target_link_libraries(workstealing_tests
    threadpool
    Boost::unit_test_framework
)

# Register tests with CTest
add_test(NAME ThreadPoolTests COMMAND threadpool_tests)
add_test(NAME WorkStealingTests COMMAND workstealing_tests)
