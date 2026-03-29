cmake_minimum_required(VERSION 3.16)

if (WIN32 AND EXISTS "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.8/bin/nvcc.exe")
    set(CMAKE_CUDA_COMPILER "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.8/bin/nvcc.exe")
endif()
if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.18)
    set(CMAKE_CUDA_ARCHITECTURES "60;61;62;70;72;75;86;87" CACHE STRING "CUDA architectures to build for")
endif()

project(end2end LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

if (CMAKE_VERSION VERSION_LESS 3.17)
    set(CUDA_INCLUDE_PATHS ${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES})
    find_library(CUDA_RUNTIME_LIB cudart
        PATHS
        /usr/local/cuda/lib64
        /usr/local/cuda/targets/aarch64-linux/lib
        /usr/lib/aarch64-linux-gnu
        /usr/lib/x86_64-linux-gnu)
    if (NOT CUDA_RUNTIME_LIB)
        message(FATAL_ERROR "CUDA runtime library not found")
    endif()
else()
    find_package(CUDAToolkit REQUIRED)
    set(CUDA_RUNTIME_LIB CUDA::cudart)
    set(CUDA_INCLUDE_PATHS ${CUDAToolkit_INCLUDE_DIRS})
endif()
find_package(OpenCV REQUIRED)

if (WIN32)
    set(TensorRT_ROOT "${CMAKE_SOURCE_DIR}/3rdparty/TensorRT-8.5.2.2")
    set(TensorRT_INCLUDE_DIRS "${TensorRT_ROOT}/include")
    set(TensorRT_LIBRARY_DIR "${TensorRT_ROOT}/lib")
    set(SOCKET_LIBS ws2_32)
    set(TENSORRT_LINK_LIBS
        "${TensorRT_LIBRARY_DIR}/nvinfer.lib"
        "${TensorRT_LIBRARY_DIR}/nvinfer_plugin.lib")
else()
    find_path(TensorRT_INCLUDE_DIRS NvInfer.h
        PATHS
        /usr/include/aarch64-linux-gnu
        /usr/include
        /usr/local/include)
    find_library(TENSORRT_NVINFER nvinfer)
    find_library(TENSORRT_NVINFER_PLUGIN nvinfer_plugin)
    set(TENSORRT_LINK_LIBS ${TENSORRT_NVINFER} ${TENSORRT_NVINFER_PLUGIN})
endif()

if (NOT TensorRT_INCLUDE_DIRS)
    message(FATAL_ERROR "TensorRT include path not found")
endif()

set(END2END_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(END2END_APP_SOURCES
    src/main.cpp
    src/udp_service.cpp
    src/service_protocol.cpp)

add_executable(${PROJECT_NAME} ${END2END_APP_SOURCES})
target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${END2END_INCLUDE_DIR}
        ${OpenCV_INCLUDE_DIRS}
        ${CUDA_INCLUDE_PATHS}
        ${TensorRT_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        ${TENSORRT_LINK_LIBS}
        ${OpenCV_LIBS}
        ${SOCKET_LIBS}
        ${CUDA_RUNTIME_LIB})

add_executable(service_protocol_test
    tests/service_protocol_test.cpp
    src/service_protocol.cpp)

target_include_directories(service_protocol_test
    PRIVATE
        ${END2END_INCLUDE_DIR})

add_test(NAME service_protocol_test COMMAND service_protocol_test)

add_executable(udp_client_demo
    tests/udp_client_demo.cpp
    src/service_protocol.cpp)

target_include_directories(udp_client_demo
    PRIVATE
        ${END2END_INCLUDE_DIR})

target_link_libraries(udp_client_demo
    PRIVATE
        ${SOCKET_LIBS})
