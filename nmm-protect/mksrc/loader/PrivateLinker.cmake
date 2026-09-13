if (NOT ANDROID_ABI STREQUAL "arm64-v8a" OR ANDROID_PLATFORM_LEVEL LESS 26)
    message(FATAL_ERROR "NMMP private loader requires ARM64 and an API26+ build target (runtime API26/27)")
endif ()
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
find_package(Java 17 REQUIRED COMPONENTS Runtime)
set(PRIVATE_DIR "${CMAKE_CURRENT_BINARY_DIR}/private")
file(MAKE_DIRECTORY "${PRIVATE_DIR}")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/loader/pack.py"
        configure "${PRIVATE_DIR}" RESULT_VARIABLE CONFIGURE_RESULT)
if (NOT CONFIGURE_RESULT EQUAL 0)
    message(FATAL_ERROR "NMMP private loader build ID generation failed")
endif ()

add_library(nmmp_inner SHARED ConstantPool.c ${GEN_SOURCES} loader/InnerBootstrap.c)
target_include_directories(nmmp_inner PRIVATE loader "${PRIVATE_DIR}")
target_compile_definitions(nmmp_inner PRIVATE JNI_OnLoad=nmmp_inner_on_load NMMP_PRIVATE_LINKER=1)
target_compile_options(nmmp_inner PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti>")
if (NMMP_OMVLL_ENABLED)
    target_compile_options(nmmp_inner PRIVATE "-fpass-plugin=${NMMP_OMVLL_PLUGIN}")
endif ()
target_link_libraries(nmmp_inner ${LIBNMMVM_NAME} log)
set_target_properties(nmmp_inner PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${PRIVATE_DIR}" LINKER_LANGUAGE CXX
        C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES
        LINK_FLAGS "-Wl,--pack-dyn-relocs=none,--hash-style=sysv,-Bsymbolic,--exclude-libs,ALL,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/loader/inner.exports")

set(PRIVATE_PAYLOAD "${PRIVATE_DIR}/Payload.c")
add_custom_command(OUTPUT "${PRIVATE_PAYLOAD}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/loader/pack.py" pack
                "$<TARGET_FILE:nmmp_inner>" "${PRIVATE_DIR}"
                --sysroot "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-android/26"
                --readelf "${CMAKE_READELF}" --java "${Java_JAVA_EXECUTABLE}"
        DEPENDS nmmp_inner loader/pack.py loader/stage0.py loader/native_formats.py loader/Seal.java "${PRIVATE_DIR}/build-id.txt"
        VERBATIM)
add_library(${LIBNAME_PLACEHOLDER} SHARED loader/Outer.c loader/Envelope.c loader/Loader.c
        loader/Once.c loader/vendor/monocypher/monocypher.c "${PRIVATE_PAYLOAD}")
target_include_directories(${LIBNAME_PLACEHOLDER} PRIVATE loader "${PRIVATE_DIR}")
target_sources(${LIBNAME_PLACEHOLDER} PRIVATE loader/Stage0.c vm/NativeVm.c)
target_include_directories(${LIBNAME_PLACEHOLDER} PRIVATE vm/include)
target_compile_options(${LIBNAME_PLACEHOLDER} PRIVATE -ffunction-sections -fdata-sections)
if (NMMP_DIAGNOSTICS)
    target_compile_definitions(${LIBNAME_PLACEHOLDER} PRIVATE NMMP_DIAGNOSTICS=1)
endif ()
target_link_libraries(${LIBNAME_PLACEHOLDER} log dl z)
set_target_properties(${LIBNAME_PLACEHOLDER} PROPERTIES C_VISIBILITY_PRESET hidden
        LINK_FLAGS "-Wl,--gc-sections,--exclude-libs,ALL,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/loader/outer.exports")
