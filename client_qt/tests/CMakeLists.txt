add_executable(liteim_qt_client_tests
    qt_client_test_main.cpp
    qt_client_test.cpp
)

target_compile_features(liteim_qt_client_tests PRIVATE cxx_std_17)
set_target_properties(liteim_qt_client_tests
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/client_qt
)

target_include_directories(liteim_qt_client_tests
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../include
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(liteim_qt_client_tests
    PRIVATE
        liteim_qt_client_core
        Qt${QT_VERSION_MAJOR}::Test
        GTest::gtest
)

liteim_qt_enable_warnings(liteim_qt_client_tests)

get_target_property(LITEIM_QT_CORE_LIBRARY Qt${QT_VERSION_MAJOR}::Core IMPORTED_LOCATION_RELEASE)
if(NOT LITEIM_QT_CORE_LIBRARY)
    get_target_property(LITEIM_QT_CORE_LIBRARY Qt${QT_VERSION_MAJOR}::Core IMPORTED_LOCATION)
endif()

set(LITEIM_QT_TEST_LD_LIBRARY_PATH "/usr/lib/x86_64-linux-gnu")
if(LITEIM_QT_CORE_LIBRARY)
    get_filename_component(LITEIM_QT_LIBRARY_DIR "${LITEIM_QT_CORE_LIBRARY}" DIRECTORY)
    string(APPEND LITEIM_QT_TEST_LD_LIBRARY_PATH ":${LITEIM_QT_LIBRARY_DIR}")
endif()
set(LITEIM_QT_TEST_ENVIRONMENT
    "LD_LIBRARY_PATH=${LITEIM_QT_TEST_LD_LIBRARY_PATH}:$ENV{LD_LIBRARY_PATH};QT_QPA_PLATFORM=offscreen"
)

function(liteim_add_qt_client_step_test name filter)
    add_test(
        NAME ${name}
        COMMAND liteim_qt_client_tests "--gtest_filter=${filter}"
    )
    set_tests_properties(${name}
        PROPERTIES
            LABELS unit
            ENVIRONMENT "${LITEIM_QT_TEST_ENVIRONMENT}"
    )
endfunction()

liteim_add_qt_client_step_test(
    LiteIMQtClient.Step46
    "QtPacketCodecTest.*:QtClientSessionTest.*:QtTcpClientTest.*"
)
liteim_add_qt_client_step_test(
    LiteIMQtClient.Step47
    "QtLoginWindowTest.*:QtRegisterDialogTest.*:QtAuthControllerTest.*:QtClientAppTest.*"
)
liteim_add_qt_client_step_test(
    LiteIMQtClient.Step48
    "QtMainWindowTest.StartsWithThreeColumnChatLayout:QtMainWindowTest.SidebarButtonsSwitchMiddleArea:QtMainWindowTest.ResizeKeepsColumnsUsable"
)
liteim_add_qt_client_step_test(
    LiteIMQtClient.Step49
    "QtConversationModelTest.*:QtContactListWidgetTest.*:QtMainWindowStep49Test.*"
)
liteim_add_qt_client_step_test(
    LiteIMQtClient.Step50
    "QtChatPageTest.*:QtChatInputBarTest.*"
)
liteim_add_qt_client_step_test(
    LiteIMQtClient.Step51
    "QtChatControllerTest.*:QtMainWindowStep51Test.*"
)
liteim_add_qt_client_step_test(
    LiteIMQtClient.Step52
    "QtClientRuntimeStep52Test.*:QtMainWindowStep52Test.*:QtLoginWindowStep52Test.*"
)
