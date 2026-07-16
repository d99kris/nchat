#!/usr/bin/env bash

# install.sh
#
# One-line installer for the portable, statically linked nchat release
# binaries. Detects the OS, CPU architecture and (on Linux) libc, picks the
# matching release artifact, verifies its checksum and installs it.
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/d99kris/nchat/master/utils/install.sh | bash
#
# Env:
#   NCHAT_VERSION   install a specific release instead of the latest
#                   (accepts "5.4.3" or "v5.4.3"); default: latest release
#   NCHAT_PREFIX    install prefix; default: ~/.local (installs
#                   $NCHAT_PREFIX/bin, $NCHAT_PREFIX/share/man,
#                   $NCHAT_PREFIX/share/doc). If the prefix is not writable, the
#                   installer stops and suggests re-running with sudo.
#   NCHAT_REPO      GitHub owner/name to fetch from; default: d99kris/nchat
#
# On glibc Linux (>= 2.28) the glibc build is selected; on musl Linux, on a
# glibc older than 2.28, or when the glibc version cannot be determined, the
# musl build; on macOS the arm64 build. The musl build supports Telegram and
# WhatsApp but NOT Signal, and WhatsApp requires /proc mounted (see
# doc/MUSLGO.md) -- so an old-glibc fallback to musl loses Signal support. The
# full tarball is fetched so the man page comes along.
#
# nchat is distributed under the MIT license, see LICENSE for details.

set -euo pipefail

REPO="${NCHAT_REPO:-d99kris/nchat}"

info() { printf '%s\n' "$*"; }
warn() { printf '%s\n' "warning: $*" >&2; }
err()  { printf '%s\n' "error: $*" >&2; }
die()  { err "$*"; exit 1; }

# Like die(), but also prints the underlying curl/wget diagnostic captured in
# FETCH_ERR (indented beneath the error line) when there is one, so a network
# failure surfaces the actually-useful cause -- a TLS handshake failure on an
# old distro, a DNS error, an HTTP 404 -- instead of swallowing it.
die_fetch() { # <message>
  err "$*"
  [[ -n "${FETCH_ERR}" ]] && printf '%s\n' "${FETCH_ERR}" | sed 's/^/  /' >&2
  exit 1
}

have() { command -v "$1" >/dev/null 2>&1; }

# --- HTTP helpers (curl preferred, wget fallback) --------------------------

# fetch_file stashes the tool's stderr in FETCH_ERR (rather than discarding it)
# so die_fetch can surface why a download failed. curl's -S keeps error text
# while -s hides progress; wget's -nv likewise keeps errors while dropping the
# progress report (-q would hide the errors too). The payload goes to the dest
# file, leaving stderr as the only output, so 2>&1 captures just diagnostics.
# The captured text is printed only on failure, so a successful fetch stays
# quiet.
FETCH_ERR=""

fetch_file() { # <url> <dest>
  local rc
  if have curl; then FETCH_ERR="$(curl -fsSL -o "$2" "$1" 2>&1)"; rc=$?
  elif have wget; then FETCH_ERR="$(wget -nv -O "$2" "$1" 2>&1)"; rc=$?
  else die "need curl or wget to download"; fi
  return "${rc}"
}

# Resolve the latest release tag (e.g. v5.17.6) into RESOLVED_TAG. Release asset
# names embed the version, so "/releases/latest/download/<asset>" cannot be used
# blind — the version has to be resolved first. The redirect that
# /releases/latest issues to /releases/tag/<tag> gives it without touching the
# rate-limited API; the wget path falls back to the API. On failure FETCH_ERR
# holds the underlying curl/wget diagnostic. The tag is returned via a global
# rather than stdout so the caller need not wrap this in a command substitution,
# whose subshell would discard the FETCH_ERR set here.
RESOLVED_TAG=""
resolve_latest_tag() {
  local eff tag errf rc body
  if have curl; then
    errf="$(mktemp)"
    eff="$(curl -fsSL -o /dev/null -w '%{url_effective}' \
      "https://github.com/${REPO}/releases/latest" 2>"${errf}")"; rc=$?
    FETCH_ERR="$(cat "${errf}")"; rm -f "${errf}"
    (( rc == 0 )) || return 1
    tag="${eff##*/tag/}"
    [[ -n "${tag}" && "${tag}" != "${eff}" ]] || return 1
    RESOLVED_TAG="${tag}"
  else
    body="$(mktemp)"
    if ! fetch_file "https://api.github.com/repos/${REPO}/releases/latest" "${body}"; then
      rm -f "${body}"; return 1
    fi
    tag="$(sed -nE 's/.*"tag_name" *: *"([^"]+)".*/\1/p' "${body}" | head -n1)"
    rm -f "${body}"
    [[ -n "${tag}" ]] || return 1
    RESOLVED_TAG="${tag}"
  fi
}

# --- platform detection ----------------------------------------------------

detect_os() {
  case "$(uname -s)" in
    Linux) echo linux ;;
    Darwin) echo macos ;;
    *) return 1 ;;
  esac
}

