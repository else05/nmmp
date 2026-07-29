cmake_minimum_required(VERSION 3.6)
project(vm_codec_vector_test)

enable_testing()

add_executable(vm_codec_vector_test VmCodecVectorTest.cpp)
target_compile_features(vm_codec_vector_test PRIVATE cxx_std_11)

add_test(NAME vm_codec_vector_test COMMAND vm_codec_vector_test)
