#!/usr/bin/env bash

# build-in-container.sh
#
# Runs inside the build container with the repo mounted at /src; invoked
# by utils/dist/build-linux.sh, not meant for direct host use. Builds
# nchat with static external libs and stages the install tree in
# dist/nchat-linux-<arch>-<libc>/.
#
# nchat is distributed under the MIT license, see LICENSE for details.

set -euo pipefail

LIBC="${1:?usage: build-in-container.sh <musl|glibc>}"

ARCH="$(uname -m)"
case "${ARCH}" in
  aarch64) ARCH="arm64" ;;
esac
TARGET="linux-${ARCH}-${LIBC}"

SRC="/src"
BUILD_DIR="${SRC}/build-dist/${TARGET}"
STAGE_DIR="${SRC}/dist/nchat-${TARGET}"

# tdlib compilation units need 2-4 GB each; cap jobs by available memory
if [[ -z "${JOBS:-}" ]]; then
  MEM_GB=$(( $(awk '/MemAvailable/ { print $2 }' /proc/meminfo) / 1048576 ))
  JOBS=$(( MEM_GB / 2 ))
  [[ ${JOBS} -lt 1 ]] && JOBS=1
  [[ ${JOBS} -gt $(nproc) ]] && JOBS=$(nproc)
fi

# musl: the image's Go toolchain carries the vendored Go PR #69325 patch
# (see Dockerfile.alpine / doc/MUSLGO.md), fixing the Go c-archive startup
# crash on musl (nchat issue #204). Pass -DHAS_MUSL_GO_PATCHED=ON so CMake
# keeps WhatsApp enabled (it auto-disables on musl otherwise). Signal stays
# OFF on musl (deferred; the glibc dist does not enable it either).
# glibc keeps WhatsApp and points CMake at the source-built static deps in
# /opt/nchat-deps (see Dockerfile.manylinux); only libc stays dynamic.
EXTRA_CMAKE_ARGS=()
LINKER_FLAGS=""
if [[ "${LIBC}" == "musl" ]]; then
  EXTRA_CMAKE_ARGS+=(-DHAS_MUSL_GO_PATCHED=ON)
  # -static: fully static (musl) link. -no-pie: link a classic non-PIE
  # executable so its code sits at fixed link-time addresses (also avoids
  # static-pie edge cases). That lets the musl crash handler's frame-pointer
  # backtrace (lib/ncutil/src/apputil.cpp) log absolute addresses that resolve
  # directly with `addr2line -e nchat.debug <addr>`, with no ASLR load-base
  # rebasing. Alpine's toolchain defaults to -pie, which under -static would
  # otherwise yield a static-PIE binary with ASLR-shifted, unresolvable
  # addresses. Pairs with -fno-omit-frame-pointer (CMakeLists.txt).
  LINKER_FLAGS="-static -no-pie"
elif [[ "${LIBC}" == "glibc" ]]; then
  DEPS="/opt/nchat-deps"
  EXTRA_CMAKE_ARGS+=(-DCMAKE_PREFIX_PATH="${DEPS}" -DOPENSSL_ROOT_DIR="${DEPS}")
  # HAS_STATIC_EXTLIBS adds -static-libstdc++/-static-libgcc; libc stays
  # dynamic (normal PIE dynamic link, so no -static/-no-pie here).
  # Put the static-deps prefix first on the link search path: clip links a
  # bare -lxcb and the manylinux base ships a dynamic libxcb.so in
  # /usr/lib64. -L is searched before system dirs, and the prefix holds only
  # libxcb.a, so xcb (and transitively Xau) link static as required.
  LINKER_FLAGS="-L${DEPS}/lib"
fi

# Stamp a build-id so the detached nchat.debug can also be matched to the binary
# by hash (and is debuginfod-ready); harmless alongside the .gnu_debuglink path
# that install.sh --debug actually relies on.
LINKER_FLAGS="${LINKER_FLAGS} -Wl,--build-id=sha1"

