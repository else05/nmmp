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
        Module.cpp
        VmInit.c
        NativeVm.c
        VmCodec.cpp
        VmBinding.cpp
        Arm64Syscall.cpp
        ApkV2Signer.cpp
        Sha256.cpp
        ProtectionManifest.cpp
        ProtectionPolicy.cpp
        ProcessMaps.cpp
        ModuleOrigins.cpp
        NativeIntegrity.cpp
        OuterIntegrity.cpp
        OuterImports.cpp
        ArtMethodChecks.cpp
        ArtRuntimeIntegrity.cpp
        EnvironmentChecks.cpp
        JavaEnvironmentChecks.cpp
        ArtifactInventory.cpp
        ArtifactApk.cpp
        ArtifactSignature.cpp
        JNIWrapper.c

        )



set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fvisibility=hidden -DNDEBUG")

#设置vm库名称
set(LIBNMMVM_NAME "nmmvm" CACHE INTERNAL "lib nmmvm name")


add_library(${LIBNMMVM_NAME} STATIC ${VM_SRC_FILES})
set(NMMP_CRYPTO_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../loader/vendor/monocypher")
target_sources(${LIBNMMVM_NAME} PRIVATE "${NMMP_CRYPTO_SOURCE}/monocypher.c" "${NMMP_CRYPTO_SOURCE}/monocypher-ed25519.c")
target_include_directories(${LIBNMMVM_NAME} PRIVATE "${NMMP_CRYPTO_SOURCE}")
target_compile_definitions(${LIBNMMVM_NAME} PRIVATE NMMP_PRIVATE_LINKER=1)
if (NMMP_DIAGNOSTICS)
    target_compile_definitions(${LIBNMMVM_NAME} PRIVATE NMMP_DIAGNOSTICS=1)
endif ()
target_compile_options(${LIBNMMVM_NAME} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti>")

target_link_libraries(${LIBNMMVM_NAME} log z dl)

target_include_directories(${LIBNMMVM_NAME} PUBLIC include)
set_target_properties(${LIBNMMVM_NAME} PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES)
