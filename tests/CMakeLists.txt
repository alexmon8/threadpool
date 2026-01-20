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

# Test executable for dependency thread pool
add_executable(dependency_tests
    test_dependency.cpp
)

target_link_libraries(dependency_tests
    threadpool
    Boost::unit_test_framework
)

# Test executable for Phase 3 optimizations
add_executable(phase3_tests
    test_phase3_optimizations.cpp
)

target_link_libraries(phase3_tests
    threadpool
    Boost::unit_test_framework
)

# Register tests with CTest
add_test(NAME ThreadPoolTests COMMAND threadpool_tests)
add_test(NAME WorkStealingTests COMMAND workstealing_tests)
add_test(NAME DependencyTests COMMAND dependency_tests)
add_test(NAME Phase3OptimizationsTests COMMAND phase3_tests)
