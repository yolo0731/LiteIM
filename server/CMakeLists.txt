add_library(liteim_protocol
    protocol/Packet.cpp
)

target_include_directories(liteim_protocol PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_options(liteim_protocol PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_executable(liteim_server
    main.cpp
)

target_link_libraries(liteim_server PRIVATE
    liteim_protocol
)

target_compile_options(liteim_server PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
