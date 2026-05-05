add_executable(liteim_server
    main.cpp
)

target_compile_features(liteim_server PRIVATE cxx_std_17)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(liteim_server PRIVATE -Wall -Wextra -Wpedantic)
endif()
