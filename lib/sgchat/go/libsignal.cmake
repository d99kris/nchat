# libsignal.cmake
#
# Acquire libsignal_ffi.a (download a pre-built artifact, or build from source)
# and expose its path. Extracted from lib/sgchat/go/CMakeLists.txt so both the
# standalone Signal build (lib/sgchat/go) and the combined WhatsApp+Signal
# c-archive build (lib/gostat) can share one copy of this logic.
#
# Inputs (set by the includer before include(); both default to the standalone
# sgchat/go layout so that lib/sgchat/go/CMakeLists.txt can include this with no
# extra setup):
#   SIGNAL_GO_DIR   - dir holding build-libsignal.sh and ext/signal (the Signal
#                     Go module source). Defaults to CMAKE_CURRENT_SOURCE_DIR.
# Outputs (set in the including scope, plus LIBSIGNAL_FFI_PATH to PARENT_SCOPE):
#   LIBSIGNAL_FFI_DIR   - dir containing libsignal_ffi.a (pass as LIBRARY_PATH to go build)
#   LIBSIGNAL_FFI_FILE  - full path to libsignal_ffi.a
#   LIBSIGNAL_FFI_PATH  - same as LIBSIGNAL_FFI_FILE, exported to the parent scope
#                         for the C++ protocol library to force-load at link time.

