add_library(liteim_storage INTERFACE)

target_compile_features(liteim_storage INTERFACE cxx_std_17)

target_include_directories(liteim_storage
    INTERFACE
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(liteim_storage
    INTERFACE
        liteim_base
)
