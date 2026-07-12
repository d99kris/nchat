#!/usr/bin/env bash

# install.sh
#
# One-line installer for the portable, statically linked nchat release
# binaries. Detects the OS, CPU architecture and (on Linux) libc, picks the
# matching release artifact, verifies its checksum and installs it.
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/d99kris/nchat/master/utils/dist/install.sh | bash
#
# Options (pass through the pipe with `| bash -s -- <opt>`):
#   --debug   also fetch the combined debug-symbols tarball and drop the detached
#             symbols next to the installed binary (nchat.debug beside it on
#             Linux, nchat.dSYM on macOS), so a debugger auto-loads them:
#             `gdb nchat` on Linux, `lldb nchat` on macOS (gdb is unsupported on
#             Apple Silicon). Only nchat's own frames symbolise; tdlib, the
#             WhatsApp Go archive and the static third-party libs are built
#             without debug info.
#
# Env:
#   NCHAT_VERSION   install a specific release instead of the latest
#                   (accepts "5.4.3" or "v5.4.3"); default: latest release
#   PREFIX          install prefix; default: ~/.local (installs $PREFIX/bin,
#                   $PREFIX/share/man). A non-writable prefix self-elevates
#                   with sudo.
#   NCHAT_REPO      GitHub owner/name to fetch from; default: d99kris/nchat
#
# On glibc Linux the glibc build is selected; on musl Linux the musl build
# (Telegram and WhatsApp — Signal not supported on musl; WhatsApp needs /proc
# mounted, see doc/MUSLGO.md); on macOS the arm64 build. The full tarball is
# fetched (so the man page comes along).
#
# nchat is distributed under the MIT license, see LICENSE for details.

set -euo pipefail

REPO="${NCHAT_REPO:-d99kris/nchat}"

