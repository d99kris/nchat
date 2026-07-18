# Project
cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(nchat LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)
include(CheckCXXSourceCompiles)
include(TestBigEndian)

# Paths
if (NOT DEFINED CMAKE_INSTALL_LIBDIR)
  set(CMAKE_INSTALL_LIBDIR "lib")
endif()
if (NOT DEFINED CMAKE_INSTALL_DATADIR)
  set(CMAKE_INSTALL_DATADIR "share")
endif()
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# Build type (Debug, Release, RelWithDebInfo)
set(DEFAULT_BUILD_TYPE "Release")
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Using build type '${DEFAULT_BUILD_TYPE}' (default)")
  set(CMAKE_BUILD_TYPE "${DEFAULT_BUILD_TYPE}")
else()
  message(STATUS "Using build type '${CMAKE_BUILD_TYPE}'")
endif()

# Build origin (github / local, or packager-provided e.g. debian via
# -DNCHAT_BUILD_ORIGIN=debian). Auto-detected as "github" under GitHub Actions.
if(NOT DEFINED NCHAT_BUILD_ORIGIN)
  if(DEFINED ENV{GITHUB_ACTIONS})
    set(NCHAT_BUILD_ORIGIN "github")
  else()
    set(NCHAT_BUILD_ORIGIN "local")
  endif()
endif()
message(STATUS "Build origin: ${NCHAT_BUILD_ORIGIN}")

