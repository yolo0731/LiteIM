add_executable(liteim_tests
    test_acceptor.cpp
    test_buffer.cpp
    test_channel.cpp
    test_epoller.cpp
    test_event_loop.cpp
    test_frame_decoder.cpp
    test_main.cpp
    test_protocol.cpp
    test_reactor_interfaces.cpp
    test_socket_util.cpp
)

target_link_libraries(liteim_tests PRIVATE
    liteim_net
    liteim_protocol
)

target_compile_options(liteim_tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_test(NAME liteim_tests COMMAND liteim_tests)
