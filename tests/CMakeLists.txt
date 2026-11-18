cmake_minimum_required(VERSION 3.22)
project(MakosizerXDSPTests LANGUAGES CXX)

enable_testing()

add_executable(dsp_tests dsp_tests.cpp ../DSP/MakosizerXDSP.cpp)
target_compile_features(dsp_tests PRIVATE cxx_std_17)
if(NOT MSVC)
    target_compile_options(dsp_tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME dsp_tests COMMAND dsp_tests)
