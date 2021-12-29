# Project
cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(libncutil LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)

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

# Library
add_library(ncutil SHARED 
  src/apputil.cpp
  src/apputil.h
  src/config.cpp
  src/config.h
  src/emojilist.cpp
  src/emojilist.h
  src/fileutil.cpp
  src/fileutil.h
  src/log.cpp
  src/log.h
  src/messagecache.cpp
  src/messagecache.h
  src/numutil.cpp
  src/numutil.h
  src/profiles.cpp
  src/profiles.h
  src/protocolutil.cpp
  src/protocolutil.h
  src/scopeddirlock.cpp
  src/scopeddirlock.h
  src/status.cpp
  src/status.h
  src/strutil.cpp
  src/strutil.h
  src/timeutil.cpp
  src/timeutil.h
)
install(TARGETS ncutil DESTINATION lib)

# Headers
target_include_directories(ncutil PRIVATE "../ncutil/src" ${MAGIC_INCLUDE_DIR})
target_include_directories(ncutil PRIVATE "../common/src")
target_include_directories(ncutil PRIVATE "../../ext/apathy")
target_include_directories(ncutil PRIVATE "../../ext/emojicpp")
target_include_directories(ncutil PRIVATE "../../ext/sqlite_modern_cpp/hdr")

# Compiler flags
set_target_properties(ncutil PROPERTIES COMPILE_FLAGS
                      "-Wall -Wextra -Wpedantic -Wshadow -Wpointer-arith \
                       -Wcast-qual -Wno-missing-braces -Wswitch-default \
                       -Wunreachable-code -Wundef -Wuninitialized \
                       -Wcast-align")
target_compile_definitions(ncutil PRIVATE NCHAT_PROJECT_VERSION="${NCHAT_PROJECT_VERSION}")

# Linking
target_link_libraries(ncutil PUBLIC ${MAGIC_LIBRARY} SQLite::SQLite3)
