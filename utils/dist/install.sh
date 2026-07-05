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
# Env:
#   NCHAT_VERSION   install a specific release instead of the latest
#                   (accepts "5.4.3" or "v5.4.3"); default: latest release
#   PREFIX          install prefix; default: ~/.local (installs $PREFIX/bin,
#                   $PREFIX/share/man, $PREFIX/libexec). A non-writable prefix
#                   self-elevates with sudo.
#   NCHAT_REPO      GitHub owner/name to fetch from; default: d99kris/nchat
#
# On glibc Linux the glibc build is selected; on musl Linux the musl build
# (Telegram-only — no WhatsApp/Signal, see issue #204); on macOS the arm64
# build. The full tarball is fetched (so the man page comes along).
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

verify_checksum() { # <dir> <file>  (expects <file>.sha256 alongside)
  local dir="$1" file="$2"
  if have sha256sum; then
    ( cd "${dir}" && sha256sum -c "${file}.sha256" >/dev/null )
  elif have shasum; then
    ( cd "${dir}" && shasum -a 256 -c "${file}.sha256" >/dev/null )
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
  local os arch libc target version tag
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
        warn "musl system — this build is Telegram-only (no WhatsApp/Signal, issue #204)"
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
  fetch_file "${base}/${asset}.sha256" "${tmp}/${asset}.sha256" \
    || die "download failed: ${base}/${asset}.sha256"

  info "verifying checksum"
  verify_checksum "${tmp}" "${asset}" || die "checksum verification failed for ${asset}"

  tar -xzf "${tmp}/${asset}" -C "${tmp}"
  srcdir="${tmp}/nchat-${version}-${target}"
  [[ -x "${srcdir}/bin/nchat" ]] || die "unexpected archive layout: ${srcdir}/bin/nchat missing"

  local prefix bindir mandir libexecdir
  if [[ -n "${PREFIX:-}" ]]; then
    prefix="${PREFIX%/}"
  else
    prefix="${HOME}/.local"
  fi
  bindir="${prefix}/bin"
  mandir="${prefix}/share/man"
  libexecdir="${prefix}/libexec"

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

  # The binary invokes the auto-compose helper at <bin>/../libexec/nchat/compose;
  # install it too so that optional feature works from the installed prefix.
  if [[ -f "${srcdir}/libexec/nchat/compose" ]]; then
    run_priv mkdir -p "${libexecdir}/nchat"
    run_priv cp "${srcdir}/libexec/nchat/compose" "${libexecdir}/nchat/compose"
    run_priv chmod 0755 "${libexecdir}/nchat/compose"
  fi

  # A curl/tar install sets no quarantine attribute and the binary is already
  # ad-hoc signed, so this is only a defensive belt-and-braces for the
  # browser-downloaded-then-unpacked edge case.
  if [[ "${os}" == "macos" ]] && have xattr; then
    run_priv xattr -dr com.apple.quarantine "${bindir}/nchat" 2>/dev/null || true
  fi

  info "installed nchat ${version} to ${bindir}/nchat"
  [[ "${target}" == *-musl ]] && info "musl build: Telegram only (no WhatsApp/Signal)"

  case ":${PATH}:" in
    *":${bindir}:"*) ;;
    *)
      info "note: ${bindir} is not on your PATH — add it, e.g.:"
      info "  export PATH=\"${bindir}:\$PATH\""
      ;;
  esac
}

main "$@"
