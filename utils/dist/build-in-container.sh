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

# musl cannot host nchat's cgo/Go protocols: the Go c-archive runtime
# faults during bringup under a static musl link (nchat issue #204), so a
# musl binary with WhatsApp/Signal segfaults at startup. Disable the Go
# protocols for musl and build a Telegram-only (plus dummy) static binary.
# glibc keeps WhatsApp and points CMake at the source-built static deps in
# /opt/nchat-deps (see Dockerfile.manylinux); only libc stays dynamic.
EXTRA_CMAKE_ARGS=()
LINKER_FLAGS=""
if [[ "${LIBC}" == "musl" ]]; then
  echo "$0: WARNING: musl target does not support cgo/Go protocols (issue" >&2
  echo "$0:          #204); disabling WhatsApp and Signal. The resulting" >&2
  echo "$0:          static binary supports Telegram and dummy only." >&2
  EXTRA_CMAKE_ARGS+=(-DHAS_WHATSAPP=OFF -DHAS_SIGNAL=OFF)
  # -static for a fully static link; -no-pie avoids static-pie edge cases
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
  -DCMAKE_EXE_LINKER_FLAGS="${LINKER_FLAGS}" \
  "${EXTRA_CMAKE_ARGS[@]}" \
  -DCMAKE_INSTALL_PREFIX="/" \
  -DCMAKE_INSTALL_MANDIR="share/man"

cmake --build "${BUILD_DIR}" -j "${JOBS}"

rm -rf "${STAGE_DIR}"
DESTDIR="${STAGE_DIR}" cmake --install "${BUILD_DIR}" --strip
rm -rf "${STAGE_DIR}/lib"
cp "${SRC}/LICENSE" "${STAGE_DIR}/LICENSE"

BIN="${STAGE_DIR}/bin/nchat"
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
