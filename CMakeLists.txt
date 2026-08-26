cmake_minimum_required(VERSION 3.20)
project(ThreadPool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(ThreadPool INTERFACE)

target_include_directories(ThreadPool INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

add_executable(PoolSample
        samples/main.cpp
        include/ThreadPool/ThreadPool.hpp)

target_link_libraries(PoolSample PRIVATE ThreadPool Threads::Threads)

#arget_compile_options(PoolSample PRIVATE -fsanitize=thread -g)
#target_link_options(PoolSample PRIVATE -fsanitize=thread)
