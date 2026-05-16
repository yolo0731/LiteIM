cmake_minimum_required(VERSION 3.16)

include(FetchContent)

project(LiteIM
    VERSION 0.1.0
    DESCRIPTION "C++17 epoll based high-performance instant messaging system"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LITEIM_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_library(liteim_sanitizer_flags INTERFACE)
if(LITEIM_ENABLE_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(liteim_sanitizer_flags
            INTERFACE
                -fsanitize=address,undefined
                -fno-omit-frame-pointer
                -fno-sanitize-recover=all
        )
        target_link_options(liteim_sanitizer_flags
            INTERFACE
                -fsanitize=address,undefined
                -fno-sanitize-recover=all
        )
    else()
        message(FATAL_ERROR "LITEIM_ENABLE_SANITIZERS requires GNU or Clang")
    endif()
endif()

enable_testing()
include(GoogleTest)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.13.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)
FetchContent_MakeAvailable(spdlog)

link_libraries(liteim_sanitizer_flags)

add_subdirectory(src)
add_subdirectory(server)
add_subdirectory(client_cli)
add_subdirectory(bench)
add_subdirectory(tests)
