add_library(liteim_net
    Acceptor.cpp
    Buffer.cpp
    Channel.cpp
    EventLoop.cpp
    EventLoopThread.cpp
    EventLoopThreadPool.cpp
    Epoller.cpp
    Session.cpp
    SocketUtil.cpp
    TcpServer.cpp
    UniqueFd.cpp
)

target_compile_features(liteim_net PUBLIC cxx_std_17)
target_include_directories(liteim_net PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(liteim_net PUBLIC liteim_base liteim_protocol)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(liteim_net PRIVATE -Wall -Wextra -Wpedantic)
endif()
