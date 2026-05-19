add_library(liteim_qt_client_core STATIC
    ../include/liteim_client/app/ClientApp.hpp
    ../include/liteim_client/app/ClientRuntime.hpp
    ../include/liteim_client/auth/AuthController.hpp
    ../include/liteim_client/chat/ChatController.hpp
    ../include/liteim_client/model/ConversationModel.hpp
    ../include/liteim_client/network/ClientSession.hpp
    ../include/liteim_client/network/TcpClient.hpp
    ../include/liteim_client/protocol/PacketCodec.hpp
    ../include/liteim_client/ui/ChatInputBar.hpp
    ../include/liteim_client/ui/ChatPage.hpp
    ../include/liteim_client/ui/ContactListWidget.hpp
    ../include/liteim_client/ui/ConversationListWidget.hpp
    ../include/liteim_client/ui/LoginWindow.hpp
    ../include/liteim_client/ui/MainWindow.hpp
    ../include/liteim_client/ui/MessageBubble.hpp
    ../include/liteim_client/ui/RegisterDialog.hpp
    ../include/liteim_client/ui/SideBar.hpp
    app/ClientApp.cpp
    app/ClientRuntime.cpp
    auth/AuthController.cpp
    chat/ChatController.cpp
    model/ConversationModel.cpp
    network/ClientSession.cpp
    network/TcpClient.cpp
    protocol/PacketCodec.cpp
    ui/ChatInputBar.cpp
    ui/ChatPage.cpp
    ui/ContactListWidget.cpp
    ui/ConversationListWidget.cpp
    ui/LoginWindow.cpp
    ui/MainWindow.cpp
    ui/MessageBubble.cpp
    ui/RegisterDialog.cpp
    ui/SideBar.cpp
)

target_compile_features(liteim_qt_client_core PUBLIC cxx_std_17)

target_include_directories(liteim_qt_client_core
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/../include
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(liteim_qt_client_core
    PUBLIC
        liteim_base
        liteim_protocol
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Network
)

liteim_qt_enable_warnings(liteim_qt_client_core)

add_executable(liteim_qt_client
    main.cpp
    ../resources/liteim_client.qrc
)

target_compile_features(liteim_qt_client PRIVATE cxx_std_17)
set_target_properties(liteim_qt_client
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/client_qt
)

target_link_libraries(liteim_qt_client
    PRIVATE
        liteim_qt_client_core
)

liteim_qt_enable_warnings(liteim_qt_client)
