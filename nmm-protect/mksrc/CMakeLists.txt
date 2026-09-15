cmake_minimum_required(VERSION 3.6)
project(nmmp)
if (NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "NMMP requires arm64-v8a")
endif ()
message(STATUS "NMMP: on-demand-v1, private linker and stage0 VM")
option(NMMP_DIAGNOSTICS "Include detailed NMMP loader diagnostics" OFF)

add_subdirectory(vm)


include_directories(.)
include_directories(generated/)


file(GLOB GEN_SOURCES generated/*_functions.c generated/jni_init.c)


set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -fvisibility=hidden ")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fvisibility=hidden ")

#外部程序处理时替换 LIBNAME_PLACEHOLDER
set(LIBNAME_PLACEHOLDER "nmmp")

include(loader/PrivateLinker.cmake)

