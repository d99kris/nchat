cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(stb LANGUAGES C)

add_library(stb STATIC stb_impl.c)
set_target_properties(stb PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(stb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(stb PRIVATE -w)