# Android/Termux reports uname -s=Linux and uname -m=aarch64 with no musl
# loader, so the detection below would pick the linux-arm64-glibc build — but
# Android uses bionic libc, so that binary cannot run there. Detect and refuse
# up front rather than installing something that won't start. `uname -o` prints
# "Android" on Termux (the same signal rustup et al rely on); the Termux-only
# env vars are belt-and-braces in case a custom uname hides it.
is_android() {
  [[ "$(uname -o 2>/dev/null)" == "Android" ]] && return 0
  [[ -n "${TERMUX_VERSION:-}" ]] && return 0
  case "${PREFIX:-}" in */com.termux/*) return 0 ;; esac
  return 1
}

detect_arch() {
  # Apple Silicon under a Rosetta shell reports x86_64; trust the CPU flag.
  if [[ "$(uname -s)" == "Darwin" ]] && \
     [[ "$(sysctl -n hw.optional.arm64 2>/dev/null || echo 0)" == "1" ]]; then
    echo arm64; return 0
  fi
  case "$(uname -m)" in
    x86_64|amd64) echo x86_64 ;;
    aarch64|arm64) echo arm64 ;;
    *) return 1 ;;
  esac
}

# Minimum glibc the release "glibc" binaries target: manylinux_2_28. The floor
# is enforced at build time (see the glibc-floor check in
# utils/dist/build-in-container.sh); keep these in sync with it.
GLIBC_FLOOR_MAJOR=2
GLIBC_FLOOR_MINOR=28

# Print the running glibc's "MAJOR.MINOR" (e.g. "2.31"), or nothing if it cannot
# be determined -- including on musl, where neither source yields a glibc
# version. getconf GNU_LIBC_VERSION is the cleanest source ("glibc 2.31"); ldd
# --version is the fallback, carrying the version as the last whitespace-
# separated token of its first line across distros ("ldd (GNU libc) 2.28",
# "ldd (Ubuntu GLIBC 2.35-0ubuntu3) 2.35").
glibc_version() {
  local line=""
  if have getconf; then line="$(getconf GNU_LIBC_VERSION 2>/dev/null || true)"; fi
  if [[ ! "${line##* }" =~ ^[0-9]+\.[0-9]+ ]] && have ldd; then
    line="$(ldd --version 2>/dev/null | head -n1 || true)"
  fi
  line="${line##* }"
  if [[ "${line}" =~ ^([0-9]+\.[0-9]+) ]]; then printf '%s' "${BASH_REMATCH[1]}"; fi
  return 0
}

# Sets DETECTED_LIBC to "glibc" or "musl". On the musl-fallback paths it also
# sets LIBC_NOTE to a short note explaining why, for the caller to print. These
# are returned via globals rather than stdout so the caller need not wrap this
# in a command substitution -- whose subshell would both discard the note and,
# were the note echoed alongside the result, fold it into the captured libc
# name (same reasoning as resolve_latest_tag).
DETECTED_LIBC=""
LIBC_NOTE=""
detect_libc() {
  LIBC_NOTE=""
  # Positive musl detection: musl's ldd --version prints "musl" (to stderr) and
  # its loader lives at /lib/ld-musl-*. Those systems take the musl build.
  if ldd --version 2>&1 | grep -qi musl; then DETECTED_LIBC=musl; return; fi
  if ls /lib/ld-musl-* >/dev/null 2>&1; then DETECTED_LIBC=musl; return; fi

  # Otherwise it is glibc, but the glibc release binary needs glibc >= 2.28. If
  # the running glibc is older than the floor -- or its version cannot be read
  # at all -- fall back to the fully static musl build, which bundles its own
  # libc and runs on any Linux. No positive musl detection is needed for this:
  # the musl artifact is the safe default whenever the glibc build might not
  # load. (nchat's musl build drops Signal support, hence the warning below.)
  local v major minor
  v="$(glibc_version)"
  if [[ -z "${v}" ]]; then
    LIBC_NOTE="could not determine glibc version, fall back to musl"
    DETECTED_LIBC=musl; return
  fi
  major="${v%%.*}"; minor="${v#*.}"; minor="${minor%%.*}"
  if (( major < GLIBC_FLOOR_MAJOR )) ||
     (( major == GLIBC_FLOOR_MAJOR && minor < GLIBC_FLOOR_MINOR )); then
    LIBC_NOTE="detected glibc ${v} < ${GLIBC_FLOOR_MAJOR}.${GLIBC_FLOOR_MINOR}, fall back to musl"
    DETECTED_LIBC=musl; return
  fi
  DETECTED_LIBC=glibc
}

# --- checksum --------------------------------------------------------------

verify_checksum() { # <dir> <file>  (expects an aggregate sha256sums.txt alongside)
  local dir="$1" file="$2" line
  # The release ships one sha256sums.txt covering every target's tarball. Pull
  # just this asset's line so verification does not fail over the other
  # targets' tarballs, which we did not download. $NF is the filename field;
  # strip a leading '*' (sha256sum's binary-mode marker) before matching.
  line="$(awk -v a="${file}" '{ n = $NF; sub(/^\*/, "", n); if (n == a) print }' \
    "${dir}/sha256sums.txt")"
  [[ -n "${line}" ]] || die "no checksum entry for ${file} in sha256sums.txt"
  if have sha256sum; then
    ( cd "${dir}" && printf '%s\n' "${line}" | sha256sum -c - >/dev/null )
  elif have shasum; then
    ( cd "${dir}" && printf '%s\n' "${line}" | shasum -a 256 -c - >/dev/null )
  else
    die "no sha256 tool available (need sha256sum or shasum) to verify the download"
  fi
}

