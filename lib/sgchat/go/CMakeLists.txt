# Project
cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(libcgosg)

# Check Golang version
execute_process(COMMAND bash "-c" "go version | cut -c14- | cut -d' ' -f1 | tr -d '\n'" OUTPUT_VARIABLE GO_VERSION)
message(STATUS "Go version ${GO_VERSION}.")
set(CUSTOM_GO_FLAGS -modcacherw -buildvcs=false)

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
else()
  set(BUILDMODE c-shared)
  set(OUTPUT_DIR ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
endif()

# Build Go library / C archive
set(TARGET cgosg)
set(GOPATH ${OUTPUT_DIR})
set(SRCS gosg.go cgosg.go)
set(LIB "libcgosg${CMAKE_SHARED_LIBRARY_SUFFIX}")

# Option to build libsignal_ffi from source instead of downloading
option(BUILD_LIBSIGNAL "Build libsignal_ffi from source" ON)
set(LIBSIGNAL_BUILD_REF "f5f6456c03c3266092609c838b28ab205e9200ec" CACHE STRING "Git ref (branch, tag, or commit SHA) for libsignal_ffi.a download")

set(LIBSIGNAL_FFI_DIR ${CMAKE_CURRENT_BINARY_DIR}/libsignal)
set(LIBSIGNAL_FFI_FILE ${LIBSIGNAL_FFI_DIR}/libsignal_ffi.a)

if(NOT BUILD_LIBSIGNAL)
  # Detect OS for libsignal download
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(LIBSIGNAL_OS "macos")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(LIBSIGNAL_OS "linux")
  else()
    message(FATAL_ERROR "Unsupported OS for libsignal_ffi download: ${CMAKE_SYSTEM_NAME}. Use -DBUILD_LIBSIGNAL=ON to build from source.")
  endif()

  # Detect arch for libsignal download
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(LIBSIGNAL_ARCH "arm64")
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
    set(LIBSIGNAL_ARCH "amd64")
  else()
    message(FATAL_ERROR "Unsupported architecture for libsignal_ffi download: ${CMAKE_SYSTEM_PROCESSOR}. Use -DBUILD_LIBSIGNAL=ON to build from source.")
  endif()

  # Invalidate cached libsignal_ffi.a if build ref has changed
  set(LIBSIGNAL_FFI_REF_FILE ${LIBSIGNAL_FFI_DIR}/libsignal_ffi.ref)
  if(EXISTS ${LIBSIGNAL_FFI_FILE} AND EXISTS ${LIBSIGNAL_FFI_REF_FILE})
    file(READ ${LIBSIGNAL_FFI_REF_FILE} LIBSIGNAL_FFI_CACHED_REF)
    string(STRIP "${LIBSIGNAL_FFI_CACHED_REF}" LIBSIGNAL_FFI_CACHED_REF)
    if(NOT "${LIBSIGNAL_FFI_CACHED_REF}" STREQUAL "${LIBSIGNAL_BUILD_REF}")
      message(STATUS "LIBSIGNAL_BUILD_REF changed (${LIBSIGNAL_FFI_CACHED_REF} -> ${LIBSIGNAL_BUILD_REF}), re-downloading")
      file(REMOVE ${LIBSIGNAL_FFI_FILE})
    endif()
  elseif(EXISTS ${LIBSIGNAL_FFI_FILE} AND NOT EXISTS ${LIBSIGNAL_FFI_REF_FILE})
    message(STATUS "No ref file found for existing libsignal_ffi.a, re-downloading")
    file(REMOVE ${LIBSIGNAL_FFI_FILE})
  endif()

  # Download libsignal_ffi.a if not already present
  if(NOT EXISTS ${LIBSIGNAL_FFI_FILE})
    set(LIBSIGNAL_FFI_URL "https://mau.dev/tulir/gomuks-build-docker/-/jobs/artifacts/${LIBSIGNAL_BUILD_REF}/raw/libsignal_ffi.a?job=libsignal%20${LIBSIGNAL_OS}%20${LIBSIGNAL_ARCH}")
    message(STATUS "Downloading libsignal_ffi.a for ${LIBSIGNAL_OS} ${LIBSIGNAL_ARCH}...")
    file(DOWNLOAD ${LIBSIGNAL_FFI_URL} ${LIBSIGNAL_FFI_FILE}
      TLS_VERIFY ON
      STATUS LIBSIGNAL_DOWNLOAD_STATUS)
    list(GET LIBSIGNAL_DOWNLOAD_STATUS 0 LIBSIGNAL_DOWNLOAD_CODE)
    list(GET LIBSIGNAL_DOWNLOAD_STATUS 1 LIBSIGNAL_DOWNLOAD_MSG)
    if(NOT LIBSIGNAL_DOWNLOAD_CODE EQUAL 0)
      file(REMOVE ${LIBSIGNAL_FFI_FILE})
      message(FATAL_ERROR "Failed to download libsignal_ffi.a: ${LIBSIGNAL_DOWNLOAD_MSG}")
    endif()
    file(SIZE ${LIBSIGNAL_FFI_FILE} LIBSIGNAL_FFI_SIZE)
    if(LIBSIGNAL_FFI_SIZE LESS 1000)
      file(REMOVE ${LIBSIGNAL_FFI_FILE})
      message(FATAL_ERROR "Downloaded libsignal_ffi.a is too small (${LIBSIGNAL_FFI_SIZE} bytes) — likely an error page")
    endif()
    message(STATUS "Downloaded libsignal_ffi.a (${LIBSIGNAL_FFI_SIZE} bytes)")
    file(WRITE ${LIBSIGNAL_FFI_REF_FILE} "${LIBSIGNAL_BUILD_REF}\n")
  else()
    message(STATUS "Using existing libsignal_ffi.a from ${LIBSIGNAL_FFI_DIR}")
  endif()
else()
  # Build libsignal_ffi from source
  # Determine required version from version.go
  file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/ext/signal/pkg/libsignalgo/version.go" LIBSIGNAL_VERSION_LINE REGEX "const Version")
  string(REGEX REPLACE ".*\"(.*)\".*" "\\1" LIBSIGNAL_REQUIRED_VERSION "${LIBSIGNAL_VERSION_LINE}")

  # Invalidate cached libsignal_ffi.a if version has changed
  set(LIBSIGNAL_FFI_VERSION_FILE ${LIBSIGNAL_FFI_DIR}/libsignal_ffi.version)
  if(EXISTS ${LIBSIGNAL_FFI_FILE} AND EXISTS ${LIBSIGNAL_FFI_VERSION_FILE})
    file(READ ${LIBSIGNAL_FFI_VERSION_FILE} LIBSIGNAL_FFI_CACHED_VERSION)
    string(STRIP "${LIBSIGNAL_FFI_CACHED_VERSION}" LIBSIGNAL_FFI_CACHED_VERSION)
    if(NOT "${LIBSIGNAL_FFI_CACHED_VERSION}" STREQUAL "${LIBSIGNAL_REQUIRED_VERSION}")
      message(STATUS "libsignal version changed (${LIBSIGNAL_FFI_CACHED_VERSION} -> ${LIBSIGNAL_REQUIRED_VERSION}), rebuilding")
      file(REMOVE ${LIBSIGNAL_FFI_FILE})
    endif()
  elseif(EXISTS ${LIBSIGNAL_FFI_FILE} AND NOT EXISTS ${LIBSIGNAL_FFI_VERSION_FILE})
    message(STATUS "No version file found for existing libsignal_ffi.a, rebuilding")
    file(REMOVE ${LIBSIGNAL_FFI_FILE})
  endif()

  if(NOT EXISTS ${LIBSIGNAL_FFI_FILE})
    message(STATUS "Building libsignal_ffi ${LIBSIGNAL_REQUIRED_VERSION} from source...")
    execute_process(
      COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/build-libsignal.sh" "${LIBSIGNAL_FFI_DIR}"
      RESULT_VARIABLE LIBSIGNAL_BUILD_RESULT)
    if(NOT LIBSIGNAL_BUILD_RESULT EQUAL 0)
      message(FATAL_ERROR "Failed to build libsignal_ffi from source. Ensure Rust toolchain (cargo), git, cmake, clang, and protobuf compiler are installed.")
    endif()
    if(NOT EXISTS ${LIBSIGNAL_FFI_FILE})
      message(FATAL_ERROR "build-libsignal.sh completed but ${LIBSIGNAL_FFI_FILE} was not produced.")
    endif()
    message(STATUS "Built libsignal_ffi.a ${LIBSIGNAL_REQUIRED_VERSION} from source")
    file(WRITE ${LIBSIGNAL_FFI_VERSION_FILE} "${LIBSIGNAL_REQUIRED_VERSION}\n")
  else()
    message(STATUS "Using existing libsignal_ffi.a ${LIBSIGNAL_REQUIRED_VERSION} from ${LIBSIGNAL_FFI_DIR}")
  endif()
endif()

# Export libsignal path for parent CMakeLists to link
set(LIBSIGNAL_FFI_PATH ${LIBSIGNAL_FFI_FILE} PARENT_SCOPE)
add_custom_command(OUTPUT ${OUTPUT_DIR}/${LIB}
  DEPENDS ${SRCS}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMAND env GOPATH=${GOPATH} CGO_ENABLED=1 LIBRARY_PATH=${LIBSIGNAL_FFI_DIR}
  go build -buildmode=${BUILDMODE} ${CUSTOM_GO_FLAGS}
  -o "${OUTPUT_DIR}/${LIB}"
  ${CMAKE_GO_FLAGS} .
  COMMENT "Building Go library")
add_custom_target(${TARGET} DEPENDS ${OUTPUT_DIR}/${LIB} ${HEADER})

# Build shared library
add_library(ref-cgosg SHARED IMPORTED GLOBAL)
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
