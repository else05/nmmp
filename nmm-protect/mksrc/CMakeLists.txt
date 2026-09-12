cmake_minimum_required(VERSION 3.6)
project(nmmp)
option(NMMP_PRIVATE_LINKER "Privately load the encrypted ARM64 interpreter" OFF)

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

if (NMMP_PRIVATE_LINKER)
    include(loader/PrivateLinker.cmake)
    return()
endif ()

add_library(${LIBNAME_PLACEHOLDER} SHARED ConstantPool.c ${GEN_SOURCES} )

if (NMMP_OMVLL_ENABLED)
    target_compile_options(${LIBNAME_PLACEHOLDER}
            PRIVATE "-fpass-plugin=${NMMP_OMVLL_PLUGIN}")
endif ()

target_link_libraries(${LIBNAME_PLACEHOLDER} ${LIBNMMVM_NAME} log)
set_target_properties(${LIBNAME_PLACEHOLDER} PROPERTIES
        LINKER_LANGUAGE CXX
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES)

