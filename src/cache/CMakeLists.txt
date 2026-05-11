find_package(PkgConfig REQUIRED)
pkg_check_modules(HIREDIS REQUIRED IMPORTED_TARGET hiredis)

add_library(liteim_cache STATIC
    LoginRateLimiter.cpp
    OnlineStatusCache.cpp
    RedisCache.cpp
    RedisClient.cpp
    RedisPool.cpp
    UnreadCounter.cpp
)

target_compile_features(liteim_cache PUBLIC cxx_std_17)

target_include_directories(liteim_cache
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(liteim_cache
    PUBLIC
        liteim_base
        liteim_storage
        PkgConfig::HIREDIS
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(liteim_cache PRIVATE -Wall -Wextra -Wpedantic)
endif()
