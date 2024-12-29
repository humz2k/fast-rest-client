cmake_minimum_required(VERSION 3.21)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

project(FASTRESTCLIENT)

find_package(OpenSSL REQUIRED)

add_library(fastrest INTERFACE)

target_link_libraries(fastrest INTERFACE OpenSSL::SSL)
target_include_directories(fastrest INTERFACE include)

if (${PROJECT_IS_TOP_LEVEL})
    file( GLOB DRIVER_SOURCES examples/*.cpp )
    foreach( sourcefile ${DRIVER_SOURCES} )
        get_filename_component( name ${sourcefile} NAME_WE )
        add_executable( ${name} ${sourcefile} )
        target_link_libraries( ${name} fastrest )
    endforeach( sourcefile ${DRIVER_SOURCES} )
endif()