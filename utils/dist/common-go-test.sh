#!/usr/bin/env bash

# common-go-test.sh
#
# Builds the combined WhatsApp+Signal Go c-archive (lib/gostat) on its own,
# as a quick verdict on a "Common go dependency mismatch" reported at the end
# of a normal build (utils/checkgodeps.cmake).
#
# That check is deliberately a warning, not an error: the combined module
# resolves every shared dependency by minimal version selection, i.e. the
# higher of the two pinned versions gets used for BOTH protocols. A divergence
# is therefore harmless as long as the lower-pinned side still compiles
# against the higher version, and only breaks where the two straddle an API
# break. Building the archive is the only way to tell the two apart.
#
# This is the cheap part of a static build: no cmake, no tdlib, no static
# dependency prefix, no final link — just the one thing a dependency
# divergence can break. A full static build (utils/dist/build-macos.sh,
# utils/dist/build-linux.sh) covers the rest and takes far longer.
#
# Needs a libsignal_ffi.a, which any local build with Signal enabled leaves in
# its build tree; the known locations are searched, else point the script at
# one with NCHAT_LIBSIGNAL_DIR.
#
# Usage:
#   utils/dist/common-go-test.sh
#
# Env:
#   NCHAT_LIBSIGNAL_DIR  dir holding libsignal_ffi.a (default: search build trees)
#   NCHAT_DIST_CACHE     cache root for the Go module cache (default ~/.cache/nchat-dist)
#
# nchat is distributed under the MIT license, see LICENSE for details.

set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
WM_GO_DIR="${REPO_DIR}/lib/wmchat/go"
SG_GO_DIR="${REPO_DIR}/lib/sgchat/go"
CACHE_DIR="${NCHAT_DIST_CACHE:-${HOME}/.cache/nchat-dist}"
WORK_DIR="${REPO_DIR}/build-dist/common-go-test"
STAGE_DIR="${WORK_DIR}/module"
OUT_DIR="${WORK_DIR}/out"

# Report which dependencies diverge, reusing the check the build runs, so the
# output stands on its own. Parsed back below to look up what MVS then picked;
# a message format change there just leaves that part silent.
DIVERGED=()
if command -v cmake > /dev/null 2>&1; then
  CHECK_OUT="$(cmake -DWM_GO_MOD="${WM_GO_DIR}/go.mod" -DSG_GO_MOD="${SG_GO_DIR}/go.mod" \
    -P "${REPO_DIR}/utils/checkgodeps.cmake" 2>&1)"
  if [[ -n "${CHECK_OUT}" ]]; then
    echo "${CHECK_OUT}"
    while IFS= read -r line; do
      [[ -n "${line}" ]] && DIVERGED+=("${line}")
    done < <(printf '%s\n' "${CHECK_OUT}" | sed -n 's/^-- Common go dependency mismatch \(.*\): .*$/\1/p')
  else
    echo "$0: no common go dependency mismatch between wmchat and sgchat"
  fi
fi

# libsignal_ffi.a is not built here (it is a Rust build, and unrelated to Go
# dependency resolution) - reuse the one a previous build already acquired.
# CMake puts it in <builddir>/lib/<sgchat/go|gostat>/libsignal/, so cover both
# the standalone Signal build (make.sh) and the combined one (dist builds).
LIBSIGNAL_DIR="${NCHAT_LIBSIGNAL_DIR:-}"
if [[ -z "${LIBSIGNAL_DIR}" ]]; then
  while IFS= read -r candidate; do
    LIBSIGNAL_DIR="$(dirname "${candidate}")"
    break
  done < <(find "${REPO_DIR}/build" "${REPO_DIR}/build-dist" "${REPO_DIR}/dbgbuild" \
             -name libsignal_ffi.a 2> /dev/null)
fi
if [[ -z "${LIBSIGNAL_DIR}" ]] || [[ ! -f "${LIBSIGNAL_DIR}/libsignal_ffi.a" ]]; then
  echo "$0: no libsignal_ffi.a found - run a build with Signal enabled first" >&2
  echo "$0: (./make.sh build with HAS_SIGNAL=ON), or set NCHAT_LIBSIGNAL_DIR" >&2
  exit 1
fi
echo "$0: using ${LIBSIGNAL_DIR}/libsignal_ffi.a"

# Prefer the module caches of the per-protocol build trees, so a run right
# after a normal build fetches little or nothing; the configured proxy (or
# direct) still covers whatever only the combined module needs - notably the
# higher version of a diverged dependency, which the lower-pinned side has no
# reason to have cached. The Go module cache itself is kept out of ~/go, next
# to the other dist build caches.
GOPROXY_LIST=""
for proto in wmchat sgchat; do
  proto_cache="${REPO_DIR}/build/lib/${proto}/go/pkg/mod/cache/download"
  if [[ -d "${proto_cache}" ]]; then
    GOPROXY_LIST="${GOPROXY_LIST}file://${proto_cache},"
  fi
done
GOPROXY_LIST="${GOPROXY_LIST}$(go env GOPROXY)"

# -modcacherw and -buildvcs=false mirror the flags CMake passes (see
# lib/gostat/CMakeLists.txt), -modcacherw notably keeping the module cache
# removable without chmod.
GO_ENV=(GOPATH="${CACHE_DIR}/gostat-gopath" GOPROXY="${GOPROXY_LIST}"
        CGO_ENABLED=1 LIBRARY_PATH="${LIBSIGNAL_DIR}")

rm -rf "${WORK_DIR}"
mkdir -p "${OUT_DIR}"
env "${GO_ENV[@]}" bash "${REPO_DIR}/lib/gostat/assemble.sh" \
  "${WM_GO_DIR}" "${SG_GO_DIR}" "${STAGE_DIR}" "${OUT_DIR}" \
  libgostat.a c-archive -modcacherw -buildvcs=false

# Report the version MVS settled on for each diverged dependency - the one both
# protocols just compiled against, and the answer to "which side won".
for module in "${DIVERGED[@]}"; do
  ( cd "${STAGE_DIR}" && env "${GO_ENV[@]}" go list -m -mod=mod "${module}" )
done

echo "$0: combined whatsapp+signal go c-archive builds"
echo "artifact: build-dist/common-go-test/out/libgostat.a"
