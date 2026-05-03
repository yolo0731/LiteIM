add_library(liteim_protocol
    protocol/FrameDecoder.cpp
    protocol/Packet.cpp
)

target_include_directories(liteim_protocol PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_compile_options(liteim_protocol PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_library(liteim_net
    net/Acceptor.cpp
    net/Buffer.cpp
    net/Channel.cpp
    net/Epoller.cpp
    net/EventLoop.cpp
    net/Session.cpp
    net/SocketUtil.cpp
    net/TcpServer.cpp
)

target_include_directories(liteim_net PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_compile_options(liteim_net PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
