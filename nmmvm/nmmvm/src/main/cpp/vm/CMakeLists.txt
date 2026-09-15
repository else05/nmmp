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
        EnvironmentChecks.cpp
        JavaEnvironmentChecks.cpp
        ArtifactInventory.cpp
        ArtifactApk.cpp
        ArtifactSignature.cpp
        JNIWrapper.c

        )


set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DLOG_INSTR ")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DNDEBUG")

#设置vm库名称
set(LIBNMMVM_NAME "nmmvm" CACHE INTERNAL "lib nmmvm name")


add_library(${LIBNMMVM_NAME} SHARED ${VM_SRC_FILES})
set(NMMP_CRYPTO_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../nmm-protect/mksrc/loader/vendor/monocypher")
target_sources(${LIBNMMVM_NAME} PRIVATE "${NMMP_CRYPTO_SOURCE}/monocypher.c" "${NMMP_CRYPTO_SOURCE}/monocypher-ed25519.c")
target_include_directories(${LIBNMMVM_NAME} PRIVATE "${NMMP_CRYPTO_SOURCE}")
if (NMMP_DIAGNOSTICS)
    target_compile_definitions(${LIBNMMVM_NAME} PRIVATE NMMP_DIAGNOSTICS=1)
endif ()

target_link_libraries(${LIBNMMVM_NAME} log z dl)
target_include_directories(${LIBNMMVM_NAME} PUBLIC include)
