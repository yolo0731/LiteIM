if(LITEIM_BUILD_QT_CLIENT)
    message(STATUS "Qt client build is not implemented yet. It will be added after the server MVP.")
else()
    message(STATUS "Qt client is disabled. Configure with -DLITEIM_BUILD_QT_CLIENT=ON after Qt files are implemented.")
endif()

