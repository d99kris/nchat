cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(qrcodegen LANGUAGES CXX)

add_library(qrcodegen STATIC qrcodegen.cpp)
set_target_properties(qrcodegen PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(qrcodegen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(qrcodegen PRIVATE -w)
