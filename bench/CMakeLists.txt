find_package(Threads REQUIRED)

add_library(liteim_bench_core STATIC
    Benchmark.cpp
)

target_compile_features(liteim_bench_core PUBLIC cxx_std_17)

target_include_directories(liteim_bench_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(liteim_bench_core
    PUBLIC
        liteim_base
        liteim_protocol
        liteim_client_cli
        Threads::Threads
)

target_compile_options(liteim_bench_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(liteim_bench
    liteim_bench.cpp
)

target_compile_features(liteim_bench PRIVATE cxx_std_17)

target_link_libraries(liteim_bench
    PRIVATE
        liteim_bench_core
)

target_compile_options(liteim_bench PRIVATE -Wall -Wextra -Wpedantic)
