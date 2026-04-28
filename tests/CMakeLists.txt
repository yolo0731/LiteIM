add_executable(liteim_tests
    test_smoke.cpp
)

target_compile_options(liteim_tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_test(NAME liteim_smoke_tests COMMAND liteim_tests)
