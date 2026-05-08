add_library(liteim_concurrency
    ThreadPool.cpp
)

target_compile_features(liteim_concurrency PUBLIC cxx_std_17)

target_include_directories(liteim_concurrency
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(liteim_concurrency
    PUBLIC
        liteim_base
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(liteim_concurrency PRIVATE -Wall -Wextra -Wpedantic)
endif()
