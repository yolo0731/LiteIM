find_package(PkgConfig REQUIRED)
pkg_check_modules(MYSQLCLIENT REQUIRED IMPORTED_TARGET mysqlclient)

add_library(liteim_storage STATIC
    MySqlConnection.cpp
    MySqlPool.cpp
)

target_compile_features(liteim_storage PUBLIC cxx_std_17)

target_include_directories(liteim_storage
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(liteim_storage
    PUBLIC
        liteim_base
        PkgConfig::MYSQLCLIENT
)
