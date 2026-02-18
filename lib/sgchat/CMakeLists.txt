# Project
cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(libsgchat LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)

# Ccache
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()

# Library
add_library(sgchat ${LIBRARY_LINKING_TYPE}
  src/sgchat.cpp
  src/sgchat.h
)
install(TARGETS sgchat DESTINATION ${CMAKE_INSTALL_LIBDIR})

# Common
target_common_config(sgchat)

# Dependency libraries
add_subdirectory(go)
add_dependencies(sgchat ref-cgosg)

# Platform specifics
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  add_compile_definitions(_XOPEN_SOURCE_EXTENDED)
  FIND_LIBRARY(CARBON_LIBRARY CoreFoundation)
  FIND_LIBRARY(CARBON_LIBRARY Security)
  target_link_libraries(sgchat PUBLIC "-framework CoreFoundation" "-framework Security")
endif()

# Headers
target_include_directories(sgchat PRIVATE "../common/src")
target_include_directories(sgchat PRIVATE "../ncutil/src")

# Compiler flags
set_target_properties(sgchat PROPERTIES COMPILE_FLAGS
                      "-Wall -Wextra -Wpedantic -Wshadow -Wpointer-arith \
                       -Wcast-qual -Wno-missing-braces -Wswitch-default \
                       -Wunreachable-code -Wundef -Wuninitialized \
                       -Wcast-align")

# Linking
message(STATUS "Go libraries: ${GO_LIBRARIES}")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  target_link_libraries(sgchat PUBLIC ref-cgosg ncutil ${GO_LIBRARIES}
    -Wl,-w -Wl,-force_load,${LIBSIGNAL_FFI_PATH}
    -lm -lz)
else()
  target_link_libraries(sgchat PUBLIC ref-cgosg ncutil ${GO_LIBRARIES}
    -Wl,--whole-archive ${LIBSIGNAL_FFI_PATH} -Wl,--no-whole-archive
    -ldl -lm -lz)
endif()
