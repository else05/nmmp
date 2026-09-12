cmake_minimum_required(VERSION 3.6)
project(vm_codec_vector_test)

enable_testing()

add_executable(vm_codec_vector_test
        VmCodecVectorTest.cpp
        ../../main/cpp/vm/VmCodec.cpp)
target_compile_features(vm_codec_vector_test PRIVATE cxx_std_11)
target_include_directories(vm_codec_vector_test PRIVATE
        ../../main/cpp/vm/include)

add_test(NAME vm_codec_vector_test COMMAND vm_codec_vector_test)

add_executable(vm_reader_test VmReaderTest.cpp)
target_compile_features(vm_reader_test PRIVATE cxx_std_11)
target_include_directories(vm_reader_test PRIVATE ../../main/cpp/vm/include)
add_test(NAME vm_reader_test COMMAND vm_reader_test)

find_package(JNI QUIET)
if (JNI_FOUND AND NOT ANDROID)
    add_executable(vm_module_test VmModuleTest.cpp ../../main/cpp/vm/VmInit.c)
    target_compile_features(vm_module_test PRIVATE cxx_std_11)
    target_include_directories(vm_module_test PRIVATE ${JNI_INCLUDE_DIRS} ../../main/cpp/vm/include ../../main/cpp)
    target_link_libraries(vm_module_test pthread)
    add_test(NAME vm_module_test COMMAND vm_module_test)
endif ()
