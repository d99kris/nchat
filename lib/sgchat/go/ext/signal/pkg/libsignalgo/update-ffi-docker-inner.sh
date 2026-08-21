#!/bin/sh
cd /data
export RUSTFLAGS="-Ctarget-feature=-crt-static" RUSTC_WRAPPER=""
apk add --no-cache git make cmake protobuf-dev musl-dev g++ clang-dev
cd libsignal
cargo build -p libsignal-ffi --release
cd ..
mv libsignal/target/release/libsignal_ffi.a .
cp libsignal/swift/Sources/SignalFfi/signal_ffi.h libsignal-ffi.h
chown 1000:1000 libsignal_ffi.a libsignal-ffi.h version.go
