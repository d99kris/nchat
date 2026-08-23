Vendored Go toolchain patches
=============================
Patches applied to the Go toolchain used to build nchat's fully static (musl)
release binaries. See `../Dockerfile.alpine` for how they are applied and
`../../../doc/MUSLGO.md` for the user-facing story.

go-pr69325-musl-argv.patch
--------------------------
Fixes the Go c-archive startup segfault on musl libc: without it, a WhatsApp
(or Signal) musl build crashes the instant the Go runtime initializes, before
`main`. The patch makes the runtime read process metadata from `/proc/self/*`
instead of the argv/envp registers that glibc supplies but musl does not.

- Fix:        Go PR #69325  — <https://github.com/golang/go/pull/69325>
- Root cause: Go issue #13492 — <https://github.com/golang/go/issues/13492>
- Context:    nchat issue #204 — <https://github.com/d99kris/nchat/issues/204>

**Status (last checked 2026-07-05):** PR #69325 is still **open and unmerged**;
no released Go toolchain carries the fix, so nchat must keep vendoring this
patch. Subscribe to the PR / issue above to learn when that changes.

### Maintenance

**When the fix ships in a released Go** (PR #69325 merged and in a tagged Go
release) — remove all the machinery and go back to stock:

1. Raise the `go` directive in `lib/wmchat/go/go.mod` and `lib/sgchat/go/go.mod`
   to that release.
2. Delete this patch and revert `../Dockerfile.alpine` to a stock Go (drop the
   `patch -p1` step; `GOTOOLCHAIN=local` is no longer required to avoid an
   unpatched auto-download).
3. Drop the musl `-DHAS_MUSL_GO_PATCHED` gate from `CMakeLists.txt` and the
   corresponding instructions in `doc/MUSLGO.md` — the gate only exists while a
   patched toolchain is required.

**On every `go.mod` Go-version bump** (while the patch is still needed) — keep
the toolchain version-aligned with the code. Three places must agree, with this
invariant:

    go.mod `go` directive   <=   patch rebase target   ==   Dockerfile.alpine GO_VERSION

Today: `go 1.25.0`  <=  patch rebased onto `go1.26.3`  ==  `GO_VERSION=1.26.3`.

When you raise the `go.mod` directive past the current rebase target (or want a
newer toolchain):

1. Re-rebase this patch onto the new Go tag. The runtime files it touches drift
   between releases, so expect small context fixups. Two deviations from the raw
   upstream PR were already required on go >= 1.26 (the `getGodebugEarly`
   signature change, and an allocation-before-`mallocinit` crash worked around
   by `getGodebugEarlyFromProc`) — see the patch header and re-verify both.
2. Update the "Rebased onto ..." line in the patch header.
3. Re-pin `GO_VERSION` in `../Dockerfile.alpine` to the same tag.
4. Rebuild the Alpine image and re-run `../smoke.sh` for both
   `linux-x86_64-musl` and `linux-arm64-musl`; the `wa-setup` probe must reach
   `qr:ok`.

### Related follow-up: Signal on musl

Signal is **not** enabled on musl — it is forced off in `CMakeLists.txt`
(`Signal: OFF (Forced - not supported on musl)`). Signal is a second Go
c-archive that hits the *same* startup crash, so this patched toolchain is a
prerequisite for it, but enabling it needs its own connect/sync validation.
When that effort begins: widen the musl gate to `HAS_SIGNAL` alongside
`HAS_MUSL_GO_PATCHED`, then validate Signal's own path. Do not assume the
WhatsApp validation covers Signal.
