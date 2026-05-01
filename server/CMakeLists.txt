add_executable(liteim_server
    main.cpp
)

target_link_libraries(liteim_server PRIVATE
    liteim_net
    liteim_protocol
)

target_compile_options(liteim_server PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
