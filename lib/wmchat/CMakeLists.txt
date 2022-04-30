# Project
cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(libwmchat LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)

# Ccache
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()

# Library
add_library(wmchat SHARED
  src/wmchat.cpp
  src/wmchat.h
)
install(TARGETS wmchat DESTINATION lib)

# Dependency libraries
add_subdirectory(go)
add_dependencies(wmchat ref-cgowm)

# Platform specifics
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  add_compile_definitions(_XOPEN_SOURCE_EXTENDED)
  FIND_LIBRARY(CARBON_LIBRARY CoreFoundation)
  FIND_LIBRARY(CARBON_LIBRARY Security)
  target_link_libraries(wmchat PUBLIC "-framework CoreFoundation" "-framework Security")
endif()

# Headers
target_include_directories(wmchat PRIVATE "../common/src")
target_include_directories(wmchat PRIVATE "../ncutil/src")

# Compiler flags
set_target_properties(wmchat PROPERTIES COMPILE_FLAGS
                      "-Wall -Wextra -Wpedantic -Wshadow -Wpointer-arith \
                       -Wcast-qual -Wno-missing-braces -Wswitch-default \
                       -Wunreachable-code -Wundef -Wuninitialized \
                       -Wcast-align")
target_compile_definitions(wmchat PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")

# Linking
target_link_libraries(wmchat PUBLIC ref-cgowm ncutil)
