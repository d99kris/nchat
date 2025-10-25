# Project
cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(libncutil LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 14)
include(CheckIncludeFile)

# Ccache
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()

# Dependencies
find_library(MAGIC_LIBRARY magic)
find_path(MAGIC_INCLUDE_DIR magic.h
  /usr/local/include
  /usr/include
)
find_package(SQLite3 REQUIRED)

# Platform specifics
if (${CMAKE_SYSTEM_NAME} MATCHES "Android")
  find_library(WORDEXP_LIBRARY android-wordexp)
endif()

# Library
add_library(ncutil ${LIBRARY_LINKING_TYPE}
  src/appconfig.cpp
  src/appconfig.h
  src/apputil.cpp
  src/apputil.h
  src/cacheutil.cpp
  src/cacheutil.h
  src/clipboard.cpp
  src/clipboard.h
  src/config.cpp
  src/config.h
  src/debuginfo.cpp
  src/debuginfo.h
  src/emojilist.cpp
  src/emojilist.h
  src/emojiutil.cpp
  src/emojiutil.h
  src/emojiutil_map.h
  src/emojiutil_view.h
  src/fileutil.cpp
  src/fileutil.h
  src/log.cpp
  src/log.h
  src/messagecache.cpp
  src/messagecache.h
  src/numutil.cpp
  src/numutil.h
  src/owned_mutex.cpp
  src/owned_mutex.h
  src/profiles.cpp
  src/profiles.h
  src/protocolutil.cpp
  src/protocolutil.h
  src/scopeddirlock.cpp
  src/scopeddirlock.h
  src/serialization.h
  src/sqlitehelp.cpp
  src/sqlitehelp.h
  src/status.cpp
  src/status.h
  src/strutil.cpp
  src/strutil.h
  src/sysutil.cpp
  src/sysutil.h
  src/timeutil.cpp
  src/timeutil.h
)
install(TARGETS ncutil DESTINATION ${CMAKE_INSTALL_LIBDIR})

# Optional dependencies
check_include_file_cxx(execinfo.h FOUND_EXECINFO_H)
if(FOUND_EXECINFO_H)
  target_compile_definitions(ncutil PRIVATE HAVE_EXECINFO_H=1)
endif()

find_package(PNG)
if (PNG_FOUND)
  set(CMAKE_REQUIRED_INCLUDES_SAVE ${CMAKE_REQUIRED_INCLUDES})
  set(CMAKE_REQUIRED_INCLUDES ${CMAKE_REQUIRED_INCLUDES};${PNG_INCLUDE_DIRS})
  check_include_file_cxx(png.h FOUND_PNG_H)
  set(CMAKE_REQUIRED_INCLUDES ${CMAKE_REQUIRED_INCLUDES_SAVE})
endif()
if(FOUND_PNG_H)
  target_include_directories(ncutil PRIVATE ${PNG_INCLUDE_DIRS})
  target_link_libraries(ncutil PRIVATE ${PNG_LIBRARIES})
  target_compile_definitions(ncutil PRIVATE HAVE_PNG_H=1)
  message(STATUS "Clipboard: Text, Image")
else()
  message(STATUS "Clipboard: Text")
endif()

# Common
target_common_config(ncutil)

# Headers
target_include_directories(ncutil PRIVATE "../ncutil/src" ${MAGIC_INCLUDE_DIR})
target_include_directories(ncutil PRIVATE "../common/src")
target_include_directories(ncutil PRIVATE "../../ext/apathy")
target_include_directories(ncutil PRIVATE "../../ext/sqlite_modern_cpp/hdr")
target_include_directories(ncutil PRIVATE "../../ext/clip")
target_include_directories(ncutil PRIVATE "../../ext/cereal/include")

# Compiler flags
set_target_properties(ncutil PROPERTIES COMPILE_FLAGS
                      "-Wall -Wextra -Wpedantic -Wshadow -Wpointer-arith \
                       -Wcast-qual -Wno-missing-braces -Wswitch-default \
                       -Wunreachable-code -Wundef -Wuninitialized \
                       -Wcast-align")

# Linking
target_link_libraries(ncutil PUBLIC ${MAGIC_LIBRARY} SQLite::SQLite3 clip ${WORDEXP_LIBRARY})
