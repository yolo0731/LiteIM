add_executable(liteim_tests
    test_protocol.cpp
)

target_link_libraries(liteim_tests PRIVATE
    liteim_protocol
)

target_compile_options(liteim_tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_test(NAME liteim_protocol_tests COMMAND liteim_tests)
