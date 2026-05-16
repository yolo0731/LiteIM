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
    storage/friend_group_dao_test.cpp
    storage/mysql_storage_test.cpp
    cache/cache_interface_test.cpp
    cache/redis_client_pool_test.cpp
    cache/online_status_cache_test.cpp
    cache/unread_login_cache_test.cpp
    cache/redis_cache_test.cpp
    service/session_manager_test.cpp
    service/online_service_test.cpp
    service/message_router_test.cpp
    service/auth_service_test.cpp
    service/friend_service_test.cpp
    service/chat_service_test.cpp
    service/group_service_test.cpp
    service/offline_message_service_test.cpp
    service/history_service_test.cpp
    service/heartbeat_service_test.cpp
    client_cli/cli_protocol_test.cpp
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
target_link_libraries(liteim_tests PRIVATE liteim_base liteim_net liteim_protocol liteim_concurrency liteim_storage liteim_cache liteim_service liteim_client_cli GTest::gtest_main)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(liteim_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

gtest_discover_tests(liteim_tests)

add_test(
    NAME LiteIMServerSignalTest.TerminatesOnSigterm
    COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/server_signal_shutdown_test.sh $<TARGET_FILE:liteim_server>
)
set_tests_properties(LiteIMServerSignalTest.TerminatesOnSigterm PROPERTIES SKIP_RETURN_CODE 77)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    set(LITEIM_E2E_TESTS
        test_auth
        test_private_chat
        test_group_chat
        test_offline
        test_heartbeat
        test_backpressure
    )

    foreach(e2e_test IN LISTS LITEIM_E2E_TESTS)
        add_test(
            NAME LiteIME2E.${e2e_test}
            COMMAND ${Python3_EXECUTABLE} -m unittest tests.e2e.${e2e_test}
        )
        set_tests_properties(LiteIME2E.${e2e_test}
            PROPERTIES
                WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                ENVIRONMENT "PYTHONPATH=${PROJECT_SOURCE_DIR};LITEIM_SERVER_BIN=$<TARGET_FILE:liteim_server>"
                RESOURCE_LOCK LiteIME2EServer
                TIMEOUT 45
        )
    endforeach()
endif()
