add_executable(liteim_tests
    test_main.cpp
    base/config_test.cpp
    base/error_code_test.cpp
    base/logger_test.cpp
    base/timestamp_test.cpp
)

target_compile_features(liteim_tests PRIVATE cxx_std_17)
target_link_libraries(liteim_tests PRIVATE liteim_base GTest::gtest_main)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(liteim_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

gtest_discover_tests(liteim_tests)
