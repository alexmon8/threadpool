# Find Boost Test framework
find_package(Boost COMPONENTS unit_test_framework REQUIRED)

# Test executable
add_executable(threadpool_tests
    test_threadpool.cpp
)

target_link_libraries(threadpool_tests
    threadpool
    Boost::unit_test_framework
)

# Register tests with CTest
add_test(NAME ThreadPoolTests COMMAND threadpool_tests)
