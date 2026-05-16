add_library(liteim_client_cli STATIC
    ClientCli.cpp
)

target_compile_features(liteim_client_cli PUBLIC cxx_std_17)

target_include_directories(liteim_client_cli
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(liteim_client_cli
    PUBLIC
        liteim_base
        liteim_net
        liteim_protocol
)

target_compile_options(liteim_client_cli PRIVATE -Wall -Wextra -Wpedantic)

add_executable(liteim_cli
    main.cpp
)

target_compile_features(liteim_cli PRIVATE cxx_std_17)

target_link_libraries(liteim_cli
    PRIVATE
        liteim_client_cli
)

target_compile_options(liteim_cli PRIVATE -Wall -Wextra -Wpedantic)
