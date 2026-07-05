LICENSEPLAN.md
==============

Plan for third-party license attribution in binary distributions of nchat.
Intended as input to a fresh session on a dedicated branch (after
`static-builds` is merged). Written 2026-07-05.

Background
----------
nchat has historically been distributed as source tarballs only, where
README.md's "Third-party Libraries" section provided attribution. With binary
distribution (static tarballs via `utils/dist/`), license texts must accompany
the binaries themselves:

- MIT / BSD-3 components require the copyright notice + license text alongside
  binary distributions (apathy, cereal, clip, QR-Code-generator, stb,
  sqlite_modern_cpp — all under `lib/ncutil/ext/`).
- tdlib is Boost (BSL-1.0), which exempts binaries from attribution — include
  it anyway by convention.
- whatsmeow is MPL-2.0: include license text + a pointer to where the source
  can be obtained (upstream repo + nchat repo, since it is carried in-tree).
- mautrix-signal / libsignal are AGPL-3.0: when Signal is compiled in, the
  combined distribution is subject to AGPL v3 (already reflected by the
  `#ifdef HAS_SIGNAL` branch in `ShowVersion()`, `src/main.cpp`). Without
  Signal, combined distribution is MIT. No current dist build includes Signal.
- Static dist builds additionally embed statically linked C libraries
  (OpenSSL is Apache-2.0 and requires its license text; also ncurses, zlib,
  etc.) and the transitive Go module trees of whatsmeow/signalmeow
  (protobuf BSD-3, go.mau.fi MPL bits, go-sqlite3, ...).

Design decisions (already settled — do not re-litigate)
-------------------------------------------------------
1. Per-component committed fragments, combined at build time. No license
   tooling in the normal build path; fragments are regenerated only by
   maintainers when dependencies change.
2. The combined file's content follows the enabled protocol set
   (`HAS_TELEGRAM` / `HAS_WHATSAPP` / `HAS_SIGNAL` CMake options), and its
   preamble states the combined-work license using the same rule as
   `ShowVersion()`: AGPL v3 if `HAS_SIGNAL`, else MIT.
3. No deduplication across fragments. Duplicate license texts between the
   wmchat and sgchat Go trees are legally harmless; fragments stay
   self-contained. If dedup is ever wanted, key it on (module, version) via
   section-marker lines — but this is explicitly out of scope now.
4. Within Go-generated fragments, keep one verbatim license text per module
   (each carries that module's copyright line, which MIT/BSD attribution
   requires). Do not collapse into "one MIT text + list of names".
5. Over-attribution is acceptable; under-attribution is the failure mode to
   avoid.

Files to create
---------------
Committed fragments (plain text, human-readable; each dependency section
starts with a header giving name, version/commit, upstream URL, copyright
line, then the verbatim license text):

- `lib/ncutil/THIRD_PARTY_LICENSES` — apathy, cereal, clip,
  QR-Code-generator, sqlite_modern_cpp, stb. Hand-written once from the
  LICENSE files already present under `lib/ncutil/ext/*/`. Always included.
- `lib/tgchat/THIRD_PARTY_LICENSES` — tdlib (Boost text from
  `lib/tgchat/ext/td/LICENSE_1_0.txt`). Included when `HAS_TELEGRAM`.
- `lib/wmchat/THIRD_PARTY_LICENSES` — whatsmeow + its transitive Go modules.
  Generated with `go-licenses` against `lib/wmchat/go`. Included when
  `HAS_WHATSAPP`.
- `lib/sgchat/THIRD_PARTY_LICENSES` — mautrix-signal/libsignal + Go module
  tree, full AGPL-3.0 text. Generated with `go-licenses` against
  `lib/sgchat/go`. Included when `HAS_SIGNAL`.
- `utils/dist/THIRD_PARTY_LICENSES.static` — statically linked C libraries
  (OpenSSL, ncurses, zlib, ...; enumerate from what build-linux.sh /
  build-macos.sh actually link statically). Appended only for static dist
  builds, NOT for regular source builds (a dynamically linked build does not
  distribute those libraries). Gate on `HAS_STATIC_EXTLIBS` or a dedicated
  option — check what the dist build scripts pass to CMake and pick the
  switch that matches "we embed these libs".

