add_library(liteim_net
    Buffer.cpp
    Channel.cpp
    EventLoop.cpp
    Epoller.cpp
    SocketUtil.cpp
)

target_compile_features(liteim_net PUBLIC cxx_std_17)
target_include_directories(liteim_net PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(liteim_net PUBLIC liteim_base)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(liteim_net PRIVATE -Wall -Wextra -Wpedantic)
endif()
