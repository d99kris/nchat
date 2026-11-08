#!/bin/bash

# Exit on error
set -e

# Store the libsignal directory path
LIBSIGNAL_DIRECTORY="libsignal"

# Check if libsignal directory exists
if [ ! -d "$LIBSIGNAL_DIRECTORY" ]; then
    echo "Error: Libsignal directory '$LIBSIGNAL_DIRECTORY' does not exist."
    exit 1
fi

# Store the current working directory
ORIGINAL_DIR="$(pwd)"

# Navigate to libsignal directory
cd "$LIBSIGNAL_DIRECTORY"

# Build libsignal
cargo build -p libsignal-ffi --release

# Navigate back to the original directory
cd "$ORIGINAL_DIR"

# Copy files from the libsignal directory
cp "${LIBSIGNAL_DIRECTORY}/target/release/libsignal_ffi.a" .
cp "${LIBSIGNAL_DIRECTORY}/swift/Sources/SignalFfi/signal_ffi.h" libsignal-ffi.h

./update-ffi-version.sh

echo "Files copied successfully."
