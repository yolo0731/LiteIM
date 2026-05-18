find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

set(CMAKE_AUTORCC ON)

add_executable(liteim_qt_client
    src/main.cpp
    src/MainWindow.cpp
    resources/liteim_client.qrc
)

target_compile_features(liteim_qt_client PRIVATE cxx_std_17)

target_include_directories(liteim_qt_client
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(liteim_qt_client
    PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(liteim_qt_client PRIVATE -Wall -Wextra -Wpedantic)
endif()
