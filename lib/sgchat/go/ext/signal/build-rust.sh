#!/bin/sh
git submodule update --init
cd pkg/libsignalgo/libsignal && RUSTFLAGS="-Ctarget-feature=-crt-static" RUSTC_WRAPPER="" cargo build -p libsignal-ffi --profile=release
