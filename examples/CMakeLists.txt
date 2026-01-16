# Basic example
add_executable(basic_example basic_example.cpp)
target_link_libraries(basic_example threadpool)

# Parallel computation example
add_executable(parallel_sum parallel_sum.cpp)
target_link_libraries(parallel_sum threadpool)

# Priority queue example
add_executable(priority_example priority_example.cpp)
target_link_libraries(priority_example threadpool)

# Work-stealing example
add_executable(workstealing_example workstealing_example.cpp)
target_link_libraries(workstealing_example threadpool)

# Dependency thread pool example
add_executable(dependency_example dependency_example.cpp)
target_link_libraries(dependency_example threadpool)
