add_library(liteim_cache INTERFACE)

target_compile_features(liteim_cache INTERFACE cxx_std_17)

target_include_directories(liteim_cache
    INTERFACE
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(liteim_cache
    INTERFACE
        liteim_base
        liteim_storage
)
