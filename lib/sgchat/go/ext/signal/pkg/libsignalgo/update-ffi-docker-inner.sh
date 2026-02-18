#!/bin/sh
cd /data
export RUSTFLAGS="-Ctarget-feature=-crt-static" RUSTC_WRAPPER=""
apk add --no-cache git make cmake protoc musl-dev g++ clang-dev cbindgen
cd libsignal
cargo build -p libsignal-ffi --release
cbindgen --profile release rust/bridge/ffi -o libsignal-ffi.h
cd ..
mv libsignal/target/release/libsignal_ffi.a .
mv libsignal/libsignal-ffi.h .
chown 1000:1000 libsignal_ffi.a libsignal-ffi.h version.go
