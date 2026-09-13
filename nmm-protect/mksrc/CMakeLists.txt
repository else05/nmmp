cmake_minimum_required(VERSION 3.6)
project(nmmp)
if (DEFINED NMMP_VM_DECODE_MODE AND NOT NMMP_VM_DECODE_MODE STREQUAL "on-demand-v1")
    message(FATAL_ERROR "Only on-demand-v1 is supported; legacy has been removed")
endif ()
foreach(obsolete_switch NMMP_PRIVATE_LINKER NMMP_STAGE0_VM)
    if (DEFINED ${obsolete_switch} AND NOT ${obsolete_switch})
        message(FATAL_ERROR "${obsolete_switch} cannot be disabled")
    endif ()
endforeach()
if (NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "NMMP requires arm64-v8a")
endif ()
message(STATUS "NMMP: on-demand-v1, private linker and stage0 VM")
option(NMMP_DIAGNOSTICS "Include detailed NMMP loader diagnostics" OFF)

# 仅在外部明确传入插件路径时启用 O-MVLL，保持原构建流程不变。
set(NMMP_OMVLL_PLUGIN "" CACHE FILEPATH "O-MVLL plugin path")
set(NMMP_OMVLL_ENABLED OFF)

if (NMMP_OMVLL_PLUGIN)
    if (NOT EXISTS "${NMMP_OMVLL_PLUGIN}")
        message(FATAL_ERROR "O-MVLL 插件不存在: ${NMMP_OMVLL_PLUGIN}")
    endif ()
    if (NOT ANDROID_ABI MATCHES "^(arm64-v8a|armeabi-v7a)$")
        message(FATAL_ERROR "O-MVLL 不支持当前 ABI: ${ANDROID_ABI}")
    endif ()

    set(NMMP_OMVLL_ENABLED ON)
    message(STATUS "NMMP O-MVLL enabled: ${NMMP_OMVLL_PLUGIN}")
endif ()

add_subdirectory(vm)


include_directories(.)
include_directories(generated/)


file(GLOB GEN_SOURCES generated/*_functions.c generated/jni_init.c)


set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -fvisibility=hidden ")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fvisibility=hidden ")

#外部程序处理时替换 LIBNAME_PLACEHOLDER
set(LIBNAME_PLACEHOLDER "nmmp")

include(loader/PrivateLinker.cmake)

