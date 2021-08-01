# Project
cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(libcgowa)

# Check Golang version
execute_process(COMMAND bash "-c" "go version | cut -c14- | cut -d' ' -f1 | tr -d '\n'" OUTPUT_VARIABLE GO_VERSION)
if (GO_VERSION VERSION_GREATER_EQUAL 1.14.0)
  message(STATUS "Go version ${GO_VERSION} (enable modcacherw).")
  SET(CUSTOM_GO_FLAGS -modcacherw -mod=mod) # @todo: remove -mod=mod eventually, see https://github.com/golang/go/issues/44129
else ()
  message(STATUS "Go version ${GO_VERSION}.")
  SET(CUSTOM_GO_FLAGS "")
endif ()

# Build Go library / C archive
set(TARGET cgowa)
set(GOPATH ${CMAKE_CURRENT_BINARY_DIR})
set(SRCS gowa.go cgowa.go)
set(LIB "libcgowa${CMAKE_SHARED_LIBRARY_SUFFIX}")
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${LIB}
  DEPENDS ${SRCS}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMAND env GOPATH=${GOPATH} go build -buildmode=c-archive ${CUSTOM_GO_FLAGS}
  -o "${CMAKE_CURRENT_BINARY_DIR}/${LIB}" 
  ${CMAKE_GO_FLAGS} ./...
  COMMENT "Building Go library")
add_custom_target(${TARGET} DEPENDS ${LIB} ${HEADER})

# Build shared library
add_library(ref-cgowa SHARED IMPORTED GLOBAL)
add_dependencies(ref-cgowa ${TARGET})
set_target_properties(ref-cgowa
  PROPERTIES
  IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/${LIB}
  INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_BINARY_DIR})
