#!/usr/bin/env bash

# build-macos.sh
#
# Builds a portable nchat macOS arm64 binary and stages the install tree
# in dist/nchat-macos-arm64/. Third-party libraries are linked static
# from a source-built deps prefix (utils/dist/build-deps-macos.sh,
# invoked automatically; cached under ~/.cache/nchat-dist/); only system
# libraries and frameworks stay dynamic, with a macOS 12.0 deployment
# floor. The binary is ad-hoc codesigned — installs via curl/tar run
# as-is, browser-downloaded archives need the quarantine attribute
# cleared (xattr -d com.apple.quarantine) or right-click-open.
#
# Usage:
#   utils/dist/build-macos.sh
#
# nchat is distributed under the MIT license, see LICENSE for details.

set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]] || [[ "$(uname -m)" != "arm64" ]]; then
  echo "$0: must run on a macOS arm64 host" >&2
  exit 1
fi

# Exported (not just passed to cmake) so the cgo/clang invocations of the
# whatsmeow Go c-archive build target the same minimum OS version.
export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-12.0}"

TARGET="macos-arm64"
REPO_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
CACHE_DIR="${NCHAT_DIST_CACHE:-${HOME}/.cache/nchat-dist}"
DEPS="${CACHE_DIR}/deps-${TARGET}"
BUILD_DIR="${REPO_DIR}/build-dist/${TARGET}"
STAGE_DIR="${REPO_DIR}/dist/nchat-${TARGET}"

"${REPO_DIR}/utils/dist/build-deps-macos.sh"

# The static deps are built by autotools with debug info (autotools defaults
# CFLAGS/CXXFLAGS to "-g -O2"), so their .a archives carry DWARF. dsymutil would
# otherwise harvest all of it into nchat.dSYM via the debug map, dwarfing
# nchat's own frames. strip -S drops the debug map/DWARF but keeps the external
# symbol table, so the static link is unaffected and only nchat's own frames end
# up in the detached symbols. Idempotent; runs against the cached prefix each
# build. (openssl uses --libdir=lib and the rest default to lib on macOS, so a
# single lib/ sweep covers them.)
find "${DEPS}/lib" -name '*.a' -exec strip -S {} + 2>/dev/null || true

# tdlib compilation units need 2-4 GB each; cap jobs by physical memory
if [[ -z "${JOBS:-}" ]]; then
  MEM_GB=$(( $(sysctl -n hw.memsize) / 1073741824 ))
  JOBS=$(( MEM_GB / 2 ))
  [[ ${JOBS} -lt 1 ]] && JOBS=1
  [[ ${JOBS} -gt $(sysctl -n hw.ncpu) ]] && JOBS=$(sysctl -n hw.ncpu)
fi

GENERATOR=()
if command -v ninja &> /dev/null; then
  GENERATOR=(-G Ninja)
fi

# Configure from a clean build dir so dist builds are reproducible and no
# stale CMake cache entry leaks across runs; ccache (if installed) still
# accelerates recompiles. The *_ROOT_DIR args point the Darwin dependency
# detection in CMakeLists.txt at the static deps prefix instead of brew.
rm -rf "${BUILD_DIR}"
cmake -S "${REPO_DIR}" -B "${BUILD_DIR}" "${GENERATOR[@]}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DHAS_STATIC_EXTLIBS=ON \
  -DHAS_DEBUG_SYMBOLS=ON \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET}" \
  -DCMAKE_PREFIX_PATH="${DEPS}" \
  -DNCURSES_ROOT_DIR="${DEPS}" \
  -DOPENSSL_ROOT_DIR="${DEPS}" \
  -DSQLITE_ROOT_DIR="${DEPS}" \
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

# Collect DWARF into a detached .dSYM bundle before stripping. dsymutil reads the
# debug map in the binary plus the .o files still present in BUILD_DIR, and the
# bundle is matched to the binary by LC_UUID (which strip -x and the re-sign
# below both preserve). -g1 does not change codegen, so the stripped binary is
# what a plain Release build produced. Only nchat's own frames carry DWARF
# (HAS_DEBUG_SYMBOLS scopes -g1 to nchat's targets; tdlib, the Go c-archive and
# the strip -S'd static deps are DWARF-free). package.sh ships the .dSYM in a
# separate symbols tarball; install.sh --debug drops it back next to the
# installed binary (use lldb on macOS — gdb is unsupported on Apple Silicon).
dsymutil "${BIN}" -o "${BIN}.dSYM"

# Strip local symbols only (a full strip is not valid for a cgo binary),
# then re-sign: stripping invalidates the signature applied at install.
strip -x "${BIN}"
"${REPO_DIR}/utils/sign" "${REPO_DIR}/src/nchat.entitlements" "${BIN}"

file "${BIN}"

# Verify the "mostly static" contract holds: only system libraries and
# frameworks dynamic (no homebrew/macports/deps-prefix leakage).
echo "$0: verifying dynamic-link allowlist and deployment target..."

UNEXPECTED="$(otool -L "${BIN}" | tail -n +2 | awk '{print $1}' \
  | grep -vE '^(/usr/lib/|/System/Library/Frameworks/)' | sort -u || true)"
if [[ -n "${UNEXPECTED}" ]]; then
  echo "$0: ERROR: binary dynamically links unexpected libraries:" >&2
  echo "${UNEXPECTED}" | sed 's/^/  /' >&2
  exit 1
fi

MINOS="$(otool -l "${BIN}" | awk '/LC_BUILD_VERSION/ { f = 1 } f && /minos/ { print $2; exit }')"
if [[ "${MINOS}" != "${MACOSX_DEPLOYMENT_TARGET}" ]]; then
  echo "$0: ERROR: binary minos ${MINOS} does not match deployment target ${MACOSX_DEPLOYMENT_TARGET}" >&2
  exit 1
fi

if ! codesign --verify "${BIN}"; then
  echo "$0: ERROR: codesign verification failed" >&2
  exit 1
fi

echo "$0: macOS verification passed (minos ${MINOS}):"
otool -L "${BIN}" | tail -n +2 | sed 's/^[[:space:]]*/  /'

echo "artifact: dist/nchat-${TARGET}/bin/nchat"