# Build git sha/branch, resolved at configure time from the source checkout.
# Guarded on a top-level .git so source-tarball builds (no repo) resolve to sha
# "unknown" / empty branch, without picking up an enclosing repo if the tarball
# is unpacked inside one.
set(NCHAT_BUILD_GIT_SHA "unknown")
set(NCHAT_BUILD_GIT_BRANCH "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
  # Short sha, fixed to 7 chars (git's --short auto-grows the abbreviation to
  # stay unique, so a plain --short can yield 8+ chars in larger clones such as
  # GitHub's). Suffixed "+" when tracked files have local changes.
  execute_process(COMMAND git rev-parse --short=7 HEAD
                  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                  OUTPUT_VARIABLE GIT_SHA
                  OUTPUT_STRIP_TRAILING_WHITESPACE
                  ERROR_QUIET)
  if(GIT_SHA)
    execute_process(COMMAND git status --porcelain --untracked-files=no
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    OUTPUT_VARIABLE GIT_DIRTY
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET)
    if(GIT_DIRTY)
      set(GIT_SHA "${GIT_SHA}+")
    endif()
    set(NCHAT_BUILD_GIT_SHA "${GIT_SHA}")
  endif()

  # Branch name, left empty for detached HEAD (e.g. release-tag builds) so it is
  # only appended to the app name for actual working branches.
  execute_process(COMMAND git rev-parse --abbrev-ref HEAD
                  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                  OUTPUT_VARIABLE GIT_BRANCH
                  OUTPUT_STRIP_TRAILING_WHITESPACE
                  ERROR_QUIET)
  if(GIT_BRANCH AND NOT GIT_BRANCH STREQUAL "HEAD")
    set(NCHAT_BUILD_GIT_BRANCH "${GIT_BRANCH}")
  endif()
endif()
message(STATUS "Build git sha: ${NCHAT_BUILD_GIT_SHA}")
message(STATUS "Build git branch: ${NCHAT_BUILD_GIT_BRANCH}")

# Platform specifics
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  add_definitions(-D_XOPEN_SOURCE_EXTENDED)

  # ncurses
  if (DEFINED NCURSES_ROOT_DIR AND EXISTS "${NCURSES_ROOT_DIR}")
    message(STATUS "Ncurses cmake prefix '${NCURSES_ROOT_DIR}' (user-specified)")
  else()
    execute_process(COMMAND sh "-c"
                    "command -v brew &> /dev/null && brew --prefix ncurses 2> /dev/null | tr -d '\n'"
                    OUTPUT_VARIABLE NCURSES_ROOT_DIR)
    if (EXISTS "${NCURSES_ROOT_DIR}")
      message(STATUS "Ncurses cmake prefix '${NCURSES_ROOT_DIR}' (homebrew)")
    else()
      foreach(FALLBACK_DIR /opt/homebrew/opt/ncurses /usr/local/opt/ncurses)
        if (EXISTS "${FALLBACK_DIR}")
          set(NCURSES_ROOT_DIR "${FALLBACK_DIR}")
          break()
        endif()
      endforeach()
      if (EXISTS "${NCURSES_ROOT_DIR}")
        message(STATUS "Ncurses cmake prefix '${NCURSES_ROOT_DIR}' (fallback)")
      elseif (EXISTS "/opt/local")
        set(NCURSES_ROOT_DIR "/opt/local")
        message(STATUS "Ncurses cmake prefix '${NCURSES_ROOT_DIR}' (macports)")
      endif()
    endif()
  endif()
  if (EXISTS "${NCURSES_ROOT_DIR}")
    list(APPEND CMAKE_PREFIX_PATH ${NCURSES_ROOT_DIR})
  endif()

  # openssl
  if (DEFINED OPENSSL_ROOT_DIR AND EXISTS "${OPENSSL_ROOT_DIR}")
    message(STATUS "OpenSSL cmake prefix '${OPENSSL_ROOT_DIR}' (user-specified)")
  else()
    execute_process(COMMAND sh "-c"
                    "command -v brew &> /dev/null && brew --prefix openssl 2> /dev/null | tr -d '\n'"
                    OUTPUT_VARIABLE OPENSSL_ROOT_DIR)
    if (EXISTS "${OPENSSL_ROOT_DIR}")
      message(STATUS "OpenSSL cmake prefix '${OPENSSL_ROOT_DIR}' (homebrew)")
    else()
      foreach(FALLBACK_DIR /opt/homebrew/opt/openssl /usr/local/opt/openssl)
        if (EXISTS "${FALLBACK_DIR}")
          set(OPENSSL_ROOT_DIR "${FALLBACK_DIR}")
          break()
        endif()
      endforeach()
      if (EXISTS "${OPENSSL_ROOT_DIR}")
        message(STATUS "OpenSSL cmake prefix '${OPENSSL_ROOT_DIR}' (fallback)")
      elseif (EXISTS "/opt/local")
        set(OPENSSL_ROOT_DIR "/opt/local")
        message(STATUS "OpenSSL cmake prefix '${OPENSSL_ROOT_DIR}' (macports)")
      endif()
    endif()
  endif()
  if (EXISTS "${OPENSSL_ROOT_DIR}")
    list(APPEND CMAKE_PREFIX_PATH ${OPENSSL_ROOT_DIR})
  endif()

  # sqlite
  if (DEFINED SQLITE_ROOT_DIR AND EXISTS "${SQLITE_ROOT_DIR}")
    message(STATUS "SQLite cmake prefix '${SQLITE_ROOT_DIR}' (user-specified)")
  else()
    execute_process(COMMAND sh "-c"
                    "command -v brew &> /dev/null && brew --prefix sqlite 2> /dev/null | tr -d '\n'"
                    OUTPUT_VARIABLE SQLITE_ROOT_DIR)
    if (EXISTS "${SQLITE_ROOT_DIR}")
      message(STATUS "SQLite cmake prefix '${SQLITE_ROOT_DIR}' (homebrew)")
    else()
      foreach(FALLBACK_DIR /opt/homebrew/opt/sqlite /usr/local/opt/sqlite)
        if (EXISTS "${FALLBACK_DIR}")
          set(SQLITE_ROOT_DIR "${FALLBACK_DIR}")
          break()
        endif()
      endforeach()
      if (EXISTS "${SQLITE_ROOT_DIR}")
        message(STATUS "SQLite cmake prefix '${SQLITE_ROOT_DIR}' (fallback)")
      elseif (EXISTS "/opt/local")
        set(SQLITE_ROOT_DIR "/opt/local")
        message(STATUS "SQLite cmake prefix '${SQLITE_ROOT_DIR}' (macports)")
      endif()
    endif()
  endif()
  if (EXISTS "${SQLITE_ROOT_DIR}")
    list(APPEND CMAKE_PREFIX_PATH ${SQLITE_ROOT_DIR})
  endif()

elseif (${CMAKE_SYSTEM_NAME} MATCHES "Android")
  add_compile_definitions(_XOPEN_SOURCE_EXTENDED)
  if (DEFINED ENV{PREFIX} AND "$ENV{PREFIX}" MATCHES "^/data/data/com\\.termux/files/usr$")
    message(STATUS "Found termux")
    add_compile_definitions(HAVE_TERMUX)
  endif()
elseif (${CMAKE_SYSTEM_NAME} MATCHES "OpenBSD")
  add_definitions(-D_XOPEN_SOURCE_EXTENDED)
endif()

# OpenBSD restrictions
if (${CMAKE_SYSTEM_NAME} MATCHES "OpenBSD")
  set(HAS_WHATSAPP OFF)
  set(HAS_SIGNAL OFF)
  set(HAS_SHARED_LIBS OFF)
  set(HAS_DYNAMICLOAD OFF)
  message(STATUS "Disabling WhatsApp, Signal, Shared libs, Dynamic load - OpenBSD restrictions")
endif()

# Config - Static linking of external library dependencies
option(HAS_STATIC_EXTLIBS "Static Ext Libs" OFF)
message(STATUS "Static Ext Libs: ${HAS_STATIC_EXTLIBS}")
if(HAS_STATIC_EXTLIBS)
  set(HAS_SHARED_LIBS OFF)
  set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
  set(OPENSSL_USE_STATIC_LIBS TRUE)
  if (NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    add_link_options(-static-libstdc++ -static-libgcc)
  endif()
endif()

# Config - Components built as shared libraries
option(HAS_SHARED_LIBS "Shared Libs" ON)
message(STATUS "Shared Libs: ${HAS_SHARED_LIBS}")

# Config - Build with Address Sanitizer (ASAN)
# To set ASAN log path: ASAN_OPTIONS=log_path=asan.log ./build/bin/nchat
option(HAS_ASAN "ASAN" OFF)
message(STATUS "ASAN: ${HAS_ASAN}")

# Optional component - Dummy Protocol
option(HAS_DUMMY "Dummy" ON)
message(STATUS "Dummy: ${HAS_DUMMY}")

# Optional component - Telegram
option(HAS_TELEGRAM "Telegram" ON)
message(STATUS "Telegram: ${HAS_TELEGRAM}")

# Optional component - WhatsApp
option(HAS_WHATSAPP "WhatsApp" ON)
message(STATUS "WhatsApp: ${HAS_WHATSAPP}")

# Optional component - Signal
option(HAS_SIGNAL "Signal" OFF)
message(STATUS "Signal: ${HAS_SIGNAL}")

# musl restrictions - Go c-archives built with a stock Go toolchain segfault
# at startup on musl (nchat issue #204 / Go PR #69325), so on musl WhatsApp and
# Signal (both Go/cgo) require a patched Go toolchain, asserted via
# -DHAS_MUSL_GO_PATCHED=ON (see doc/MUSLGO.md).
option(HAS_MUSL_GO_PATCHED "Musl Go Patched" OFF)
execute_process(COMMAND ${CMAKE_CXX_COMPILER} -dumpmachine
                OUTPUT_VARIABLE TARGET_TRIPLE ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
if (TARGET_TRIPLE MATCHES "musl")
  set(NCHAT_BUILD_MUSL 1)
  if (HAS_WHATSAPP AND NOT HAS_MUSL_GO_PATCHED)
    set(HAS_WHATSAPP OFF)
    message(STATUS "WhatsApp: ${HAS_WHATSAPP} (Forced - musl needs a Go toolchain patched with Go PR #69325, then pass -DHAS_MUSL_GO_PATCHED=ON; see doc/MUSLGO.md)")
  endif()
  if (HAS_SIGNAL AND NOT HAS_MUSL_GO_PATCHED)
    set(HAS_SIGNAL OFF)
    message(STATUS "Signal: ${HAS_SIGNAL} (Forced - musl needs a Go toolchain patched with Go PR #69325, then pass -DHAS_MUSL_GO_PATCHED=ON; see doc/MUSLGO.md)")
  endif()
endif()

# Feature - Dynamic library load
option(HAS_DYNAMICLOAD "Dynamic Load" ON)
if(HAS_DYNAMICLOAD AND NOT HAS_SHARED_LIBS)
  set(HAS_DYNAMICLOAD OFF)
  message(STATUS "Dynamic Load: ${HAS_DYNAMICLOAD} (Forced)")
else()
  message(STATUS "Dynamic Load: ${HAS_DYNAMICLOAD}")
endif()

# Feature - Multi protocol
option(HAS_MULTIPROTOCOL "Multi Protocol" ON)
message(STATUS "Multi Protocol: ${HAS_MULTIPROTOCOL}")

# Feature - Core dump
option(HAS_COREDUMP "Core Dump" ON)
message(STATUS "Core Dump: ${HAS_COREDUMP}")

# Feature - Static Go library
option(HAS_STATICGOLIB "Static Go" ON)
message(STATUS "Static Go: ${HAS_STATICGOLIB}")

# Feature - Combined Go library (single c-archive hosting both WhatsApp and Signal)
# When the internal protocol libraries are static (NOT HAS_SHARED_LIBS), their Go
# c-archives are pulled into nchat's single final link. Two independent Go
# c-archives cannot co-link there (duplicate embedded runtime and mattn/go-sqlite3
# cgo-export symbols). So when both WhatsApp and Signal are enabled in a static,
# static-Go build, assemble them into one c-archive (one runtime) via lib/gostat.
# Shared-library builds (incl. the default dynamic-load build) keep the separate
# per-protocol libraries — each its own loaded object, so no collision — and never
# trigger this.
if(HAS_WHATSAPP AND HAS_SIGNAL AND HAS_STATICGOLIB AND NOT HAS_SHARED_LIBS)
  set(HAS_COMBINED_GOLIB ON)
else()
  set(HAS_COMBINED_GOLIB OFF)
endif()
message(STATUS "Combined Go library: ${HAS_COMBINED_GOLIB}")

# Check Golang version for whatsmeow minimum requirement
set(GO_VERSION_MIN 1.23)
execute_process(COMMAND bash "-c" "go version 2> /dev/null | cut -c14- | cut -d' ' -f1 | tr -d '\n'" OUTPUT_VARIABLE GO_VERSION)
if((HAS_WHATSAPP OR HAS_SIGNAL) AND (NOT GO_VERSION VERSION_GREATER_EQUAL GO_VERSION_MIN))
  message(FATAL_ERROR "Go version ${GO_VERSION} (need >= ${GO_VERSION_MIN} to build WhatsApp or Signal)")
endif()

# Check Little/Big Endian for tdlib requirement
test_big_endian(IS_BIG_ENDIAN)
if(IS_BIG_ENDIAN)
  message(STATUS "Endian: Big")
  if(HAS_TELEGRAM)
    message(WARNING "Telegram requires little endian, auto-disabling")
	set(HAS_TELEGRAM OFF)
  endif()
else()
  message(STATUS "Endian: Little")
endif()

# Common compiler and linking flags
if(HAS_ASAN)
  set(NCHAT_CFLAGS -fsanitize=address -fno-omit-frame-pointer)
  set(NCHAT_LDFLAGS -fsanitize=address)
endif()

# Detached debug symbols (enabled by the dist/release build scripts). When on,
# nchat's own translation units are compiled with -g1 so the DWARF can be split
# into a detached nchat.debug / .dSYM after the build. Applied via
# target_common_config() below, so it is scoped to nchat's own targets (the
# nchat executable and the ncutil / *chat libraries) and deliberately excludes
# the heavy vendored subbuilds — tdlib and the whatsmeow/signalmeow Go
# c-archives — so only nchat's own frames land in the shipped symbols artifacts
# (the Go archives are additionally built -ldflags=-w; see lib/*/go/CMakeLists).
# -g1 emits function + line tables (enough to resolve a backtrace to
# function + file:line) but omits the local-variable/type DWARF that dominates
# -g2's size. -g1 does not change codegen, so the stripped release binary stays
# byte-for-byte a plain Release build.
option(HAS_DEBUG_SYMBOLS "Emit detached debug symbols for nchat" OFF)
message(STATUS "Debug symbols: ${HAS_DEBUG_SYMBOLS}")

# Helper function for applying common target properties
function(target_common_config target)
  # Build type specifics
  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(${target} PRIVATE NCHAT_BUILD_RELEASE=1)
  elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    target_compile_definitions(${target} PRIVATE NCHAT_BUILD_RELWITHDEBINFO=1)
  elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(${target} PRIVATE NCHAT_BUILD_DEBUG=1)
  endif()

  # Build origin/sha/branch are consumed only by lib/ncutil (SysUtil build-info
  # accessors) via a generated buildinfo.h -- see lib/ncutil/CMakeLists.txt --
  # rather than defined target-wide here, so a git sha/branch change on every
  # commit does not force a full rebuild of every target.
  if(HAS_STATIC_EXTLIBS)
    target_compile_definitions(${target} PRIVATE NCHAT_BUILD_STATIC_EXTLIBS=1)
  endif()
  if(NCHAT_BUILD_MUSL)
    target_compile_definitions(${target} PRIVATE NCHAT_BUILD_MUSL=1)
    # musl has no execinfo/backtrace() and libgcc's _Unwind_Backtrace cannot step
    # past musl's signal trampoline, so the crash handler walks the frame-pointer
    # chain seeded from the signal ucontext (see AppUtil::SignalHandler /
    # FramePointerBacktrace in lib/ncutil/src/apputil.cpp). That walk needs a
    # maintained frame pointer in nchat's own frames, which -O2 otherwise omits.
    # Applied via target_common_config(), so it is scoped to nchat's own targets
    # (the nchat executable and the ncutil / *chat libraries) and to musl only --
    # the glibc/macOS builds use backtrace() and are unaffected. Paired with
    # -no-pie on the musl link line (utils/dist/build-in-container.sh) so the
    # logged addresses are absolute and resolve directly with
    # `addr2line -e nchat.debug`.
    target_compile_options(${target} PRIVATE -fno-omit-frame-pointer)
  endif()

  # On musl, WhatsApp and Signal (Go/cgo) are only built when the Go toolchain
  # carries the PR #69325 patch, asserted via -DHAS_MUSL_GO_PATCHED=ON (see
  # doc/MUSLGO.md). Propagate that to the C++ side so SysUtil::IsSupportedLibc()
  # treats such builds -- the official musl release binaries -- as supported and
  # does not print the "unsupported platform" WhatsApp/Signal setup warning.
  if(HAS_MUSL_GO_PATCHED)
    target_compile_definitions(${target} PRIVATE NCHAT_MUSL_GO_PATCHED=1)
  endif()

  # Compile options
  target_compile_options(${target} PUBLIC ${NCHAT_CFLAGS})

  # Detached debug symbols (see HAS_DEBUG_SYMBOLS). Scoped here so only nchat's
  # own code contributes DWARF to the detached nchat.debug / .dSYM; -g1 keeps
  # function + line tables for backtrace symbolication.
  if(HAS_DEBUG_SYMBOLS)
    target_compile_options(${target} PRIVATE -g1)
  endif()

  # Linking
  target_link_libraries(${target} PUBLIC ${NCHAT_LDFLAGS})
endfunction()

# Internal library linking type
if(HAS_SHARED_LIBS)
  set(LIBRARY_LINKING_TYPE SHARED)
else()
  set(LIBRARY_LINKING_TYPE STATIC)
endif()

# Embedded compose script - bundled in the executable and extracted to the
# temp dir at runtime, so no separate libexec install is needed.
set(COMPOSE_SCRIPT_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/composescript.h")
add_custom_command(
  OUTPUT "${COMPOSE_SCRIPT_HEADER}"
  COMMAND "${CMAKE_COMMAND}"
          -DIN_FILE=${CMAKE_CURRENT_SOURCE_DIR}/src/compose
          -DOUT_FILE=${COMPOSE_SCRIPT_HEADER}
          -DVAR_NAME=ComposeScript
          -P ${CMAKE_CURRENT_SOURCE_DIR}/utils/embedfile.cmake
  DEPENDS src/compose utils/embedfile.cmake
  COMMENT "Embedding compose script"
  VERBATIM
)

# Application
add_executable(nchat
  ${COMPOSE_SCRIPT_HEADER}
  src/main.cpp
  src/ui.cpp
  src/ui.h
  src/uichatlistdialog.cpp
  src/uichatlistdialog.h
  src/uicolorconfig.cpp
  src/uicolorconfig.h
  src/uiconfig.cpp
  src/uiconfig.h
  src/uicontactlistdialog.cpp
  src/uicontactlistdialog.h
  src/uicontroller.cpp
  src/uigroupmemberlistdialog.cpp
  src/uigroupmemberlistdialog.h
  src/uicontroller.h
  src/uidialog.cpp
  src/uidialog.h
  src/uiemojilistdialog.cpp
  src/uiemojilistdialog.h
  src/uientryview.cpp
  src/uientryview.h
  src/uifilelistdialog.cpp
  src/uifilelistdialog.h
  src/uihelpview.cpp
  src/uihelpview.h
  src/uihistoryview.cpp
  src/uihistoryview.h
  src/uikeyconfig.cpp
  src/uikeyconfig.h
  src/uikeydump.cpp
  src/uikeydump.h
  src/uikeyinput.cpp
  src/uikeyinput.h
  src/uilistborderview.cpp
  src/uilistborderview.h
  src/uilistdialog.cpp
  src/uilistdialog.h
  src/uilistview.cpp
  src/uilistview.h
  src/uimessagedialog.cpp
  src/uimessagedialog.h
  src/uimodel.cpp
  src/uimodel.h
  src/uiscreen.cpp
  src/uiscreen.h
  src/uistatusview.cpp
  src/uistatusview.h
  src/uitextinputdialog.cpp
  src/uitextinputdialog.h
  src/uitopview.cpp
  src/uitopview.h
  src/uiview.cpp
  src/uiview.h
  src/uiviewbase.cpp
  src/uiviewbase.h
)
install(TARGETS nchat DESTINATION bin)

# Headers
target_include_directories(nchat PRIVATE "lib/common/src")
target_include_directories(nchat PRIVATE "lib/ncutil/src")
target_include_directories(nchat PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

# Compiler flags
set_target_properties(nchat PROPERTIES COMPILE_FLAGS
                      "-Wall -Wextra -Wpedantic -Wshadow -Wpointer-arith \
                       -Wcast-qual -Wno-missing-braces -Wswitch-default \
                       -Wunreachable-code -Wundef -Wuninitialized \
                       -Wcast-align")

# Dependency libraries
add_subdirectory(lib/ncutil)
if(HAS_DUMMY)
  add_subdirectory(lib/duchat)
endif()
if(HAS_TELEGRAM)
  add_subdirectory(lib/tgchat)
endif()
if(HAS_COMBINED_GOLIB)
  # Combined WhatsApp+Signal c-archive; added before the protocol dirs so its
  # ref-cgogostat target and LIBSIGNAL_FFI_PATH / GO_LIBRARIES are available when
  # lib/wmchat and lib/sgchat configure.
  add_subdirectory(lib/gostat)
endif()
if(HAS_WHATSAPP)
  add_subdirectory(lib/wmchat)
endif()
if(HAS_SIGNAL)
  add_subdirectory(lib/sgchat)
endif()

# Dependency ncurses
if (${CMAKE_SYSTEM_NAME} MATCHES "OpenBSD")
  target_link_libraries(nchat PUBLIC ncurses)
else()
  set(CURSES_NEED_NCURSES TRUE)
  set(CURSES_NEED_WIDE TRUE)
  find_package(Curses REQUIRED)
  if(HAS_STATIC_EXTLIBS)
    # static ncursesw does not bundle terminfo routines on some distros
    find_library(TINFO_LIBRARY NAMES tinfow tinfo)
    if(TINFO_LIBRARY)
      list(APPEND CURSES_LIBRARIES ${TINFO_LIBRARY})
    endif()
  endif()
  include_directories(${CURSES_INCLUDE_DIR})
  target_compile_options(nchat PUBLIC ${NCURSES_CFLAGS})
endif()

# Dependency execinfo
if (${CMAKE_SYSTEM_NAME} MATCHES "OpenBSD")
  find_library(EXECINFO_LIBRARY execinfo)
  if(EXECINFO_LIBRARY)
    target_link_libraries(nchat PUBLIC ${EXECINFO_LIBRARY})
  endif()
endif()

# Linking
target_link_libraries(nchat PUBLIC ncutil pthread ${CURSES_LIBRARIES} ${CMAKE_DL_LIBS})

# Optionals
if(HAS_DUMMY)
  target_include_directories(nchat PRIVATE "lib/duchat/src")
  target_compile_definitions(nchat PRIVATE HAS_DUMMY="${HAS_DUMMY}")
  if(NOT HAS_DYNAMICLOAD)
    target_link_libraries(nchat PUBLIC duchat)
  endif()
endif()

if(HAS_TELEGRAM)
  target_include_directories(nchat PRIVATE "lib/tgchat/src")
  target_compile_definitions(nchat PRIVATE HAS_TELEGRAM="${HAS_TELEGRAM}")
  if(NOT HAS_DYNAMICLOAD)
    target_link_libraries(nchat PUBLIC tgchat)
  endif()
endif()

if(HAS_WHATSAPP)
  target_include_directories(nchat PRIVATE "lib/wmchat/src")
  target_compile_definitions(nchat PRIVATE HAS_WHATSAPP="${HAS_WHATSAPP}")
  if(NOT HAS_DYNAMICLOAD)
    target_link_libraries(nchat PUBLIC wmchat)
  endif()
endif()

if(HAS_SIGNAL)
  target_include_directories(nchat PRIVATE "lib/sgchat/src")
  target_compile_definitions(nchat PRIVATE HAS_SIGNAL="${HAS_SIGNAL}")
  if(NOT HAS_DYNAMICLOAD)
    target_link_libraries(nchat PUBLIC sgchat)
  endif()
endif()

# Features
if(HAS_MULTIPROTOCOL)
  target_compile_definitions(nchat PRIVATE HAS_MULTIPROTOCOL="${HAS_MULTIPROTOCOL}")
endif()

if(HAS_DYNAMICLOAD)
  target_compile_definitions(nchat PRIVATE HAS_DYNAMICLOAD="${HAS_DYNAMICLOAD}")
endif()

if(HAS_COREDUMP)
  target_compile_definitions(nchat PRIVATE HAS_COREDUMP="${HAS_COREDUMP}")
  if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    # Core dump entitlements
    set(SIGNSCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/utils/sign")
    set(ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/src/nchat.entitlements")
    install(CODE "execute_process(COMMAND
      \"${SIGNSCRIPT}\" \"${ENTITLEMENTS}\" \"\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/nchat\"
      )" COMPONENT Runtime)
  endif()
endif()

# Common
target_common_config(nchat)
target_compile_definitions(nchat PRIVATE
                           CMAKE_INSTALL_LIBDIR="${CMAKE_INSTALL_LIBDIR}"
                           GO_VERSION="${GO_VERSION}"
                           GO_VERSION_MIN="${GO_VERSION_MIN}")

# Third-party license attribution
#
# Combine the committed per-component license fragments into a single notices
# file matching this build's enabled protocols and link mode, for binary
# distributions. The Go fragments (lib/wmchat, lib/sgchat) are regenerated by
# utils/dist/gen-third-party-licenses; the others are hand-maintained. The
# combined-work preamble mirrors the license rule in ShowVersion()
# (src/main.cpp): AGPL v3 when Signal is compiled in (mautrix-signal), else
# GPL v3 when WhatsApp is compiled in (whatsmeow links GPL-3.0 libsignal),
# else MIT.
if(HAS_SIGNAL)
  set(TPL_PREAMBLE "${CMAKE_CURRENT_SOURCE_DIR}/utils/licenses/preamble-agpl.txt")
elseif(HAS_WHATSAPP)
  set(TPL_PREAMBLE "${CMAKE_CURRENT_SOURCE_DIR}/utils/licenses/preamble-gpl.txt")
else()
  set(TPL_PREAMBLE "${CMAKE_CURRENT_SOURCE_DIR}/utils/licenses/preamble-mit.txt")
endif()

set(TPL_FRAGMENTS
  "${TPL_PREAMBLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/lib/ncutil/THIRD_PARTY_LICENSES")
if(HAS_TELEGRAM)
  list(APPEND TPL_FRAGMENTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/tgchat/THIRD_PARTY_LICENSES")
endif()
if(HAS_WHATSAPP)
  list(APPEND TPL_FRAGMENTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/wmchat/THIRD_PARTY_LICENSES")
endif()
if(HAS_SIGNAL)
  list(APPEND TPL_FRAGMENTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/sgchat/THIRD_PARTY_LICENSES")
endif()
if(HAS_STATIC_EXTLIBS)
  list(APPEND TPL_FRAGMENTS "${CMAKE_CURRENT_SOURCE_DIR}/utils/dist/THIRD_PARTY_LICENSES.static")
endif()

set(TPL_COMBINED "${CMAKE_CURRENT_BINARY_DIR}/THIRD_PARTY_LICENSES")
file(WRITE "${TPL_COMBINED}" "")
foreach(TPL_FRAGMENT ${TPL_FRAGMENTS})
  if(NOT EXISTS "${TPL_FRAGMENT}")
    message(FATAL_ERROR "Missing third-party license fragment: ${TPL_FRAGMENT}")
  endif()
  file(READ "${TPL_FRAGMENT}" TPL_FRAGMENT_TEXT)
  file(APPEND "${TPL_COMBINED}" "${TPL_FRAGMENT_TEXT}")
  file(APPEND "${TPL_COMBINED}" "\n")
  # Regenerate the combined file when a fragment or preamble changes.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${TPL_FRAGMENT}")
endforeach()

# Install the combined notices file and nchat's own license to the FHS doc dir
# (share/doc/nchat), so binary distributions carry attribution.
install(FILES "${TPL_COMBINED}" "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/nchat")

# Manual install dir
if(NOT CMAKE_INSTALL_MANDIR)
  if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(CMAKE_INSTALL_MANDIR "${CMAKE_INSTALL_PREFIX}/share/man")
  else()
    set(CMAKE_INSTALL_MANDIR "${CMAKE_INSTALL_PREFIX}/man")
  endif()
  message(STATUS "Using fallback man dir: ${CMAKE_INSTALL_MANDIR}")
else()
  message(STATUS "Using default man dir: ${CMAKE_INSTALL_MANDIR}")
endif()

# Manual
install(FILES src/nchat.1 DESTINATION "${CMAKE_INSTALL_MANDIR}/man1")

# Uninstall
if(HAS_SHARED_LIBS)
  add_custom_target(uninstall
    COMMAND "${CMAKE_COMMAND}" -E remove "${CMAKE_INSTALL_PREFIX}/bin/nchat"
    COMMAND "${CMAKE_COMMAND}" -E remove "${CMAKE_INSTALL_MANDIR}/man1/nchat.1"
    COMMAND "${CMAKE_COMMAND}" -E remove "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libncutil${CMAKE_SHARED_LIBRARY_SUFFIX}"
    COMMAND "${CMAKE_COMMAND}" -E remove "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libtdclientshared${CMAKE_SHARED_LIBRARY_SUFFIX}"
    COMMAND "${CMAKE_COMMAND}" -E remove "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libtgchat${CMAKE_SHARED_LIBRARY_SUFFIX}"
    COMMAND "${CMAKE_COMMAND}" -E remove "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libduchat${CMAKE_SHARED_LIBRARY_SUFFIX}"
    COMMAND "${CMAKE_COMMAND}" -E remove "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libwmchat${CMAKE_SHARED_LIBRARY_SUFFIX}"
    COMMAND "${CMAKE_COMMAND}" -E remove "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libsgchat${CMAKE_SHARED_LIBRARY_SUFFIX}"
    COMMAND "${CMAKE_COMMAND}" -E remove "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/doc/nchat/THIRD_PARTY_LICENSES"
    COMMAND "${CMAKE_COMMAND}" -E remove "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/doc/nchat/LICENSE"
    COMMAND "${CMAKE_COMMAND}" -E remove_directory "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/doc/nchat"
  )
else()
  add_custom_target(uninstall
    COMMAND "${CMAKE_COMMAND}" -E remove "${CMAKE_INSTALL_PREFIX}/bin/nchat"
    COMMAND "${CMAKE_COMMAND}" -E remove "${CMAKE_INSTALL_MANDIR}/man1/nchat.1"
    COMMAND "${CMAKE_COMMAND}" -E remove "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/doc/nchat/THIRD_PARTY_LICENSES"
    COMMAND "${CMAKE_COMMAND}" -E remove "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/doc/nchat/LICENSE"
    COMMAND "${CMAKE_COMMAND}" -E remove_directory "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/doc/nchat"
  )
endif()

# Dev Application
if(CMAKE_BUILD_TYPE MATCHES "Debug")
  message(STATUS "Building dev application")
  add_executable(devnchat
    src/main.cpp
    dev/ui.cpp
    dev/ui.h
  )

  # Headers
  target_include_directories(devnchat PRIVATE "lib/common/src")
  target_include_directories(devnchat PRIVATE "lib/ncutil/src")

  # Compiler flags
  set_target_properties(devnchat PROPERTIES COMPILE_FLAGS
                        "-Wall -Wextra -Wpedantic -Wshadow -Wpointer-arith \
                         -Wcast-qual -Wno-missing-braces -Wswitch-default \
                         -Wunreachable-code -Wundef -Wuninitialized \
                         -Wcast-align")

  # Linking
  target_link_libraries(devnchat PUBLIC ncutil pthread ${CURSES_NCURSES_LIBRARY} dl)

  # Optionals
  if(HAS_DUMMY)
    target_include_directories(devnchat PRIVATE "lib/duchat/src")
    target_compile_definitions(devnchat PRIVATE HAS_DUMMY="${HAS_DUMMY}")
    if(NOT HAS_DYNAMICLOAD)
      target_link_libraries(devnchat PUBLIC duchat)
    endif()
  endif()

  if(HAS_TELEGRAM)
    target_include_directories(devnchat PRIVATE "lib/tgchat/src")
    target_compile_definitions(devnchat PRIVATE HAS_TELEGRAM="${HAS_TELEGRAM}")
    if(NOT HAS_DYNAMICLOAD)
      target_link_libraries(devnchat PUBLIC tgchat)
    endif()
  endif()

  if(HAS_WHATSAPP)
    target_include_directories(devnchat PRIVATE "lib/wmchat/src")
    target_compile_definitions(devnchat PRIVATE HAS_WHATSAPP="${HAS_WHATSAPP}")
    if(NOT HAS_DYNAMICLOAD)
      target_link_libraries(devnchat PUBLIC wmchat)
    endif()
  endif()

  # Features
  if(HAS_MULTIPROTOCOL)
    target_compile_definitions(devnchat PRIVATE HAS_MULTIPROTOCOL="${HAS_MULTIPROTOCOL}")
  endif()

  if(HAS_DYNAMICLOAD)
    target_compile_definitions(devnchat PRIVATE HAS_DYNAMICLOAD="${HAS_DYNAMICLOAD}")
  endif()

  # Common
  target_compile_definitions(devnchat PRIVATE CMAKE_INSTALL_LIBDIR="${CMAKE_INSTALL_LIBDIR}"
                             GO_VERSION="${GO_VERSION}" GO_VERSION_MIN="${GO_VERSION_MIN}")

endif()
