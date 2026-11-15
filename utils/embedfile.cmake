# embedfile.cmake
#
# Copyright (c) 2026 Kristofer Berggren
# All rights reserved.
#
# nchat is distributed under the MIT license, see LICENSE for details.
#
# Generates a C/C++ header embedding a file as an unsigned char array.
# Usage:
#   cmake -DIN_FILE=<path> -DOUT_FILE=<path> -DVAR_NAME=<identifier> -P embedfile.cmake

if(NOT IN_FILE OR NOT OUT_FILE OR NOT VAR_NAME)
  message(FATAL_ERROR "usage: cmake -DIN_FILE=<path> -DOUT_FILE=<path> -DVAR_NAME=<identifier> -P embedfile.cmake")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/embedutil.cmake")
embed_hex_bytes("${IN_FILE}" BYTES)

get_filename_component(IN_NAME "${IN_FILE}" NAME)
get_filename_component(OUT_NAME "${OUT_FILE}" NAME)

file(WRITE "${OUT_FILE}"
  "// ${OUT_NAME} - generated from ${IN_NAME} by embedfile.cmake, do not edit\n"
  "\n"
  "#pragma once\n"
  "\n"
  "static const unsigned char ${VAR_NAME}[] =\n"
  "{\n"
  "  ${BYTES}\n"
  "};\n")
