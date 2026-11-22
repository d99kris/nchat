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
if(HAS_STATIC_EXTLIBS)
  # static libmagic needs its compression backends
  foreach(MAGIC_DEP z bz2 lzma zstd)
    find_library(MAGIC_DEP_LIBRARY_${MAGIC_DEP} ${MAGIC_DEP})
    if(MAGIC_DEP_LIBRARY_${MAGIC_DEP})
      list(APPEND MAGIC_LIBRARY ${MAGIC_DEP_LIBRARY_${MAGIC_DEP}})
    endif()
  endforeach()
endif()
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

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
  src/buildinfo.cpp
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
  src/emojiwidthutil.cpp
  src/emojiwidthutil.h
  src/fileutil.cpp
  src/fileutil.h
  src/setuputil.cpp
  src/setuputil.h
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
  src/qrutil.cpp
  src/qrutil.h
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

# Volatile build metadata (git sha/branch/origin, resolved by the top-level
# CMakeLists and inherited here) is written to a generated header included only
# by buildinfo.cpp, rather than passed target-wide via target_compile_definitions,
# so a commit-to-commit sha/branch change recompiles only buildinfo.cpp instead of
# all of ncutil. configure_file rewrites the header only when a value changes.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/buildinfo.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/generated/buildinfo.h" @ONLY)
target_include_directories(ncutil PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

# Bundled libmagic database. libmagic compiles in its default db path from its
# install prefix, which for static dist builds refers to the build host (e.g.
# /opt/nchat-deps/share/misc/magic.mgc) and does not exist on user systems, so
# mime detection fails and images get sent as documents. Static builds thus
# embed the db (gzip-compressed, ~0.45 MB vs ~10 MB raw) and load it with
# magic_load_buffers(). The db format is tied to the libmagic version, so it is
# taken from the same prefix as the magic.h being built against.
if(HAS_STATIC_EXTLIBS)
  if(CMAKE_VERSION VERSION_LESS 3.19)
    message(FATAL_ERROR "Static build requires CMake >= 3.19 to bundle magic.mgc")
  endif()
  if(NOT MAGIC_DEP_LIBRARY_z)
    message(FATAL_ERROR "Static build requires zlib to bundle magic.mgc")
  endif()
  get_filename_component(MAGIC_PREFIX "${MAGIC_INCLUDE_DIR}" DIRECTORY)
  find_file(MAGIC_DB_FILE magic.mgc PATHS "${MAGIC_PREFIX}/share/misc" NO_DEFAULT_PATH)
  if(NOT MAGIC_DB_FILE)
    message(FATAL_ERROR "Static build requires magic.mgc in ${MAGIC_PREFIX}/share/misc")
  endif()
  find_path(ZLIB_INCLUDE_DIR zlib.h)
  set(MAGIC_DB_GZ "${CMAKE_CURRENT_BINARY_DIR}/generated/magic.mgc.gz")
  set(MAGIC_DB_SRC "${CMAKE_CURRENT_BINARY_DIR}/generated/magicdb.c")
  file(ARCHIVE_CREATE OUTPUT "${MAGIC_DB_GZ}" PATHS "${MAGIC_DB_FILE}"
       FORMAT raw COMPRESSION GZip COMPRESSION_LEVEL 9)
  file(SIZE "${MAGIC_DB_FILE}" MAGIC_DB_SIZE)
  file(SIZE "${MAGIC_DB_GZ}" MAGIC_DB_GZ_SIZE)
  file(READ "${MAGIC_DB_GZ}" MAGIC_DB_HEX HEX)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," MAGIC_DB_HEX "${MAGIC_DB_HEX}")
  file(WRITE "${MAGIC_DB_SRC}"
       "// Generated from ${MAGIC_DB_FILE}, do not edit.\n"
       "#include <stddef.h>\n"
       "const size_t g_MagicDbSize = ${MAGIC_DB_SIZE};\n"
       "const size_t g_MagicDbGzSize = ${MAGIC_DB_GZ_SIZE};\n"
       "const unsigned char g_MagicDbGz[] = {${MAGIC_DB_HEX}};\n")
  unset(MAGIC_DB_HEX)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${MAGIC_DB_FILE}")
  target_sources(ncutil PRIVATE "${MAGIC_DB_SRC}")
  target_include_directories(ncutil PRIVATE ${ZLIB_INCLUDE_DIR})
  target_compile_definitions(ncutil PRIVATE HAS_BUNDLED_MAGIC=1)
  message(STATUS "Bundled magic db: ${MAGIC_DB_FILE} (${MAGIC_DB_GZ_SIZE} bytes compressed)")
endif()

# Optional dependencies
check_include_file_cxx(execinfo.h FOUND_EXECINFO_H)
if(FOUND_EXECINFO_H)
  target_compile_definitions(ncutil PRIVATE HAVE_EXECINFO_H=1)
  if (${CMAKE_SYSTEM_NAME} MATCHES "OpenBSD|FreeBSD")
    find_library(EXECINFO_LIBRARY execinfo)
    if(EXECINFO_LIBRARY)
      target_link_libraries(ncutil PUBLIC ${EXECINFO_LIBRARY})
    endif()
  endif()
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

# External dependencies
add_subdirectory(ext/QR-Code-generator/cpp)
add_subdirectory(ext/stb)
add_subdirectory(ext/clip)
if(HAS_STATIC_EXTLIBS AND NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  # static libxcb needs libXau and libXdmcp
  find_library(XAU_LIBRARY Xau)
  find_library(XDMCP_LIBRARY Xdmcp)
  if(XAU_LIBRARY AND XDMCP_LIBRARY)
    target_link_libraries(clip ${XAU_LIBRARY} ${XDMCP_LIBRARY})
  endif()
  # static libpng needs zlib after it on the link line
  if(FOUND_PNG_H)
    target_link_libraries(clip ${PNG_LIBRARIES})
  endif()
endif()

# Common
target_common_config(ncutil)

# Headers
target_include_directories(ncutil PRIVATE "../ncutil/src" ${MAGIC_INCLUDE_DIR})
target_include_directories(ncutil PRIVATE "../common/src")
target_include_directories(ncutil PRIVATE "ext/apathy")
target_include_directories(ncutil PRIVATE "ext/sqlite_modern_cpp/hdr" ${SQLite3_INCLUDE_DIRS})
target_include_directories(ncutil PRIVATE "ext/clip")
target_include_directories(ncutil PRIVATE "ext/cereal/include")

# Compiler flags
set_target_properties(ncutil PROPERTIES COMPILE_FLAGS
                      "-Wall -Wextra -Wpedantic -Wshadow -Wpointer-arith \
                       -Wcast-qual -Wno-missing-braces -Wswitch-default \
                       -Wunreachable-code -Wundef -Wuninitialized \
                       -Wcast-align")

# Linking
target_link_libraries(ncutil PUBLIC ${MAGIC_LIBRARY} ${SQLite3_LIBRARIES} clip qrcodegen stb ${WORDEXP_LIBRARY} Threads::Threads ${CMAKE_DL_LIBS})
