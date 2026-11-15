# embedthemes.cmake
#
# Copyright (c) 2026 Kristofer Berggren
# All rights reserved.
#
# nchat is distributed under the MIT license, see LICENSE for details.
#
# Generates a C++ header embedding the themes listed in index.txt, as unsigned
# char arrays (color.conf and usercolor.conf per theme) and an EmbeddedThemes
# table in index order.
# Usage:
#   cmake -DTHEMES_DIR=<path> -DOUT_FILE=<path> -P embedthemes.cmake

if(NOT THEMES_DIR OR NOT OUT_FILE)
  message(FATAL_ERROR "usage: cmake -DTHEMES_DIR=<path> -DOUT_FILE=<path> -P embedthemes.cmake")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/embedutil.cmake")
embed_theme_names("${THEMES_DIR}/index.txt" THEME_NAMES)
if(NOT THEME_NAMES)
  message(FATAL_ERROR "no themes listed in ${THEMES_DIR}/index.txt")
endif()

get_filename_component(OUT_NAME "${OUT_FILE}" NAME)

set(CONTENT "")
string(APPEND CONTENT
  "// ${OUT_NAME} - generated from index.txt by embedthemes.cmake, do not edit\n"
  "\n"
  "#pragma once\n"
  "\n"
  "#include <cstddef>\n"
  "\n"
  "struct EmbeddedTheme\n"
  "{\n"
  "  const char* name;\n"
  "  const unsigned char* colorData;\n"
  "  size_t colorSize;\n"
  "  const unsigned char* userColorData;\n"
  "  size_t userColorSize;\n"
  "};\n")

set(TABLE "")
foreach(THEME_NAME ${THEME_NAMES})
  string(MAKE_C_IDENTIFIER "${THEME_NAME}" THEME_ID)
  set(TABLE_ENTRY "  { \"${THEME_NAME}\"")
  foreach(CONF_NAME color usercolor)
    set(CONF_FILE "${THEMES_DIR}/${THEME_NAME}/${CONF_NAME}.conf")
    if(NOT EXISTS "${CONF_FILE}")
      message(FATAL_ERROR "theme ${THEME_NAME} listed in index.txt missing file: ${CONF_FILE}")
    endif()

    # Zero-length arrays are not valid C++, so an empty file (e.g. the default
    # theme's usercolor.conf) gets a single padding byte and an explicit size.
    file(SIZE "${CONF_FILE}" CONF_SIZE)
    embed_hex_bytes("${CONF_FILE}" BYTES)
    if(BYTES STREQUAL "")
      set(BYTES "0x00")
    endif()

    set(VAR_NAME "Theme_${THEME_ID}_${CONF_NAME}")
    string(APPEND CONTENT
      "\n"
      "static const unsigned char ${VAR_NAME}[] =\n"
      "{\n"
      "  ${BYTES}\n"
      "};\n")
    string(APPEND TABLE_ENTRY ", ${VAR_NAME}, ${CONF_SIZE}")
  endforeach()
  string(APPEND TABLE "${TABLE_ENTRY} },\n")
endforeach()

string(APPEND CONTENT
  "\n"
  "static const EmbeddedTheme EmbeddedThemes[] =\n"
  "{\n"
  "${TABLE}"
  "};\n")

file(WRITE "${OUT_FILE}" "${CONTENT}")
