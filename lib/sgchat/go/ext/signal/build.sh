#!/bin/sh
set -e
./build-rust.sh
cp -f pkg/libsignalgo/libsignal/target/release/libsignal_ffi.a .
LIBRARY_PATH=.:$LIBRARY_PATH ./build-go.sh