Preamble (two variants, small committed files or a configured template):
- MIT variant: "This distribution of nchat is subject to the MIT license",
  followed by nchat's own LICENSE text, then a note that depending on build
  options some listed components may not be present in a given binary.
- AGPL variant: same, but AGPL v3 as the combined-work license.

Generator script:
- `utils/dist/gen-third-party-licenses` (name flexible) — regenerates the two
  Go-based fragments using `go-licenses` (report/save against each Go module
  dir), normalizing output so regeneration is diff-stable. Maintainer-run
  only. The ncutil/tgchat fragments are maintained by hand (they change
  rarely).

Build integration
-----------------
1. CMake concatenation step: at configure/build time, concatenate
   preamble + selected fragments into
   `${CMAKE_CURRENT_BINARY_DIR}/THIRD_PARTY_LICENSES` based on
   HAS_TELEGRAM / HAS_WHATSAPP / HAS_SIGNAL (+ static-libs fragment when
   applicable). Plain `file(READ)`/`file(APPEND)` or `cat` via
   `add_custom_command` — must be trivially cheap, no external tools.
2. `install(FILES ...)` the combined file — and the top-level `LICENSE` — to
   `${CMAKE_INSTALL_DATADIR}/doc/nchat/` (i.e. `share/doc/nchat/`, the FHS
   location). Note: currently LICENSE is only copied into the macOS stage by
   `utils/dist/build-macos.sh:73` (`cp LICENSE ...`); once CMake installs
   both files, that ad-hoc copy can be dropped and Linux gets it for free.
3. `utils/dist/package.sh` packages staged install trees verbatim, so the
   tarballs pick the file up automatically once the install rule exists.
   Optionally also copy it to the archive top level next to LICENSE for
   visibility (match existing archive layout conventions).
4. Update README.md "Third-party Libraries" section to mention the
   THIRD_PARTY_LICENSES mechanism (keep the human-readable list; it can note
   that binary distributions carry the full texts in share/doc/nchat/).

Maintenance hooks
-----------------
- `utils/tdlib-update`, `utils/whatsmeow-update`, `utils/signal-update`:
  after updating, run (or print a reminder to run) the generator for the
  affected fragment, so dependency bumps and fragment updates land in the
  same commit.
- Optional CI freshness check: a job that reruns the generator for the Go
  fragments and `git diff --exit-code`s them, catching hand-bumped Go
  modules. Nice-to-have, not required for the first version.

Later / out of scope for the first pass
---------------------------------------
- `nchat --license` (or `--third-party-licenses`) flag printing the embedded
  combined file, so a bare binary separated from its tarball still carries
  attribution. Straightforward later: embed the same build-dir combined file
  (CMake-generated header or go:embed) so it always matches the build's
  protocol set. `ShowVersion()` could point at it.
- Deduplication across fragments (see decision 3).
- Signal-enabled binary distribution: if that ever happens, AGPL requires
  more than a notices file (source offer for the combined work) — revisit
  separately.

Verification checklist
----------------------
- Default source build (`./make.sh build`): combined file contains preamble
  (MIT) + ncutil + tgchat + wmchat fragments, no sgchat, no static fragment;
  `make install` places it in share/doc/nchat/.
- Build with `-DHAS_SIGNAL=ON`: preamble switches to AGPL, sgchat fragment
  included — matching `nchat --version` output.
- Build with `-DHAS_TELEGRAM=OFF` etc.: corresponding fragment absent.
- Dist build (build-linux.sh / build-macos.sh + package.sh): tarball contains
  the combined file including the static-libs fragment; content matches the
  target's protocol set (musl targets are Telegram-only per package.sh).
- Note: builds need env vars the agent shell lacks — the user runs builds
  themselves; prepare changes and ask them to compile/verify.
