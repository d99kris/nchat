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
if(HAS_COMBINED_GOLIB)
  # Combined WhatsApp+Signal c-archive built in lib/gostat (which also provides
  # LIBSIGNAL_FFI_PATH); the per-protocol Go library is not built here.
  set(SGCHAT_GOLIB ref-cgogostat)
else()
  add_subdirectory(go)
  set(SGCHAT_GOLIB ref-cgosg)
endif()
add_dependencies(sgchat ${SGCHAT_GOLIB})

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
  target_link_libraries(sgchat PUBLIC ${SGCHAT_GOLIB} ncutil ${GO_LIBRARIES}
    -Wl,-w -Wl,-force_load,${LIBSIGNAL_FFI_PATH}
    -lm -lz)
else()
  target_link_libraries(sgchat PUBLIC ${SGCHAT_GOLIB} ncutil ${GO_LIBRARIES}
    -Wl,--whole-archive ${LIBSIGNAL_FFI_PATH} -Wl,--no-whole-archive
    -ldl -lm -lz)
endif()