# --- install target check --------------------------------------------------

# Writable if the target dir, or its nearest existing ancestor, is writable.
target_writable() { # <dir>
  local d="$1"
  while [[ ! -e "${d}" ]]; do d="$(dirname "${d}")"; done
  [[ -w "${d}" ]]
}

# --- main ------------------------------------------------------------------

main() {
  local os arch libc target version tag arg
  for arg in "$@"; do
    case "${arg}" in
      -h|--help)
        info "usage: install.sh  (see the header comment for env vars)"
        return 0 ;;
      *) die "unknown argument: ${arg}" ;;
    esac
  done
  is_android && die "Android/Termux is unsupported: the release binaries are glibc/musl Linux builds and cannot run under Android's bionic libc; build nchat from source instead"
  os="$(detect_os)" || die "unsupported OS: $(uname -s)"
  arch="$(detect_arch)" || die "unsupported architecture: $(uname -m)"

  case "${os}" in
    macos)
      [[ "${arch}" == "arm64" ]] || \
        die "macOS builds are Apple Silicon (arm64) only; Intel is not supported"
      target="macos-arm64"
      ;;
    linux)
      detect_libc
      libc="${DETECTED_LIBC}"
      [[ -n "${LIBC_NOTE}" ]] && info "note: ${LIBC_NOTE}"
      target="linux-${arch}-${libc}"
      if [[ "${libc}" == "musl" ]]; then
        warn "musl build: Signal not supported (Telegram and WhatsApp work); WhatsApp requires /proc mounted, see doc/MUSLGO.md"
      fi
      ;;
  esac

  if [[ -n "${NCHAT_VERSION:-}" ]]; then
    version="${NCHAT_VERSION#v}"
    tag="v${version}"
    info "installing pinned version ${version} (${target})"
  else
    resolve_latest_tag || die_fetch "could not resolve the latest release for ${REPO}"
    tag="${RESOLVED_TAG}"
    version="${tag#v}"
    info "latest release is ${tag} (${target})"
  fi

  local asset base srcdir
  asset="nchat-${version}-${target}.tar.gz"
  base="https://github.com/${REPO}/releases/download/${tag}"

  # tmp is intentionally NOT local: the EXIT trap fires after main() returns, so
  # a function-local would be out of scope by then — under `set -u` that both
  # errors ("tmp: unbound variable", nonzero exit on an otherwise-successful
  # install) and skips the cleanup. A script-scope var keeps the trap working.
  tmp="$(mktemp -d)"
  trap 'rm -rf "${tmp}"' EXIT

  info "downloading ${asset}"
  fetch_file "${base}/${asset}" "${tmp}/${asset}" \
    || die_fetch "download failed (no ${asset} in release ${tag}?): ${base}/${asset}"
  fetch_file "${base}/sha256sums.txt" "${tmp}/sha256sums.txt" \
    || die_fetch "download failed: ${base}/sha256sums.txt"

  info "verifying checksum"
  verify_checksum "${tmp}" "${asset}" || die "checksum verification failed for ${asset}"

  tar -xzf "${tmp}/${asset}" -C "${tmp}"
  srcdir="${tmp}/nchat-${version}-${target}"
  [[ -x "${srcdir}/bin/nchat" ]] || die "unexpected archive layout: ${srcdir}/bin/nchat missing"

  local prefix bindir mandir docdir
  if [[ -n "${NCHAT_PREFIX:-}" ]]; then
    prefix="${NCHAT_PREFIX%/}"
  else
    prefix="${HOME}/.local"
  fi
  bindir="${prefix}/bin"
  mandir="${prefix}/share/man"
  docdir="${prefix}/share/doc/nchat"

  if ! target_writable "${prefix}"; then
    err "install prefix ${prefix} is not writable by the current user"
    err "re-run with sudo, e.g.:"
    err "  curl -fsSL https://raw.githubusercontent.com/${REPO}/master/utils/install.sh | sudo NCHAT_PREFIX=${prefix} bash"
    die "or set NCHAT_PREFIX to a writable location (default: ~/.local)"
  fi

  info "installing nchat to ${bindir}"
  mkdir -p "${bindir}" "${mandir}/man1"
  cp "${srcdir}/bin/nchat" "${bindir}/nchat"
  chmod 0755 "${bindir}/nchat"

  if [[ -f "${srcdir}/share/man/man1/nchat.1" ]]; then
    info "installing nchat.1 man to ${mandir}/man1"
    cp "${srcdir}/share/man/man1/nchat.1" "${mandir}/man1/nchat.1"
    chmod 0644 "${mandir}/man1/nchat.1"
  fi

  # Install the license notices under share/doc/nchat, mirroring the CMake
  # static-build install rule and OS package-manager convention. The static
  # binary embeds third-party libraries, so keeping their combined attribution
  # beside the installed binary makes the install self-contained rather than
  # relying on the downloaded tarball, which this installer discards on exit.
  for f in LICENSE THIRD_PARTY_LICENSES; do
    if [[ -f "${srcdir}/share/doc/nchat/${f}" ]]; then
      info "installing ${f} to ${docdir}"
      mkdir -p "${docdir}"
      cp "${srcdir}/share/doc/nchat/${f}" "${docdir}/${f}"
      chmod 0644 "${docdir}/${f}"
    fi
  done

  # A curl/tar install sets no quarantine attribute and the binary is already
  # ad-hoc signed, so this is only a defensive belt-and-braces for the
  # browser-downloaded-then-unpacked edge case.
  if [[ "${os}" == "macos" ]] && have xattr; then
    xattr -dr com.apple.quarantine "${bindir}/nchat" 2>/dev/null || true
  fi

  info "successfully installed nchat"
  [[ "${target}" == *-musl ]] && info "musl build: Telegram and WhatsApp (Signal not supported; WhatsApp needs /proc mounted)"

  case ":${PATH}:" in
    *":${bindir}:"*) ;;
    *)
      info "note: ${bindir} is not on your PATH, add it:"
      info "  export PATH=\"${bindir}:\$PATH\""
      ;;
  esac
}

main "$@"
