# Project
cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(libcgogostat)

# lib/gostat — combined WhatsApp+Signal Go c-archive.
#
# Used only for static-Go builds with BOTH protocols enabled (HAS_COMBINED_GOLIB,
# see top-level CMakeLists), where two separate protocol c-archives cannot be
# linked into one executable. It assembles a single Go module hosting both
# protocols as subpackages and builds one c-archive (one embedded runtime). The
# lib/wmchat/go and lib/sgchat/go dirs are left untouched and still build their
# own libraries for the shared/dynamic-load configuration. See assemble.sh.

# Check Golang version
execute_process(COMMAND bash "-c" "go version | cut -c14- | cut -d' ' -f1 | tr -d '\n'" OUTPUT_VARIABLE GO_VERSION)
message(STATUS "Go version ${GO_VERSION}.")
set(CUSTOM_GO_FLAGS -modcacherw -buildvcs=false)

# Detached debug symbols: omit this c-archive's Go DWARF with -ldflags=-w when the
# dist/release build splits nchat's own DWARF into a detached file (see the same
# note in lib/wmchat/go and lib/sgchat/go).
if(HAS_DEBUG_SYMBOLS)
  list(APPEND CUSTOM_GO_FLAGS -ldflags=-w)
endif()

# Check Go package (gccgo needs an explicit -lgo at final link)
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

# The two untouched protocol source dirs the combined module is assembled from
get_filename_component(WM_GO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../wmchat/go" ABSOLUTE)
get_filename_component(SG_GO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sgchat/go" ABSOLUTE)

# Acquire libsignal_ffi.a (shared with the standalone Signal build); the Signal
# build tooling (build-libsignal.sh, ext/signal) lives in lib/sgchat/go.
set(SIGNAL_GO_DIR ${SG_GO_DIR})
include(${SG_GO_DIR}/libsignal.cmake)

# Build the combined Go library / C archive
set(TARGET cgogostat)
set(GOPATH ${OUTPUT_DIR})
set(STAGE ${CMAKE_CURRENT_BINARY_DIR}/module)
set(LIB "libgostat${LIBSUFFIX}")

# Rebuild when either protocol's authored sources, module files, or vendored
# dependency trees change (mirrors the standalone modules' EXT_SRCS glob).
set(SRCS
  ${WM_GO_DIR}/gowm.go ${WM_GO_DIR}/cgowm.go ${WM_GO_DIR}/go.mod ${WM_GO_DIR}/go.sum
  ${SG_GO_DIR}/gosg.go ${SG_GO_DIR}/cgosg.go ${SG_GO_DIR}/go.mod ${SG_GO_DIR}/go.sum
  ${CMAKE_CURRENT_SOURCE_DIR}/assemble.sh)
file(GLOB_RECURSE WM_EXT_SRCS ${WM_GO_DIR}/ext/*.go)
file(GLOB_RECURSE SG_EXT_SRCS ${SG_GO_DIR}/ext/*.go)
list(APPEND SRCS ${WM_EXT_SRCS} ${SG_EXT_SRCS})

set(HEADERS ${OUTPUT_DIR}/libcgowm.h ${OUTPUT_DIR}/libcgosg.h)
add_custom_command(OUTPUT ${OUTPUT_DIR}/${LIB} ${HEADERS}
  DEPENDS ${SRCS}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMAND env GOPATH=${GOPATH} CGO_ENABLED=1 LIBRARY_PATH=${LIBSIGNAL_FFI_DIR}
  bash ${CMAKE_CURRENT_SOURCE_DIR}/assemble.sh
  ${WM_GO_DIR} ${SG_GO_DIR} ${STAGE} ${OUTPUT_DIR} ${LIB} ${BUILDMODE}
  ${CUSTOM_GO_FLAGS} ${CMAKE_GO_FLAGS}
  COMMENT "Building combined WhatsApp+Signal Go library")
add_custom_target(${TARGET} DEPENDS ${OUTPUT_DIR}/${LIB})

# Reference the combined c-archive. IMPORTED GLOBAL so both the wmchat and sgchat
# C++ libraries (sibling dirs) can link it; ld extracts each archive member at
# most once, so referencing it from both leaves a single embedded runtime.
if (HAS_STATICGOLIB)
  add_library(ref-cgogostat STATIC IMPORTED GLOBAL)
else()
  add_library(ref-cgogostat SHARED IMPORTED GLOBAL)
endif()
add_dependencies(ref-cgogostat ${TARGET})
set_target_properties(ref-cgogostat
  PROPERTIES
  IMPORTED_NO_SONAME TRUE
  IMPORTED_LOCATION ${OUTPUT_DIR}/${LIB}
  INTERFACE_INCLUDE_DIRECTORIES ${OUTPUT_DIR})

# Install (shared build only; combined mode is c-archive in practice)
if (NOT HAS_STATICGOLIB)
  install(FILES ${OUTPUT_DIR}/${LIB} DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
