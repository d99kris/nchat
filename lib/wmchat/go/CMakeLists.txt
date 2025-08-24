# Project
cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(libcgowm)

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
set(TARGET cgowm)
set(GOPATH ${OUTPUT_DIR})
set(SRCS gowm.go cgowm.go)
set(LIB "libcgowm${CMAKE_SHARED_LIBRARY_SUFFIX}")
add_custom_command(OUTPUT ${OUTPUT_DIR}/${LIB}
  DEPENDS ${SRCS}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMAND env GOPATH=${GOPATH} CGO_ENABLED=1 go build -buildmode=${BUILDMODE} ${CUSTOM_GO_FLAGS}
  -o "${OUTPUT_DIR}/${LIB}"
  ${CMAKE_GO_FLAGS} ./...
  COMMENT "Building Go library")
add_custom_target(${TARGET} DEPENDS ${OUTPUT_DIR}/${LIB} ${HEADER})

# Build shared library
add_library(ref-cgowm SHARED IMPORTED GLOBAL)
add_dependencies(ref-cgowm ${TARGET})
set_target_properties(ref-cgowm
  PROPERTIES
  IMPORTED_NO_SONAME TRUE
  IMPORTED_LOCATION ${OUTPUT_DIR}/${LIB}
  INTERFACE_INCLUDE_DIRECTORIES ${OUTPUT_DIR})

# Install
if (NOT HAS_STATICGOLIB)
  install(FILES ${OUTPUT_DIR}/${LIB} DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
