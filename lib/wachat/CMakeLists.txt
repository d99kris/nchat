# Project
cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(libwachat LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)

# Ccache
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()

# Library
add_library(wachat SHARED
  src/wachat.cpp
  src/wachat.h
)
install(TARGETS wachat DESTINATION lib)

# Dependency libraries
add_subdirectory(go)
add_dependencies(wachat ref-cgowa)

# Platform specifics
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  add_compile_definitions(_XOPEN_SOURCE_EXTENDED)
  FIND_LIBRARY(CARBON_LIBRARY CoreFoundation)
  FIND_LIBRARY(CARBON_LIBRARY Security)
  target_link_libraries(wachat PUBLIC "-framework CoreFoundation" "-framework Security")
endif()

# Headers
target_include_directories(wachat PRIVATE "../common/src")
target_include_directories(wachat PRIVATE "../ncutil/src")

# Compiler flags
set_target_properties(wachat PROPERTIES COMPILE_FLAGS
                      "-Wall -Wextra -Wpedantic -Wshadow -Wpointer-arith \
                       -Wcast-qual -Wno-missing-braces -Wswitch-default \
                       -Wunreachable-code -Wundef -Wuninitialized \
                       -Wcast-align")
target_compile_definitions(wachat PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")

# Linking
target_link_libraries(wachat PUBLIC ref-cgowa ncutil)
