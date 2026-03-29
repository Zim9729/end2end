cmake_minimum_required(VERSION 3.16)
project(caller_client_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

set(CALLER_CLIENT_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(caller_client_lib
    src/client.cpp
    src/file_utils.cpp
    src/protocol.cpp)

target_include_directories(caller_client_lib
    PUBLIC
        ${CALLER_CLIENT_INCLUDE_DIR})

if(WIN32)
    target_link_libraries(caller_client_lib
        PUBLIC
            ws2_32)
endif()

add_executable(caller_client_demo
    src/main.cpp)

target_link_libraries(caller_client_demo
    PRIVATE
        caller_client_lib)

add_executable(protocol_test
    tests/protocol_test.cpp)

target_link_libraries(protocol_test
    PRIVATE
        caller_client_lib)

add_test(NAME protocol_test COMMAND protocol_test)
