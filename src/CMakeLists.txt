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

add_library(liteim_service
    service/MessageRouter.cpp
)

target_include_directories(liteim_service PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(liteim_service PUBLIC
    liteim_net
    liteim_protocol
)

target_compile_options(liteim_service PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

find_package(SQLite3 REQUIRED)

add_library(liteim_storage
    storage/NullCache.cpp
    storage/SQLiteStorage.cpp
)

target_include_directories(liteim_storage PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(liteim_storage PRIVATE
    SQLite::SQLite3
)

target_compile_options(liteim_storage PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
