add_library(liteim_protocol
    MessageType.cpp
    Tlv.cpp
)

target_compile_features(liteim_protocol PUBLIC cxx_std_17)

target_include_directories(liteim_protocol
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(liteim_protocol PRIVATE -Wall -Wextra -Wpedantic)
endif()
