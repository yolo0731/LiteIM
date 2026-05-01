cmake_minimum_required(VERSION 3.16)

project(LiteIM
    VERSION 0.1.0
    DESCRIPTION "A lightweight IM system built with C++17, epoll, Qt and SQLite"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LITEIM_BUILD_QT_CLIENT "Build the Qt Widgets client" OFF)

enable_testing()

add_subdirectory(src)
add_subdirectory(server)
add_subdirectory(tests)
add_subdirectory(client_qt)
