# libsignalgo
Go bindings for [libsignal](https://github.com/signalapp/libsignal).

## Installation
0. Install Rust. You may also need to install libclang-dev and cbindgen manually.
1. Clone [libsignal](https://github.com/signalapp/libsignal) somewhere.
2. Run `./update-ffi.sh <path to libsignal>` (this builds the library, regenerates the header, and copies them both here)
3. Copy `libsignal_ffi.a` to `/usr/lib/`.
   * Alternatively, set `LIBRARY_PATH` to the directory containing `libsignal_ffi.a`.
	 Something like this: `LIBRARY_PATH="$LIBRARY_PATH:./pkg/libsignalgo" ./build.sh`
4. Use like a normal Go library.

## Precompiled
You can find precompiled `libsignal_ffi.a`'s on
[mau.dev/tulir/gomuks-build-docker](https://mau.dev/tulir/gomuks-build-docker).
Direct links:
* [Linux amd64](https://mau.dev/tulir/gomuks-build-docker/-/jobs/artifacts/master/raw/libsignal_ffi.a?job=libsignal%20linux%20amd64)
* [Linux arm64](https://mau.dev/tulir/gomuks-build-docker/-/jobs/artifacts/master/raw/libsignal_ffi.a?job=libsignal%20linux%20arm64)
* [macOS arm64](https://mau.dev/tulir/gomuks-build-docker/-/jobs/artifacts/master/raw/libsignal_ffi.a?job=libsignal%20macos%20arm64)