info() { printf '%s\n' "install.sh: $*" >&2; }
warn() { printf '%s\n' "install.sh: warning: $*" >&2; }
err()  { printf '%s\n' "install.sh: error: $*" >&2; }
die()  { err "$*"; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

# --- HTTP helpers (curl preferred, wget fallback) --------------------------

fetch_stdout() { # <url>
  if have curl; then curl -fsSL "$1"
  elif have wget; then wget -qO- "$1"
  else die "need curl or wget to download"; fi
}

fetch_file() { # <url> <dest>
  if have curl; then curl -fsSL -o "$2" "$1"
  elif have wget; then wget -qO "$2" "$1"
  else die "need curl or wget to download"; fi
}

# Resolve the latest release tag (e.g. v5.17.6). Release asset names embed the
# version, so "/releases/latest/download/<asset>" cannot be used blind — the
# version has to be resolved first. The redirect that /releases/latest issues
# to /releases/tag/<tag> gives it without touching the rate-limited API; the
# wget path falls back to the API.
resolve_latest_tag() {
  local eff tag
  if have curl; then
    eff="$(curl -fsSL -o /dev/null -w '%{url_effective}' \
      "https://github.com/${REPO}/releases/latest")" || return 1
    tag="${eff##*/tag/}"
    [[ -n "${tag}" && "${tag}" != "${eff}" ]] || return 1
    printf '%s' "${tag}"
  else
    fetch_stdout "https://api.github.com/repos/${REPO}/releases/latest" \
      | sed -nE 's/.*"tag_name" *: *"([^"]+)".*/\1/p' | head -n1
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

detect_libc() {
  # ldd --version prints "musl" on musl systems (to stderr) and GNU libc info
  # on glibc. Fall back to the loader path, defaulting to glibc.
  if ldd --version 2>&1 | grep -qi musl; then echo musl; return; fi
  if ls /lib/ld-musl-* >/dev/null 2>&1; then echo musl; return; fi
  echo glibc
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

# --- privileged install helpers --------------------------------------------

# Writable if the target dir, or its nearest existing ancestor, is writable.
target_writable() { # <dir>
  local d="$1"
  while [[ ! -e "${d}" ]]; do d="$(dirname "${d}")"; done
  [[ -w "${d}" ]]
}

SUDO=""
run_priv() {
  if [[ -n "${SUDO}" ]]; then sudo "$@"; else "$@"; fi
}

# --- main ------------------------------------------------------------------

main() {
  local os arch libc target version tag want_debug=0 arg
  for arg in "$@"; do
    case "${arg}" in
      --debug) want_debug=1 ;;
      -h|--help)
        info "usage: install.sh [--debug]  (see the header comment for env vars)"
        return 0 ;;
      *) die "unknown argument: ${arg} (supported: --debug)" ;;
    esac
  done
  os="$(detect_os)" || die "unsupported OS: $(uname -s)"
  arch="$(detect_arch)" || die "unsupported architecture: $(uname -m)"

  case "${os}" in
    macos)
      [[ "${arch}" == "arm64" ]] || \
        die "macOS builds are Apple Silicon (arm64) only; Intel is not supported"
      target="macos-arm64"
      ;;
    linux)
      libc="$(detect_libc)"
      target="linux-${arch}-${libc}"
      if [[ "${libc}" == "musl" ]]; then
        warn "musl system — Signal not supported (Telegram and WhatsApp work); WhatsApp requires /proc mounted, see doc/MUSLGO.md"
      fi
      ;;
  esac

  if [[ -n "${NCHAT_VERSION:-}" ]]; then
    version="${NCHAT_VERSION#v}"
    tag="v${version}"
    info "installing pinned version ${version} (${target})"
  else
    tag="$(resolve_latest_tag)" || die "could not resolve the latest release for ${REPO}"
    version="${tag#v}"
    info "latest release is ${tag} (${target})"
  fi

  local asset base tmp srcdir
  asset="nchat-${version}-${target}.tar.gz"
  base="https://github.com/${REPO}/releases/download/${tag}"

  tmp="$(mktemp -d)"
  trap 'rm -rf "${tmp}"' EXIT

  info "downloading ${asset}"
  fetch_file "${base}/${asset}" "${tmp}/${asset}" \
    || die "download failed (no ${asset} in release ${tag}?): ${base}/${asset}"
  fetch_file "${base}/sha256sums.txt" "${tmp}/sha256sums.txt" \
    || die "download failed: ${base}/sha256sums.txt"

  info "verifying checksum"
  verify_checksum "${tmp}" "${asset}" || die "checksum verification failed for ${asset}"

  tar -xzf "${tmp}/${asset}" -C "${tmp}"
  srcdir="${tmp}/nchat-${version}-${target}"
  [[ -x "${srcdir}/bin/nchat" ]] || die "unexpected archive layout: ${srcdir}/bin/nchat missing"

  local prefix bindir mandir
  if [[ -n "${PREFIX:-}" ]]; then
    prefix="${PREFIX%/}"
  else
    prefix="${HOME}/.local"
  fi
  bindir="${prefix}/bin"
  mandir="${prefix}/share/man"

  if ! target_writable "${prefix}"; then
    if have sudo; then
      SUDO="sudo"
      info "${prefix} is not writable; using sudo"
    else
      die "${prefix} is not writable and sudo is unavailable; set PREFIX to a writable location"
    fi
  fi

  info "installing to ${bindir}/nchat"
  run_priv mkdir -p "${bindir}" "${mandir}/man1"
  run_priv cp "${srcdir}/bin/nchat" "${bindir}/nchat"
  run_priv chmod 0755 "${bindir}/nchat"

  if [[ -f "${srcdir}/share/man/man1/nchat.1" ]]; then
    run_priv cp "${srcdir}/share/man/man1/nchat.1" "${mandir}/man1/nchat.1"
    run_priv chmod 0644 "${mandir}/man1/nchat.1"
  fi

  # A curl/tar install sets no quarantine attribute and the binary is already
  # ad-hoc signed, so this is only a defensive belt-and-braces for the
  # browser-downloaded-then-unpacked edge case.
  if [[ "${os}" == "macos" ]] && have xattr; then
    run_priv xattr -dr com.apple.quarantine "${bindir}/nchat" 2>/dev/null || true
  fi

  # --debug: fetch the combined detached-symbols tarball and drop this target's
  # symbols beside the binary. On Linux the stripped binary's .gnu_debuglink
  # points gdb at nchat.debug; on macOS lldb matches nchat.dSYM by the binary's
  # LC_UUID.
  if [[ "${want_debug}" == "1" ]]; then
    local dbgasset dbgdir symname
    # The release ships one combined symbols tarball covering every target; it
    # holds a <target>/ subdir per build (nchat.debug on Linux, nchat.dSYM on
    # macOS). Fetch it and pull out just this target's symbols. It is larger than
    # a single target's symbols, but --debug is a rare crash-analysis step, so
    # the extra download is a fair trade for a single release asset.
    dbgasset="symbols-${version}.tar.gz"
    [[ "${os}" == "macos" ]] && symname="nchat.dSYM" || symname="nchat.debug"

    info "downloading ${dbgasset}"
    fetch_file "${base}/${dbgasset}" "${tmp}/${dbgasset}" \
      || die "download failed (no ${dbgasset} in release ${tag}?): ${base}/${dbgasset}"
    info "verifying checksum"
    verify_checksum "${tmp}" "${dbgasset}" || die "checksum verification failed for ${dbgasset}"

    tar -xzf "${tmp}/${dbgasset}" -C "${tmp}"
    dbgdir="${tmp}/symbols-${version}/${target}"
    [[ -e "${dbgdir}/${symname}" ]] \
      || die "unexpected debug archive layout: ${dbgdir}/${symname} missing"

    info "installing debug symbols to ${bindir}/${symname}"
    run_priv rm -rf "${bindir}/${symname}"
    run_priv cp -R "${dbgdir}/${symname}" "${bindir}/${symname}"
  fi

  info "installed nchat ${version} to ${bindir}/nchat"
  if [[ "${want_debug}" == "1" ]]; then
    if [[ "${os}" == "macos" ]]; then
      info "debug symbols installed; symbolise with lldb (gdb is unsupported on Apple Silicon):"
      info "  lldb ${bindir}/nchat"
    else
      info "debug symbols installed; a debugger auto-loads them via .gnu_debuglink:"
      info "  gdb ${bindir}/nchat"
    fi
  fi
  [[ "${target}" == *-musl ]] && info "musl build: Telegram and WhatsApp (Signal not supported; WhatsApp needs /proc mounted)"

  case ":${PATH}:" in
    *":${bindir}:"*) ;;
    *)
      info "note: ${bindir} is not on your PATH — add it, e.g.:"
      info "  export PATH=\"${bindir}:\$PATH\""
      ;;
  esac
}

main "$@"
