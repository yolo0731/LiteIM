add_executable(liteim_tests
    test_main.cpp
    base/config_test.cpp
    base/error_code_test.cpp
    base/logger_test.cpp
    base/timestamp_test.cpp
    net/buffer_test.cpp
    net/channel_header_test.cpp
    net/epoller_header_test.cpp
    net/epoller_test.cpp
    net/event_loop_header_test.cpp
    net/socket_util_test.cpp
    protocol/frame_decoder_test.cpp
    protocol/message_type_test.cpp
    protocol/packet_test.cpp
    protocol/tlv_codec_test.cpp
    protocol/tlv_type_test.cpp
)

target_compile_features(liteim_tests PRIVATE cxx_std_17)
target_link_libraries(liteim_tests PRIVATE liteim_base liteim_net liteim_protocol GTest::gtest_main)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(liteim_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

gtest_discover_tests(liteim_tests)
