# 查找 Qt Widgets,优先 Qt6，如果没有找到 Qt6 就找 Qt5
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network Test)

# 开启 qrc 资源自动处理
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_library(liteim_qt_client_core STATIC
    include/liteim_client/ClientSession.hpp
    include/liteim_client/MainWindow.hpp
    include/liteim_client/PacketCodec.hpp
    include/liteim_client/TcpClient.hpp
    src/ClientSession.cpp
    src/MainWindow.cpp
    src/PacketCodec.cpp
    src/TcpClient.cpp
)

target_compile_features(liteim_qt_client_core PUBLIC cxx_std_17)

target_include_directories(liteim_qt_client_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/include
)

# 链接 Qt Widgets
target_link_libraries(liteim_qt_client_core
    PUBLIC
        liteim_base
        liteim_protocol
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Network
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(liteim_qt_client_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(liteim_qt_client
    src/main.cpp
    resources/liteim_client.qrc
)

target_compile_features(liteim_qt_client PRIVATE cxx_std_17)

target_link_libraries(liteim_qt_client
    PRIVATE
        liteim_qt_client_core
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(liteim_qt_client PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(liteim_qt_client_tests
    tests/qt_client_test_main.cpp
    tests/qt_client_test.cpp
)

target_compile_features(liteim_qt_client_tests PRIVATE cxx_std_17)

target_include_directories(liteim_qt_client_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(liteim_qt_client_tests
    PRIVATE
        liteim_qt_client_core
        Qt${QT_VERSION_MAJOR}::Test
        GTest::gtest
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(liteim_qt_client_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME LiteIMQtClient.Step46 COMMAND liteim_qt_client_tests)
set_tests_properties(LiteIMQtClient.Step46 PROPERTIES LABELS unit)

get_target_property(LITEIM_QT_CORE_LIBRARY Qt${QT_VERSION_MAJOR}::Core IMPORTED_LOCATION_RELEASE)
if(NOT LITEIM_QT_CORE_LIBRARY)
    get_target_property(LITEIM_QT_CORE_LIBRARY Qt${QT_VERSION_MAJOR}::Core IMPORTED_LOCATION)
endif()

set(LITEIM_QT_TEST_LD_LIBRARY_PATH "/usr/lib/x86_64-linux-gnu")
if(LITEIM_QT_CORE_LIBRARY)
    get_filename_component(LITEIM_QT_LIBRARY_DIR "${LITEIM_QT_CORE_LIBRARY}" DIRECTORY)
    string(APPEND LITEIM_QT_TEST_LD_LIBRARY_PATH ":${LITEIM_QT_LIBRARY_DIR}")
endif()

set_tests_properties(LiteIMQtClient.Step46
    PROPERTIES
        ENVIRONMENT "LD_LIBRARY_PATH=${LITEIM_QT_TEST_LD_LIBRARY_PATH}:$ENV{LD_LIBRARY_PATH}"
)
