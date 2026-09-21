cmake_minimum_required(VERSION 3.6)
project(vm_codec_vector_test)

enable_testing()

add_executable(vm_codec_vector_test
        VmCodecVectorTest.cpp
        ../../main/cpp/vm/VmCodec.cpp
        ../../main/cpp/vm/VmInit.c
        ../../main/cpp/vm/NativeVm.c)
if (NOT ANDROID)
    target_link_libraries(vm_codec_vector_test pthread)
endif()
target_compile_features(vm_codec_vector_test PRIVATE cxx_std_11)
target_include_directories(vm_codec_vector_test PRIVATE
        ../../main/cpp/vm/include)

add_test(NAME vm_codec_vector_test COMMAND vm_codec_vector_test)

set(NMMP_RESOLVER_FIXTURE "" CACHE PATH "Java-generated resolver string fixture directory")
if (NMMP_RESOLVER_FIXTURE)
    add_executable(resolver_strings_test ResolverStringsTest.c
            ../../main/cpp/vm/VmCodec.cpp ../../main/cpp/vm/VmInit.c
            ../../main/cpp/vm/NativeVm.c ../../main/cpp/vm/Sha256.cpp
            ../../main/cpp/vm/Interp.cpp ../../main/cpp/vm/InterpC-portable.cpp
            ../../main/cpp/vm/DexCatch.cpp ../../main/cpp/vm/Exception.cpp
            ../../main/cpp/vm/JNIWrapper.c)
    target_include_directories(resolver_strings_test PRIVATE "${NMMP_RESOLVER_FIXTURE}"
            ../../main/cpp ../../main/cpp/vm ../../main/cpp/vm/include)
    target_compile_features(resolver_strings_test PRIVATE cxx_std_11 c_std_99)
    if (NOT ANDROID)
        find_package(JNI REQUIRED)
        target_include_directories(resolver_strings_test PRIVATE ${JNI_INCLUDE_DIRS})
        target_link_libraries(resolver_strings_test pthread)
    else()
        target_link_libraries(resolver_strings_test log)
    endif()
    foreach(mode normal concurrent jni-1 jni-2 jni-3 jni-4 jni-5 oom-0 oom-1 oom-2 oom-3 oom-4
            array-ok array-class-fail array-new-fail array-invalid)
        add_test(NAME resolver_strings_${mode} COMMAND resolver_strings_test ${mode})
    endforeach()
endif()

add_executable(vm_reader_test VmReaderTest.cpp)
target_compile_features(vm_reader_test PRIVATE cxx_std_11)
target_compile_definitions(vm_reader_test PRIVATE NMMP_VM_READER_TEST=1)
target_include_directories(vm_reader_test PRIVATE ../../main/cpp/vm/include)
add_test(NAME vm_reader_test COMMAND vm_reader_test)

add_executable(protection_marker_test ProtectionMarkerTest.cpp
        ../../main/cpp/vm/ProtectionMarker.cpp
        ../../main/cpp/vm/Sha256.cpp)
target_compile_definitions(protection_marker_test PRIVATE NMMP_PROTECTION_MARKER_TEST=1)
target_compile_features(protection_marker_test PRIVATE cxx_std_11)
target_include_directories(protection_marker_test PRIVATE ../../main/cpp/vm/include)
add_test(NAME protection_marker_test COMMAND protection_marker_test)

add_executable(native_vm_test NativeVmTest.cpp ../../main/cpp/vm/NativeVm.c)
target_include_directories(native_vm_test PRIVATE ../../main/cpp/vm/include)
target_compile_features(native_vm_test PRIVATE cxx_std_11)
set(NMMP_NATIVE_VECTORS "" CACHE FILEPATH "Java/native program vector binary")
if (NMMP_NATIVE_VECTORS)
    add_test(NAME native_vm_test COMMAND native_vm_test "${NMMP_NATIVE_VECTORS}")
endif ()

set(NMMP_NATIVE_TEST_CONFIG "" CACHE PATH "Generated codec3 test configuration")
if (NMMP_NATIVE_TEST_CONFIG)
    add_executable(native_root_test NativeRootTest.cpp ../../main/cpp/vm/VmCodec.cpp
            ../../main/cpp/vm/NativeVm.c ../../main/cpp/vm/VmInit.c)
    target_include_directories(native_root_test PRIVATE "${NMMP_NATIVE_TEST_CONFIG}" ../../main/cpp/vm/include)
    target_compile_options(native_root_test PRIVATE -include "${NMMP_NATIVE_TEST_CONFIG}/VmCodecConfig.h")
    target_compile_features(native_root_test PRIVATE cxx_std_11)
    target_link_libraries(native_root_test pthread)
    if (NMMP_EXPECT_NATIVE_INIT_FAILURE)
        target_compile_definitions(native_root_test PRIVATE NMMP_EXPECT_NATIVE_INIT_FAILURE=1)
    endif ()
    add_test(NAME native_root_test COMMAND native_root_test)
endif ()

find_package(JNI QUIET)
if (JNI_FOUND AND NOT ANDROID)
    add_executable(vm_module_test VmModuleTest.cpp ../../main/cpp/vm/VmInit.c)
    target_compile_features(vm_module_test PRIVATE cxx_std_11)
    target_include_directories(vm_module_test PRIVATE ${JNI_INCLUDE_DIRS} ../../main/cpp/vm/include ../../main/cpp)
    target_link_libraries(vm_module_test pthread)
    add_test(NAME vm_module_test COMMAND vm_module_test)
    set(NMMP_GENERATED_INIT "" CACHE FILEPATH "Actual generated two-DEX jni_init.c")
    if (NMMP_GENERATED_INIT)
        add_executable(generated_init_test GeneratedInitTest.c ../../main/cpp/vm/VmInit.c)
        target_include_directories(generated_init_test PRIVATE ${JNI_INCLUDE_DIRS} ../../main/cpp/vm/include)
        target_compile_definitions(generated_init_test PRIVATE NMMP_GENERATED_INIT="${NMMP_GENERATED_INIT}")
        target_link_libraries(generated_init_test pthread)
        foreach(mode 0 1 2)
            add_test(NAME generated_init_${mode} COMMAND generated_init_test ${mode})
            set_tests_properties(generated_init_${mode} PROPERTIES TIMEOUT 15)
        endforeach()
    endif ()
endif ()
