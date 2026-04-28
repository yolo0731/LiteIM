add_executable(liteim_server
    main.cpp
)

target_compile_options(liteim_server PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

