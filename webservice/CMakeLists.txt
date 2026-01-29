# Image Processing Web Service
# Uses cpp-httplib for HTTP server and stb libraries for image processing

cmake_minimum_required(VERSION 3.14)

# Web service executable
add_executable(image_service
    src/main.cpp
    src/stb_impl.cpp
    src/ImageProcessor.cpp
    src/PoolOrchestrator.cpp
    src/routes.cpp
)

target_include_directories(image_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/include  # For threadpool headers
)

target_link_libraries(image_service
    threadpool
    Threads::Threads
)

# Compile definitions for stb headers (will add when we create ImageProcessor)
target_compile_definitions(image_service PRIVATE
    # STB_IMAGE_IMPLEMENTATION  # Will enable in ImageProcessor.cpp
    # STB_IMAGE_WRITE_IMPLEMENTATION
    # STB_IMAGE_RESIZE2_IMPLEMENTATION
)

# Set C++17 standard (same as main project)
target_compile_features(image_service PRIVATE cxx_std_17)

# Enable warnings for our code, but not for third-party stb libraries
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(image_service PRIVATE
        -Wall -Wextra -Wpedantic
    )
    # Disable warnings for stb implementation file
    set_source_files_properties(src/stb_impl.cpp PROPERTIES
        COMPILE_FLAGS "-w"  # Disable all warnings
    )
endif()

# Installation
install(TARGETS image_service DESTINATION bin)
