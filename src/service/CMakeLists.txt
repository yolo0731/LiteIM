find_package(PkgConfig REQUIRED)
pkg_check_modules(OPENSSL REQUIRED IMPORTED_TARGET openssl)

add_library(liteim_service STATIC
    AuthService.cpp
    FriendService.cpp
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
        PkgConfig::OPENSSL
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(liteim_service PRIVATE -Wall -Wextra -Wpedantic)
endif()
