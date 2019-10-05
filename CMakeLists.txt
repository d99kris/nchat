# Project
cmake_minimum_required(VERSION 3.1 FATAL_ERROR)
project(nchat VERSION 0.22 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)

# Paths
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# Build type
set(DEFAULT_BUILD_TYPE "Release")
if((EXISTS "${CMAKE_SOURCE_DIR}/.svn") OR (EXISTS "${CMAKE_SOURCE_DIR}/.git"))
  set(DEFAULT_BUILD_TYPE "Debug")
endif()
 
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Using build type '${DEFAULT_BUILD_TYPE}' (default).")
  set(CMAKE_BUILD_TYPE "${DEFAULT_BUILD_TYPE}")
else()
  message(STATUS "Using build type '${CMAKE_BUILD_TYPE}'.")
endif()

# Platform specifics
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  add_definitions(-D_XOPEN_SOURCE_EXTENDED)
  set(CMAKE_PREFIX_PATH /usr/local/opt/ncurses)
  set(OPENSSL_ROOT_DIR /usr/local/opt/openssl)
endif()

# Application
add_executable(nchat
  ext/apathy/path.hpp
  ext/emojicpp/emoji.h
  src/config.cpp
  src/config.h
  src/log.cpp
  src/log.h
  src/main.cpp
  src/protocol.h
  src/setup.cpp
  src/setup.h
  src/telegram.cpp
  src/telegram.h
  src/ui.h
  src/uicommon.cpp
  src/uicommon.h
  src/uidefault.cpp
  src/uidefault.h
  src/uilite.cpp
  src/uilite.h
  src/util.cpp
  src/util.h)
install(TARGETS nchat DESTINATION bin)

# Headers
target_include_directories(nchat PRIVATE "ext/apathy")
target_include_directories(nchat PRIVATE "ext/emojicpp")

# Compiler flags
set_target_properties(nchat PROPERTIES COMPILE_FLAGS
                      "-Wall -Wextra -Wpedantic -Wshadow -Wpointer-arith \
                       -Wcast-qual -Wno-missing-braces -Wswitch-default -Wunreachable-code \
                       -Wundef -Wuninitialized -Wcast-align")
target_compile_definitions(nchat PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")

# Dependency tdlib
add_subdirectory(ext/td EXCLUDE_FROM_ALL)
set(TDLIBNAME "libtdclientshared${CMAKE_SHARED_LIBRARY_SUFFIX}")
install(FILES ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/${TDLIBNAME} DESTINATION lib)

# Dependency ncurses
set(CURSES_NEED_NCURSES TRUE)
set(CURSES_NEED_WIDE TRUE)
find_package(Curses REQUIRED)
include_directories(${CURSES_INCLUDE_DIR})

# Linking
target_link_libraries(nchat PUBLIC tdclientshared ${CURSES_NCURSES_LIBRARY})

# Manual
install(FILES src/nchat.1 DESTINATION share/man/man1)

# Uninstall
add_custom_target(uninstall
  COMMAND "${CMAKE_COMMAND}" -E remove "${CMAKE_INSTALL_PREFIX}/bin/nchat"
  COMMAND "${CMAKE_COMMAND}" -E remove "${CMAKE_INSTALL_PREFIX}/lib/${TDLIBNAME}"
  COMMAND "${CMAKE_COMMAND}" -E remove "${CMAKE_INSTALL_PREFIX}/share/man/man1/nchat.1"
)

