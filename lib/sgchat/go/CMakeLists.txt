# Project
cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(libcgosg)

# Check Golang version
execute_process(COMMAND bash "-c" "go version | cut -c14- | cut -d' ' -f1 | tr -d '\n'" OUTPUT_VARIABLE GO_VERSION)
message(STATUS "Go version ${GO_VERSION}.")
set(CUSTOM_GO_FLAGS -modcacherw -buildvcs=false)

# Detached debug symbols: when the dist/release build splits nchat's own DWARF
# into a detached nchat.debug / .dSYM (HAS_DEBUG_SYMBOLS, top-level CMakeLists),
# omit this c-archive's Go DWARF with -ldflags=-w. A c-archive linked into
# nchat would otherwise fold signalmeow's Go DWARF into the detached symbols,
# dwarfing nchat's own frames. -w drops the DWARF only (the archive's symbol
# table, needed by the final link, stays), so codegen and linking are unaffected.
# (Signal is not enabled in the current dist builds, but keep this in sync with
# lib/wmchat/go so a future Signal dist build inherits the same treatment.)
if(HAS_DEBUG_SYMBOLS)
  list(APPEND CUSTOM_GO_FLAGS -ldflags=-w)
endif()

# Check Go package
execute_process(COMMAND bash "-c" "go version | grep -v -q gccgo" RESULT_VARIABLE GO_GCC)
if (GO_GCC EQUAL "1")
  message(STATUS "Go gccgo.")
  set(GO_LIBRARIES "go" PARENT_SCOPE)
else ()
  message(STATUS "Go golang.")
  set(GO_LIBRARIES "" PARENT_SCOPE)
endif ()

# Config params
if (HAS_STATICGOLIB)
  set(BUILDMODE c-archive)
  set(OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
  set(LIBSUFFIX ${CMAKE_STATIC_LIBRARY_SUFFIX})
else()
  set(BUILDMODE c-shared)
  set(OUTPUT_DIR ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
  set(LIBSUFFIX ${CMAKE_SHARED_LIBRARY_SUFFIX})
endif()

# Build Go library / C archive
set(TARGET cgosg)
set(GOPATH ${OUTPUT_DIR})
set(SRCS gosg.go cgosg.go go.mod go.sum)
file(GLOB_RECURSE EXT_SRCS ext/*.go)
list(APPEND SRCS ${EXT_SRCS})
set(LIB "libcgosg${LIBSUFFIX}")

# Acquire libsignal_ffi.a (download pre-built or build from source), sets
# LIBSIGNAL_FFI_DIR / LIBSIGNAL_FFI_FILE here and LIBSIGNAL_FFI_PATH in the
# parent scope. Shared with lib/gostat (combined WhatsApp+Signal c-archive).
include(${CMAKE_CURRENT_SOURCE_DIR}/libsignal.cmake)

add_custom_command(OUTPUT ${OUTPUT_DIR}/${LIB}
  DEPENDS ${SRCS}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMAND env GOPATH=${GOPATH} CGO_ENABLED=1 LIBRARY_PATH=${LIBSIGNAL_FFI_DIR}
  go build -buildmode=${BUILDMODE} ${CUSTOM_GO_FLAGS}
  -o "${OUTPUT_DIR}/${LIB}"
  ${CMAKE_GO_FLAGS} .
  COMMENT "Building Go library")
add_custom_target(${TARGET} DEPENDS ${OUTPUT_DIR}/${LIB} ${HEADER})

# Reference Go library (c-archive is a static library, link it by path)
if (HAS_STATICGOLIB)
  add_library(ref-cgosg STATIC IMPORTED GLOBAL)
else()
  add_library(ref-cgosg SHARED IMPORTED GLOBAL)
endif()
add_dependencies(ref-cgosg ${TARGET})
set_target_properties(ref-cgosg
  PROPERTIES
  IMPORTED_NO_SONAME TRUE
  IMPORTED_LOCATION ${OUTPUT_DIR}/${LIB}
  INTERFACE_INCLUDE_DIRECTORIES ${OUTPUT_DIR})

# Install
if (NOT HAS_STATICGOLIB)
  install(FILES ${OUTPUT_DIR}/${LIB} DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
