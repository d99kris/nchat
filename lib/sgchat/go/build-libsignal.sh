#!/usr/bin/env bash

# build-libsignal.sh
#
# Builds libsignal_ffi.a from source
#
# Dependencies: Rust toolchain (cargo), git, cmake, clang, protobuf compiler
#
# Usage: build-libsignal.sh <output-dir>

set -e

OUTPUT_DIR="${1:?Usage: build-libsignal.sh <output-dir>}"
LIBSIGNAL_SRC_DIR="${OUTPUT_DIR}/libsignal-src"

# Check dependencies
MISSING=""
for CMD in cargo git cmake clang protoc; do
  if ! command -v "${CMD}" &> /dev/null; then
    MISSING="${MISSING} ${CMD}"
  fi
done
if [ -n "${MISSING}" ]; then
  echo "Error: missing required dependencies:${MISSING}"
  echo ""
  echo "Install them before using --build-libsignal:"
  echo "  cargo   - curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh"
  echo "  git     - install via system package manager"
  echo "  cmake   - install via system package manager"
  echo "  clang   - install via system package manager"
  echo "  protoc  - install protobuf compiler via system package manager"
  exit 1
fi

mkdir -p "${OUTPUT_DIR}"

# Determine required libsignal version from version.go
SCRIPT_DIR="$(cd "$(dirname "${0}")" && pwd)"
VERSION_FILE="${SCRIPT_DIR}/ext/signal/pkg/libsignalgo/version.go"
if [ ! -f "${VERSION_FILE}" ]; then
  echo "Error: ${VERSION_FILE} not found"
  exit 1
fi
LIBSIGNAL_VERSION=$(grep 'const Version' "${VERSION_FILE}" | sed 's/.*"\(.*\)".*/\1/')
if [ -z "${LIBSIGNAL_VERSION}" ]; then
  echo "Error: could not parse libsignal version from ${VERSION_FILE}"
  exit 1
fi
echo "Required libsignal version: ${LIBSIGNAL_VERSION}"

# Clone libsignal if not already present
if [ ! -d "${LIBSIGNAL_SRC_DIR}" ]; then
  echo "Clone libsignal"
  git clone --depth 1 https://github.com/signalapp/libsignal.git "${LIBSIGNAL_SRC_DIR}"
else
  echo "Use exiting clone"
fi

# Fetch desired version tag
cd "${LIBSIGNAL_SRC_DIR}"
git fetch --depth 1 origin tag "${LIBSIGNAL_VERSION}"
git checkout "${LIBSIGNAL_VERSION}"

# Build libsignal-ffi
echo "Building libsignal_ffi..."
cd "${LIBSIGNAL_SRC_DIR}"
RUSTFLAGS="-Ctarget-feature=-crt-static" RUSTC_WRAPPER="" cargo build -p libsignal-ffi --release

# Copy output
cp target/release/libsignal_ffi.a "${OUTPUT_DIR}/libsignal_ffi.a"
echo "Built libsignal_ffi.a in ${OUTPUT_DIR}"