if(NOT DEFINED SIGNAL_GO_DIR)
  set(SIGNAL_GO_DIR ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# Option to download pre-built libsignal_ffi instead of building from source
option(DOWNLOAD_LIBSIGNAL "Download pre-built libsignal_ffi (falls back to building from source if unavailable)" ON)
# LIBSIGNAL_BUILD_REF — intentionally a plain (non-CACHE) variable so the value
# defined here always wins over CMakeCache.txt. The ref must match the ABI of
# the vendored libsignal-ffi.h; a CACHE variable would stick to its first
# configured value and silently ignore a bump here, leaving an ABI-mismatched
# libsignal_ffi.a in the build tree (runtime crash in signal_encrypt_message).
# This also means the ref cannot be overridden from the command line with
# -DLIBSIGNAL_BUILD_REF=...; edit this line directly (or use utils/signal-update).
set(LIBSIGNAL_BUILD_REF "bf9b99a622fb9bbe99550491b36f37e4a51b1e66")

set(LIBSIGNAL_FFI_DIR ${CMAKE_CURRENT_BINARY_DIR}/libsignal)
set(LIBSIGNAL_FFI_FILE ${LIBSIGNAL_FFI_DIR}/libsignal_ffi.a)

set(LIBSIGNAL_USE_DOWNLOAD FALSE)

if(DOWNLOAD_LIBSIGNAL)
  # Detect OS for libsignal download
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(LIBSIGNAL_OS "macos")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(LIBSIGNAL_OS "linux")
  else()
    message(STATUS "Unsupported OS for libsignal_ffi download: ${CMAKE_SYSTEM_NAME}, falling back to build from source.")
  endif()

  # Detect arch for libsignal download
  if(LIBSIGNAL_OS)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
      set(LIBSIGNAL_ARCH "arm64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
      set(LIBSIGNAL_ARCH "amd64")
    else()
      message(STATUS "Unsupported architecture for libsignal_ffi download: ${CMAKE_SYSTEM_PROCESSOR}, falling back to build from source.")
    endif()
  endif()

  if(LIBSIGNAL_OS AND LIBSIGNAL_ARCH)
    # Invalidate cached libsignal_ffi.a if build ref has changed
    set(LIBSIGNAL_FFI_REF_FILE ${LIBSIGNAL_FFI_DIR}/libsignal_ffi.ref)
    if(EXISTS ${LIBSIGNAL_FFI_FILE} AND EXISTS ${LIBSIGNAL_FFI_REF_FILE})
      file(READ ${LIBSIGNAL_FFI_REF_FILE} LIBSIGNAL_FFI_CACHED_REF)
      string(STRIP "${LIBSIGNAL_FFI_CACHED_REF}" LIBSIGNAL_FFI_CACHED_REF)
      if(NOT "${LIBSIGNAL_FFI_CACHED_REF}" STREQUAL "${LIBSIGNAL_BUILD_REF}")
        message(STATUS "LIBSIGNAL_BUILD_REF changed (${LIBSIGNAL_FFI_CACHED_REF} -> ${LIBSIGNAL_BUILD_REF}), re-downloading")
        file(REMOVE ${LIBSIGNAL_FFI_FILE})
        file(REMOVE ${LIBSIGNAL_FFI_REF_FILE})
      endif()
    elseif(EXISTS ${LIBSIGNAL_FFI_FILE} AND NOT EXISTS ${LIBSIGNAL_FFI_REF_FILE})
      message(STATUS "No ref file found for existing libsignal_ffi.a, re-downloading")
      file(REMOVE ${LIBSIGNAL_FFI_FILE})
    endif()

    # Download libsignal_ffi.a if not already present
    if(NOT EXISTS ${LIBSIGNAL_FFI_FILE})
      set(LIBSIGNAL_FFI_URL "https://mau.dev/tulir/gomuks-build-docker/-/jobs/artifacts/${LIBSIGNAL_BUILD_REF}/raw/libsignal_ffi.a?job=libsignal%20${LIBSIGNAL_OS}%20${LIBSIGNAL_ARCH}")
      message(STATUS "Downloading libsignal_ffi.a for ${LIBSIGNAL_OS} ${LIBSIGNAL_ARCH}...")
      file(DOWNLOAD ${LIBSIGNAL_FFI_URL} ${LIBSIGNAL_FFI_FILE}
        TLS_VERIFY ON
        STATUS LIBSIGNAL_DOWNLOAD_STATUS)
      list(GET LIBSIGNAL_DOWNLOAD_STATUS 0 LIBSIGNAL_DOWNLOAD_CODE)
      list(GET LIBSIGNAL_DOWNLOAD_STATUS 1 LIBSIGNAL_DOWNLOAD_MSG)
      if(NOT LIBSIGNAL_DOWNLOAD_CODE EQUAL 0)
        file(REMOVE ${LIBSIGNAL_FFI_FILE})
        message(STATUS "Failed to download libsignal_ffi.a: ${LIBSIGNAL_DOWNLOAD_MSG}, falling back to build from source.")
      else()
        file(SIZE ${LIBSIGNAL_FFI_FILE} LIBSIGNAL_FFI_SIZE)
        if(LIBSIGNAL_FFI_SIZE LESS 1000)
          file(REMOVE ${LIBSIGNAL_FFI_FILE})
          message(STATUS "Downloaded libsignal_ffi.a is too small (${LIBSIGNAL_FFI_SIZE} bytes), falling back to build from source.")
        else()
          message(STATUS "Downloaded libsignal_ffi.a (${LIBSIGNAL_FFI_SIZE} bytes)")
          file(WRITE ${LIBSIGNAL_FFI_REF_FILE} "${LIBSIGNAL_BUILD_REF}\n")
          set(LIBSIGNAL_USE_DOWNLOAD TRUE)
        endif()
      endif()
    else()
      message(STATUS "Using existing libsignal_ffi.a from ${LIBSIGNAL_FFI_DIR}")
      set(LIBSIGNAL_USE_DOWNLOAD TRUE)
    endif()
  endif()
endif()

if(NOT LIBSIGNAL_USE_DOWNLOAD)
  # Build libsignal_ffi from source
  # Determine required version from version.go
  file(STRINGS "${SIGNAL_GO_DIR}/ext/signal/pkg/libsignalgo/version.go" LIBSIGNAL_VERSION_LINE REGEX "const Version")
  string(REGEX REPLACE ".*\"(.*)\".*" "\\1" LIBSIGNAL_REQUIRED_VERSION "${LIBSIGNAL_VERSION_LINE}")

  # Invalidate cached libsignal_ffi.a if version has changed
  set(LIBSIGNAL_FFI_VERSION_FILE ${LIBSIGNAL_FFI_DIR}/libsignal_ffi.version)
  if(EXISTS ${LIBSIGNAL_FFI_FILE} AND EXISTS ${LIBSIGNAL_FFI_VERSION_FILE})
    file(READ ${LIBSIGNAL_FFI_VERSION_FILE} LIBSIGNAL_FFI_CACHED_VERSION)
    string(STRIP "${LIBSIGNAL_FFI_CACHED_VERSION}" LIBSIGNAL_FFI_CACHED_VERSION)
    if(NOT "${LIBSIGNAL_FFI_CACHED_VERSION}" STREQUAL "${LIBSIGNAL_REQUIRED_VERSION}")
      message(STATUS "libsignal version changed (${LIBSIGNAL_FFI_CACHED_VERSION} -> ${LIBSIGNAL_REQUIRED_VERSION}), rebuilding")
      file(REMOVE ${LIBSIGNAL_FFI_FILE})
    endif()
  elseif(EXISTS ${LIBSIGNAL_FFI_FILE} AND NOT EXISTS ${LIBSIGNAL_FFI_VERSION_FILE})
    message(STATUS "No version file found for existing libsignal_ffi.a, rebuilding")
    file(REMOVE ${LIBSIGNAL_FFI_FILE})
  endif()

  if(NOT EXISTS ${LIBSIGNAL_FFI_FILE})
    message(STATUS "Building libsignal_ffi ${LIBSIGNAL_REQUIRED_VERSION} from source...")
    execute_process(
      COMMAND bash "${SIGNAL_GO_DIR}/build-libsignal.sh" "${LIBSIGNAL_FFI_DIR}"
      RESULT_VARIABLE LIBSIGNAL_BUILD_RESULT)
    if(NOT LIBSIGNAL_BUILD_RESULT EQUAL 0)
      message(FATAL_ERROR "Failed to build libsignal_ffi from source. Ensure Rust toolchain (cargo), git, cmake, clang, and protobuf compiler are installed.")
    endif()
    if(NOT EXISTS ${LIBSIGNAL_FFI_FILE})
      message(FATAL_ERROR "build-libsignal.sh completed but ${LIBSIGNAL_FFI_FILE} was not produced.")
    endif()
    message(STATUS "Built libsignal_ffi.a ${LIBSIGNAL_REQUIRED_VERSION} from source")
    file(WRITE ${LIBSIGNAL_FFI_VERSION_FILE} "${LIBSIGNAL_REQUIRED_VERSION}\n")
  else()
    message(STATUS "Using existing libsignal_ffi.a ${LIBSIGNAL_REQUIRED_VERSION} from ${LIBSIGNAL_FFI_DIR}")
  endif()
endif()

# BoringSSL/OpenSSL symbol isolation for static builds that also contain Telegram.
#
# libsignal_ffi.a statically bundles BoringSSL, which exports OpenSSL-named symbols
# (EVP_sha256, EVP_DigestInit, PKCS5_PBKDF2_HMAC, ...). In one static executable
# that also links tdlib (Telegram) against the system OpenSSL, the final link binds
# tdlib's EVP_sha256 to BoringSSL (pulled in via --whole-archive) while
# EVP_MD_get_size — an OpenSSL-3.0-only name absent from BoringSSL — binds to the
# system OpenSSL. tdlib then passes a BoringSSL EVP_MD* to OpenSSL's
# EVP_MD_get_size, reads an incompatible struct, and aborts on the pbkdf2 hash-size
# assertion while loading an existing profile.
#
# Fix: combine libsignal_ffi.a into one relocatable object (so libsignal's own
# BoringSSL references stay internal) and localize every symbol except the signal_*
# FFI surface. tdlib's crypto references then resolve entirely to system OpenSSL,
# while libsignal keeps its private BoringSSL. Only needed when both libsignal and
# tdlib land in a single static image; shared/dynamic-load builds keep each in its
# own object and are left untouched.
if(NOT HAS_SHARED_LIBS AND HAS_TELEGRAM)
  set(LIBSIGNAL_FFI_LOCAL ${LIBSIGNAL_FFI_DIR}/libsignal_ffi_local.a)
  if(NOT EXISTS ${LIBSIGNAL_FFI_LOCAL} OR ${LIBSIGNAL_FFI_FILE} IS_NEWER_THAN ${LIBSIGNAL_FFI_LOCAL})
    message(STATUS "Isolating libsignal_ffi BoringSSL symbols (static Telegram build)...")
    set(LIBSIGNAL_FFI_OBJ ${LIBSIGNAL_FFI_DIR}/libsignal_ffi_combined.o)
    if(APPLE)
      # macOS/Mach-O: ld64 has neither objcopy nor --whole-archive. Combine and
      # localize in one relocatable link — -force_load pulls in every archive
      # member, and -exported_symbols_list keeps only the signal_* FFI surface
      # global (Mach-O C symbols carry a leading '_'), marking BoringSSL and all
      # other internals private_extern so they cannot satisfy tdlib's external
      # OpenSSL references.
      set(LIBSIGNAL_FFI_SYMLIST ${LIBSIGNAL_FFI_DIR}/libsignal_ffi_exported.list)
      file(WRITE ${LIBSIGNAL_FFI_SYMLIST} "_signal_*\n")
      execute_process(
        COMMAND ${CMAKE_C_COMPILER} -r -nostdlib
                -Wl,-force_load,${LIBSIGNAL_FFI_FILE}
                -Wl,-exported_symbols_list,${LIBSIGNAL_FFI_SYMLIST}
                -o ${LIBSIGNAL_FFI_OBJ}
        RESULT_VARIABLE LIBSIGNAL_LOCALIZE_RESULT)
      if(NOT LIBSIGNAL_LOCALIZE_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to combine/localize libsignal_ffi.a into a relocatable object")
      endif()
    else()
      # ELF/GNU-ld: combine into one relocatable object, then isolate symbols with
      # objcopy. --keep-global-symbol 'signal_*' keeps only the FFI surface global,
      # localizing BoringSSL and every other defined internal so they cannot satisfy
      # tdlib's external OpenSSL references.
      #
      # --weaken-symbol additionally makes the undefined reference to the glibc-2.29
      # extension posix_spawn_file_actions_addchdir_np weak. The prebuilt
      # libsignal_ffi.a is a musl-target Rust build, so std's process code takes the
      # static-libc path and references the symbol *strongly* (musl always provides
      # it) rather than through std's own weak!. The glibc release container ships
      # glibc < 2.29, so the final link cannot resolve that strong reference.
      # Weakening lets it resolve to 0 there — the same intent as std's weak! on
      # glibc targets — while a real definition still binds normally wherever one
      # exists. The call is reachable only via Rust's Command::current_dir(), which
      # libsignal (a crypto library spawning no subprocesses) never uses.
      #
      # Weaken by name only. Weakening every symbol that is weak *somewhere* in the
      # archive would be wrong: getrandom, for one, is weak in std but strong in the
      # bundled getrandom crate, and weakening it would leave that crate's function
      # pointer NULL in the fully-static musl build — a weak undef does not pull a
      # definition out of libc.a — crashing libsignal's RNG.
      if(NOT CMAKE_OBJCOPY)
        find_program(CMAKE_OBJCOPY NAMES objcopy)
      endif()
      if(NOT CMAKE_OBJCOPY)
        message(FATAL_ERROR "objcopy not found; required to isolate libsignal_ffi BoringSSL symbols")
      endif()
      # Relocatable link via the C compiler driver so the correct (possibly cross)
      # toolchain ld is used.
      execute_process(
        COMMAND ${CMAKE_C_COMPILER} -r -nostdlib
                -Wl,--whole-archive ${LIBSIGNAL_FFI_FILE} -Wl,--no-whole-archive
                -o ${LIBSIGNAL_FFI_OBJ}
        RESULT_VARIABLE LIBSIGNAL_LOCALIZE_RESULT)
      if(NOT LIBSIGNAL_LOCALIZE_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to combine libsignal_ffi.a into a relocatable object")
      endif()
      execute_process(
        COMMAND ${CMAKE_OBJCOPY} -w
                --keep-global-symbol "signal_*"
                --weaken-symbol=posix_spawn_file_actions_addchdir_np
                ${LIBSIGNAL_FFI_OBJ}
        RESULT_VARIABLE LIBSIGNAL_LOCALIZE_RESULT)
      if(NOT LIBSIGNAL_LOCALIZE_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to isolate libsignal_ffi symbols")
      endif()
    endif()
    file(REMOVE ${LIBSIGNAL_FFI_LOCAL})
    execute_process(
      COMMAND ${CMAKE_AR} rcs ${LIBSIGNAL_FFI_LOCAL} ${LIBSIGNAL_FFI_OBJ}
      RESULT_VARIABLE LIBSIGNAL_LOCALIZE_RESULT)
    if(NOT LIBSIGNAL_LOCALIZE_RESULT EQUAL 0)
      message(FATAL_ERROR "Failed to repackage localized libsignal_ffi.a")
    endif()
    file(REMOVE ${LIBSIGNAL_FFI_OBJ})
  endif()
  # Export the isolated archive for the parent CMakeLists to link
  set(LIBSIGNAL_FFI_PATH ${LIBSIGNAL_FFI_LOCAL} PARENT_SCOPE)
else()
  # Export libsignal path for parent CMakeLists to link
  set(LIBSIGNAL_FFI_PATH ${LIBSIGNAL_FFI_FILE} PARENT_SCOPE)
endif()
