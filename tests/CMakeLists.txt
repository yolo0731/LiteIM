add_executable(liteim_tests
    test_main.cpp
    base/config_test.cpp
    base/error_code_test.cpp
    base/logger_test.cpp
    base/timestamp_test.cpp
    net/buffer_test.cpp
    net/unique_fd_test.cpp
    net/channel_header_test.cpp
    net/channel_test.cpp
    net/epoller_header_test.cpp
    net/epoller_test.cpp
    net/event_loop_header_test.cpp
    net/event_loop_test.cpp
    net/event_loop_thread_header_test.cpp
    net/event_loop_thread_pool_header_test.cpp
    net/event_loop_thread_test.cpp
    net/event_loop_thread_pool_test.cpp
    net/acceptor_header_test.cpp
    net/acceptor_test.cpp
    net/session_header_test.cpp
    net/session_test.cpp
    net/signal_watcher_header_test.cpp
    net/signal_watcher_test.cpp
    net/socket_util_test.cpp
    net/tcp_server_header_test.cpp
    net/tcp_server_test.cpp
    concurrency/thread_pool_header_test.cpp
    concurrency/thread_pool_test.cpp
    storage/storage_interface_test.cpp
    storage/mysql_connection_test.cpp
    storage/mysql_pool_test.cpp
    storage/user_dao_test.cpp
    storage/message_dao_test.cpp
    cache/cache_interface_test.cpp
    timer/timer_heap_header_test.cpp
    timer/timer_heap_test.cpp
    timer/timer_manager_header_test.cpp
    timer/timer_manager_test.cpp
    protocol/frame_decoder_test.cpp
    protocol/byte_order_test.cpp
    protocol/message_type_test.cpp
    protocol/packet_test.cpp
    protocol/tlv_codec_test.cpp
    protocol/tlv_type_test.cpp
)

target_compile_features(liteim_tests PRIVATE cxx_std_17)
target_link_libraries(liteim_tests PRIVATE liteim_base liteim_net liteim_protocol liteim_concurrency liteim_storage liteim_cache GTest::gtest_main)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(liteim_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

gtest_discover_tests(liteim_tests)

add_test(
    NAME LiteIMServerSignalTest.TerminatesOnSigterm
    COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/server_signal_shutdown_test.sh $<TARGET_FILE:liteim_server>
)
