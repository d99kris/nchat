# embedutil.cmake
#
# Copyright (c) 2026 Kristofer Berggren
# All rights reserved.
#
# nchat is distributed under the MIT license, see LICENSE for details.
#
# Helper functions for embedfile.cmake and embedthemes.cmake.

# Read IN_FILE and set OUT_VAR to its bytes as a comma-separated list of hex
# literals, wrapped at 12 bytes per line. An empty file yields an empty string.
function(embed_hex_bytes IN_FILE OUT_VAR)
  file(READ "${IN_FILE}" HEX_DATA HEX)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1, " BYTES "${HEX_DATA}")
  string(REPEAT "0x[0-9a-f][0-9a-f], " 12 LINE_PATTERN)
  string(REGEX REPLACE "(${LINE_PATTERN})" "\\1\n  " BYTES "${BYTES}")
  string(REGEX REPLACE " \n" "\n" BYTES "${BYTES}")
  string(REGEX REPLACE ",[ \n]*$" "" BYTES "${BYTES}")

  set(${OUT_VAR} "${BYTES}" PARENT_SCOPE)
endfunction()

# Set OUT_VAR to the theme names listed in INDEX_FILE, skipping comments and
# blank lines.
function(embed_theme_names INDEX_FILE OUT_VAR)
  file(STRINGS "${INDEX_FILE}" NAMES REGEX "^[a-z0-9]")
  set(${OUT_VAR} "${NAMES}" PARENT_SCOPE)
endfunction()
