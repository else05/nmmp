cmake_minimum_required(VERSION 3.6)
project(libvm)


include_directories(..)
include_directories(./include)


set(VM_SRC_FILES
        DexCatch.cpp
        Exception.cpp
        Interp.cpp
        InterpC-portable.cpp
        GlobalCache.cpp
        Codec.cpp
        Demand.cpp
        Module.cpp
        VmInit.c
        NativeVm.c
        VmCodec.cpp
        VmBinding.cpp
        Arm64Syscall.cpp
        ApkV2Signer.cpp
        Sha256.cpp
        JNIWrapper.c

        )


set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DLOG_INSTR ")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fvisibility=hidden -DNDEBUG")

#设置vm库名称
set(LIBNMMVM_NAME "nmmvm" CACHE INTERNAL "lib nmmvm name")


add_library(${LIBNMMVM_NAME} STATIC ${VM_SRC_FILES})
if (NMMP_PRIVATE_LINKER)
    target_compile_definitions(${LIBNMMVM_NAME} PRIVATE NMMP_PRIVATE_LINKER=1)
    target_compile_options(${LIBNMMVM_NAME} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti>")
endif ()

if (NMMP_OMVLL_ENABLED)
    target_compile_options(${LIBNMMVM_NAME}
            PRIVATE "-fpass-plugin=${NMMP_OMVLL_PLUGIN}")
endif ()

target_link_libraries(${LIBNMMVM_NAME} log)

target_include_directories(${LIBNMMVM_NAME} PUBLIC include)
set_target_properties(${LIBNMMVM_NAME} PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES)
