WhatsApp on musl Systems (e.g. Alpine Linux)
============================================
On musl-based systems, WhatsApp support built with a stock Go toolchain
crashes at startup (nchat issue [#204](https://github.com/d99kris/nchat/issues/204),
caused by Go issue [#13492](https://github.com/golang/go/issues/13492)). The
fix ([Go PR #69325](https://github.com/golang/go/pull/69325)) is not yet part
of any Go release, so nchat carries it as a patch.

The pre-built musl release binaries are already built with a patched Go
toolchain, so WhatsApp works out of the box — no action needed.

When building nchat from source on a musl system, WhatsApp is automatically
disabled unless a patched Go toolchain is used. To enable it:

1. Install a Go toolchain matching the version noted in
   `utils/dist/patches/go-pr69325-musl-argv.patch` and apply that patch to
   its source tree:

       patch -p1 -d "$(go env GOROOT)" < utils/dist/patches/go-pr69325-musl-argv.patch

2. Set `GOTOOLCHAIN=local` in the build environment, so Go does not
   auto-download an unpatched toolchain.

3. Pass `-DHAS_MUSL_GO_PATCHED=ON` to cmake to confirm the toolchain is
   patched, e.g:

       mkdir -p build && cd build
       cmake -DHAS_MUSL_GO_PATCHED=ON .. && make -s

See `utils/dist/Dockerfile.alpine` for a complete working example.

Notes:

- Running nchat with WhatsApp on musl requires `/proc` to be mounted
  (true on any normal system, but not necessarily in a minimal chroot).
- Signal is not supported on musl.
