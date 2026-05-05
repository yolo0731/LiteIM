cmake_minimum_required(VERSION 3.16)

project(LiteIM
    VERSION 0.1.0
    DESCRIPTION "C++17 epoll based high-performance instant messaging system"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

message(STATUS "LiteIM Step 0 reset scaffold: implementation targets start from Step 1")