# Configure from a clean build dir so dist builds are reproducible and no
# stale CMake cache entry leaks across runs (the repo is bind-mounted, so
# ${BUILD_DIR} otherwise persists). ccache still accelerates recompiles.
# Note: ccache is wired in by lib/ncutil/CMakeLists.txt via a global
# RULE_LAUNCH_COMPILE; do NOT also set CMAKE_*_COMPILER_LAUNCHER here or
# ccache 4.x aborts with "Recursive invocation" (ccache ccache <compiler>).
rm -rf "${BUILD_DIR}"
cmake -S "${SRC}" -B "${BUILD_DIR}" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DHAS_STATIC_EXTLIBS=ON \
  -DHAS_DEBUG_SYMBOLS=ON \
  -DCMAKE_EXE_LINKER_FLAGS="${LINKER_FLAGS}" \
  "${EXTRA_CMAKE_ARGS[@]}" \
  -DCMAKE_INSTALL_PREFIX="/" \
  -DCMAKE_INSTALL_MANDIR="share/man"

cmake --build "${BUILD_DIR}" -j "${JOBS}"

rm -rf "${STAGE_DIR}"
DESTDIR="${STAGE_DIR}" cmake --install "${BUILD_DIR}"
rm -rf "${STAGE_DIR:?}/lib"
# LICENSE and the combined THIRD_PARTY_LICENSES are installed under
# share/doc/nchat/ by the CMake install rule; package.sh also surfaces them at
# the archive top level.

BIN="${STAGE_DIR}/bin/nchat"

# Split debug info out of the release binary: copy the DWARF into a detached
# nchat.debug sibling, strip the shipped binary to its release form (matching
# the former `cmake --install --strip`), then record a .gnu_debuglink so gdb
# auto-loads nchat.debug whenever it sits beside the binary. -g1 does not change
# codegen, so the stripped binary is byte-for-byte what a plain Release build
# produced. Only nchat's own frames carry DWARF (HAS_DEBUG_SYMBOLS scopes -g1 to
# nchat's targets; tdlib, the Go c-archive and the static deps are DWARF-free).
# package.sh ships nchat.debug in a separate symbols tarball; install.sh --debug
# drops it back next to the installed binary.
objcopy --only-keep-debug "${BIN}" "${BIN}.debug"
chmod 0644 "${BIN}.debug"
strip --strip-all "${BIN}"
( cd "${STAGE_DIR}/bin" && objcopy --add-gnu-debuglink="nchat.debug" "nchat" )

file "${BIN}"

# glibc target: verify the "mostly static" contract holds — nothing imports
# a glibc newer than 2.28, and only libc/libm/libpthread/libdl/librt/ld-linux
# remain dynamically linked (every third-party lib must be static).
if [[ "${LIBC}" == "glibc" ]]; then
  echo "$0: verifying glibc floor (<= 2.28) and dynamic-link allowlist..."

  BADVER="$(objdump -T "${BIN}" \
    | grep -oE 'GLIBC_[0-9]+\.[0-9]+(\.[0-9]+)?' \
    | sed 's/GLIBC_//' | sort -uV \
    | awk -F. '($1 > 2) || ($1 == 2 && $2 > 28)')"
  if [[ -n "${BADVER}" ]]; then
    echo "$0: ERROR: binary imports glibc symbols newer than 2.28:" >&2
    echo "${BADVER}" | sed 's/^/  GLIBC_/' >&2
    exit 1
  fi

  # linux-vdso and ld-linux are virtual/loader entries, not real deps.
  ALLOWED='^(libc|libm|libpthread|libdl|librt)\.so'
  UNEXPECTED="$(ldd "${BIN}" 2>/dev/null \
    | grep -oE '\b[a-zA-Z0-9_+-]+\.so[.0-9]*' \
    | grep -vE '^(linux-vdso|ld-linux)' \
    | grep -vE "${ALLOWED}" | sort -u || true)"
  if [[ -n "${UNEXPECTED}" ]]; then
    echo "$0: ERROR: binary dynamically links unexpected libraries:" >&2
    echo "${UNEXPECTED}" | sed 's/^/  /' >&2
    exit 1
  fi

  echo "$0: glibc verification passed:"
  ldd "${BIN}" | sed 's/^/  /'
fi
