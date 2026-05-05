add_library(liteim_base
    Config.cpp
    ErrorCode.cpp
    Logger.cpp
    Status.cpp
    Timestamp.cpp
)

target_compile_features(liteim_base PUBLIC cxx_std_17)

target_include_directories(liteim_base
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(liteim_base
    PUBLIC
        spdlog::spdlog_header_only
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(liteim_base PRIVATE -Wall -Wextra -Wpedantic)
endif()
