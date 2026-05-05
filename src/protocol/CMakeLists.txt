add_library(liteim_protocol
    FrameDecoder.cpp
    MessageType.cpp
    Packet.cpp
    Tlv.cpp
    TlvCodec.cpp
)

target_compile_features(liteim_protocol PUBLIC cxx_std_17)

target_include_directories(liteim_protocol
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(liteim_protocol
    PUBLIC
        liteim_base
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(liteim_protocol PRIVATE -Wall -Wextra -Wpedantic)
endif()
