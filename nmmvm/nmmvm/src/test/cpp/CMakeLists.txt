cmake_minimum_required(VERSION 3.6)
project(vm_codec_vector_test)

enable_testing()

add_executable(vm_codec_vector_test
        VmCodecVectorTest.cpp
        ../../main/cpp/vm/VmCodec.cpp
        ../../main/cpp/vm/VmInit.c)
target_link_libraries(vm_codec_vector_test pthread)
target_compile_features(vm_codec_vector_test PRIVATE cxx_std_11)
target_include_directories(vm_codec_vector_test PRIVATE
        ../../main/cpp/vm/include)

add_test(NAME vm_codec_vector_test COMMAND vm_codec_vector_test)

add_executable(vm_reader_test VmReaderTest.cpp)
target_compile_features(vm_reader_test PRIVATE cxx_std_11)
target_include_directories(vm_reader_test PRIVATE ../../main/cpp/vm/include)
add_test(NAME vm_reader_test COMMAND vm_reader_test)

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
