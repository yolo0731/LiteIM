add_library(liteim_service STATIC
    MessageRouter.cpp
    OnlineService.cpp
    SessionManager.cpp
)

target_compile_features(liteim_service PUBLIC cxx_std_17)

target_include_directories(liteim_service
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(liteim_service
    PUBLIC
        liteim_base
        liteim_net
        liteim_cache
        liteim_concurrency
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(liteim_service PRIVATE -Wall -Wextra -Wpedantic)
endif()
