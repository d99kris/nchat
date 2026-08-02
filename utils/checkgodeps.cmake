# checkgodeps.cmake
#
# Copyright (c) 2026 Kristofer Berggren
# All rights reserved.
#
# nchat is distributed under the MIT license, see LICENSE for details.
#
# Reports Go dependencies which the WhatsApp and Signal modules pin to different
# versions. Such a divergence builds fine per protocol, but may break the
# combined c-archive of a fully static build (see lib/gostat).
# Usage:
#   cmake -DWM_GO_MOD=<path> -DSG_GO_MOD=<path> -P checkgodeps.cmake

if(NOT WM_GO_MOD OR NOT SG_GO_MOD)
  message(FATAL_ERROR "usage: cmake -DWM_GO_MOD=<path> -DSG_GO_MOD=<path> -P checkgodeps.cmake")
endif()

# Read a go.mod require list into <prefix>_MODULES (module paths) and
# <prefix>_<module> (version). Handles both the single-line and the block form of
# require; comments, the go/toolchain directives and replace lines are skipped.
# The go.sum files are deliberately not used - they hold every version in the
# module graph, not the selected one.
macro(read_go_modules FILE PREFIX)
  set(${PREFIX}_MODULES "")
  file(STRINGS "${FILE}" GO_MOD_LINES)
  foreach(GO_MOD_LINE IN LISTS GO_MOD_LINES)
    string(REGEX REPLACE "//.*$" "" GO_MOD_LINE "${GO_MOD_LINE}")
    string(REGEX REPLACE "^[ \t]*require[ \t]+" "" GO_MOD_LINE "${GO_MOD_LINE}")
    string(STRIP "${GO_MOD_LINE}" GO_MOD_LINE)
    if(NOT GO_MOD_LINE MATCHES "=>")
      if(GO_MOD_LINE MATCHES "^([^ \t]+\\.[^ \t]+)[ \t]+(v[^ \t]+)$")
        list(APPEND ${PREFIX}_MODULES "${CMAKE_MATCH_1}")
        set(${PREFIX}_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
      endif()
    endif()
  endforeach()
endmacro()

# Report a pseudo-version by its base version, i.e. v0.9.12-0.20260717235539-
# f9ffa7eca58d as v0.9.12. Regular (tagged) versions have no timestamp/commit
# part and are left as they are.
macro(shorten_version OUT VERSION)
  string(REGEX REPLACE "-([0-9A-Za-z.]+\\.)?[0-9]+-[0-9a-f]+$" "" ${OUT} "${VERSION}")
endmacro()

# As above but keeping an abbreviated commit, i.e. v0.9.12-f9ffa7e. Used only
# for untagged modules, whose base version says nothing about which commit is
# pinned, and which would otherwise be reported as two identical strings.
set(HEX7 "[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]") # cmake regex has no {7}
macro(shorten_version_with_commit OUT VERSION)
  string(REGEX REPLACE "-([0-9A-Za-z.]+\\.)?[0-9]+-(${HEX7})[0-9a-f]*$" "-\\2" ${OUT} "${VERSION}")
endmacro()

read_go_modules("${WM_GO_MOD}" WM)
read_go_modules("${SG_GO_MOD}" SG)

# Only the base versions are compared, as differing commits within one base
# version - two pseudo-versions of the same upstream release, or a
# pseudo-version and the release it precedes - are the same API and build fine
# combined. The exception is v0.0.0, the base version of an untagged module,
# which carries no API information at all; there the commits are compared.
foreach(MODULE IN LISTS WM_MODULES)
  if(DEFINED SG_${MODULE})
    shorten_version(WM_VERSION "${WM_${MODULE}}")
    shorten_version(SG_VERSION "${SG_${MODULE}}")
    if("${WM_VERSION}" STREQUAL "v0.0.0" AND "${SG_VERSION}" STREQUAL "v0.0.0")
      shorten_version_with_commit(WM_VERSION "${WM_${MODULE}}")
      shorten_version_with_commit(SG_VERSION "${SG_${MODULE}}")
    endif()
    if(NOT "${WM_VERSION}" STREQUAL "${SG_VERSION}")
      message(STATUS "Common go dependency mismatch ${MODULE}: "
              "wmchat ${WM_VERSION} != sgchat ${SG_VERSION}")
    endif()
  endif()
endforeach()
