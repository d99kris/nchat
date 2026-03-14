Signal
======
In order to build nchat with Signal support one needs to enable the signal
protocol feature explicitly by setting:

    export NCHAT_CMAKEARGS="-DHAS_SIGNAL=ON"

One can set it in .bashrc (or equivalent) to always enable Signal in nchat
builds, or simply set it when building, example:

    NCHAT_CMAKEARGS="-DHAS_SIGNAL=ON" ./make.sh build

The rationale for currently not enabling Signal by default is that it
requires several additional dependencies and the build time increase
noticeable. Meanwhile the number of Signal users is small compared to
the number of Telegram and WhatsApp users.

libsignal
---------
Signal support requires the `libsignal_ffi` library. By default nchat
downloads a pre-built binary (supported on Linux and macOS, amd64 and
arm64). If the download is not available for the current platform, it
automatically falls back to building from source (requires Rust toolchain,
git, cmake, clang, and protobuf compiler).

To force building from source (skipping the download attempt):

    NCHAT_CMAKEARGS="-DHAS_SIGNAL=ON -DDOWNLOAD_LIBSIGNAL=OFF" ./make.sh build
