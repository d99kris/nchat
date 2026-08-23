#!/usr/bin/env bash

# smoke.sh
#
# Smoke-tests packaged nchat tarballs (produced by package.sh) to catch the
# class of bugs static builds exist to avoid: an unresolved dynamic symbol,
# a too-new glibc, a missing shared library, or a broken terminfo lookup.
#
# For each Linux tarball it extracts the tree once, then in a matrix of
# clean distro containers runs (fatal) `nchat --version` and `nchat --help`,
# plus a (non-fatal) TUI-init probe: launched with an empty HOME and no
# profiles, nchat runs initscr()/endwin() and prints "No profiles setup,
# exiting." — exercising ncurses/terminfo and a full clean lifecycle without
# any interactive input. macOS tarballs are checked natively on the host.
#
# musl tarballs additionally get a WhatsApp setup probe (once per tarball,
# in the alpine image): it drives `nchat -s` up to the QR pairing screen,
# exercising the Go c-archive runtime bring-up that segfaulted on musl
# before the patched-Go toolchain (issue #204, doc/MUSLGO.md). Reaching
# the QR banner is fatal-on-failure; actually receiving a QR code from
# WhatsApp servers additionally proves DNS/TLS from the static binary and
# is reported non-fatally (qr:ok / qr:WARN) since it needs network.
#
# Only artifacts whose architecture matches the host are tested (no
# emulation); run this on a host of each target arch. Containers require
# docker; a glibc artifact skips the musl-only alpine image.
#
# Usage:
#   utils/dist/smoke.sh                     smoke-test every dist/*.tar.gz
#   utils/dist/smoke.sh <tarball|target>... only the named artifact(s)
#                                             (target e.g. linux-x86_64-glibc)
#
# Env:
#   NCHAT_SMOKE_CONTAINERS  space-separated docker images (override matrix)
#   NCHAT_SMOKE_TERM        TERM for the TUI-init probe (default xterm-256color)
#   NCHAT_SMOKE_TUI=0       skip the TUI-init probe
#   NCHAT_SMOKE_WA=0        skip the WhatsApp setup probe (musl tarballs)
#   NCHAT_SMOKE_WA_IMAGE    image for the WhatsApp probe (default alpine:latest)
#
# nchat is distributed under the MIT license, see LICENSE for details.

set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
DIST_DIR="${REPO_DIR}/dist"

# Known packaging targets, longest-suffix match first so the version prefix
# (which itself may contain dashes) never confuses target extraction.
KNOWN_TARGETS=(
  linux-x86_64-musl linux-arm64-musl
  linux-x86_64-glibc linux-arm64-glibc
  macos-arm64
)

DEFAULT_CONTAINERS=(ubuntu:20.04 ubuntu:24.04 debian:11 fedora:latest rockylinux:8 alpine:latest)
if [[ -n "${NCHAT_SMOKE_CONTAINERS:-}" ]]; then
  read -r -a LINUX_CONTAINERS <<< "${NCHAT_SMOKE_CONTAINERS}"
else
  LINUX_CONTAINERS=("${DEFAULT_CONTAINERS[@]}")
fi
SMOKE_TERM="${NCHAT_SMOKE_TERM:-xterm-256color}"
TUI_PROBE="${NCHAT_SMOKE_TUI:-1}"
WA_PROBE="${NCHAT_SMOKE_WA:-1}"
WA_IMAGE="${NCHAT_SMOKE_WA_IMAGE:-alpine:latest}"

# The "no profiles" clean-exit sentinel printed after initscr()/endwin().
SENTINEL="No profiles setup, exiting."

norm_arch() {
  case "$1" in
    x86_64|amd64) echo x86_64 ;;
    aarch64|arm64) echo arm64 ;;
    *) echo "$1" ;;
  esac
}

HOST_OS="$(uname -s)"
HOST_ARCH="$(norm_arch "$(uname -m)")"

# target_of <tarball-basename> -> known target suffix on stdout (empty if
# none). Always exits 0 so `target=$(target_of ...)` is safe under set -e.
target_of() {
  local base="${1%.tar.gz}"
  local t
  for t in "${KNOWN_TARGETS[@]}"; do
    [[ "${base}" == *"-${t}" ]] && { echo "${t}"; return 0; }
  done
  return 0
}

# with_timeout <secs> <cmd...> — bounds a probe if a timeout tool exists.
with_timeout() {
  local secs="$1"; shift
  if command -v timeout >/dev/null 2>&1; then
    timeout "${secs}" "$@"
  elif command -v gtimeout >/dev/null 2>&1; then
    gtimeout "${secs}" "$@"
  else
    "$@"
  fi
}

# Resolve args to tarball paths (accept a path or a bare target name).
resolve_args() {
  local arg base match
  if [[ $# -eq 0 ]]; then
    # Only the runnable main tarballs (nchat-*). The detached-symbol tarballs are
    # named symbols-* precisely so this nchat-* glob skips them — they hold no
    # binary to smoke-test.
    shopt -s nullglob
    for f in "${DIST_DIR}"/nchat-*.tar.gz; do echo "${f}"; done
    shopt -u nullglob
    return
  fi
  for arg in "$@"; do
    if [[ -f "${arg}" ]]; then
      echo "${arg}"; continue
    fi
    # bare target name -> match a tarball in dist/
    match=""
    shopt -s nullglob
    for f in "${DIST_DIR}"/nchat-*-"${arg}".tar.gz; do match="${f}"; done
    shopt -u nullglob
    if [[ -n "${match}" ]]; then
      echo "${match}"
    else
      echo "$0: no tarball for '${arg}' under dist/" >&2
    fi
  done
}

PASS=0; FAIL=0; SKIP=0
declare -a SUMMARY=()

record() { SUMMARY+=("$1"); }

# run_core_linux <image> <platform> <pkg-root> — fatal --version/--help.
# pkg-root is the package top dir (holds bin/); mounted read-only at /opt/nchat.
run_core_linux() {
  local image="$1" platform="$2" root="$3" out rc
  out="$(docker run --rm --platform "${platform}" \
    -v "${root}:/opt/nchat:ro" "${image}" /bin/sh -c '
      set -e
      B=/opt/nchat/bin/nchat
      "$B" --version
      "$B" --help >/dev/null
    ' 2>&1)" && rc=0 || rc=$?
  if [[ ${rc} -eq 0 && "${out}" == *nchat* ]]; then
    return 0
  fi
  echo "${out}" | sed 's/^/      /'
  return 1
}

# run_tui_linux <image> <platform> <pkg-root> — non-fatal init probe.
run_tui_linux() {
  local image="$1" platform="$2" root="$3" out
  out="$(with_timeout 30 docker run --rm --platform "${platform}" \
    -e HOME=/tmp/h -e "TERM=${SMOKE_TERM}" \
    -v "${root}:/opt/nchat:ro" "${image}" /bin/sh -c '
      mkdir -p /tmp/h
      /opt/nchat/bin/nchat </dev/null 2>&1
    ' 2>&1)" || true
  [[ "${out}" == *"${SENTINEL}"* ]]
}

# run_wa_linux <image> <platform> <pkg-root> — WhatsApp setup probe (musl).
# Drives `nchat -s` up to the QR pairing screen and prints a result token:
#   qr      QR code received from WhatsApp servers (DNS/TLS path proven)
#   init    QR banner reached (Go runtime + client init OK), no QR (network?)
#   none    binary has no WhatsApp protocol compiled in
#   crash   setup died before the QR banner (the #204 failure mode)
run_wa_linux() {
  local image="$1" platform="$2" root="$3" out idx
  # Enumerate protocols: with stdin at EOF the selector aborts cleanly.
  out="$(docker run --rm --platform "${platform}" \
    -e HOME=/tmp/h -v "${root}:/opt/nchat:ro" "${image}" /bin/sh -c '
      mkdir -p /tmp/h
      /opt/nchat/bin/nchat -s </dev/null 2>&1
    ' 2>&1)" || true
  idx="$(printf '%s\n' "${out}" | sed -n 's/^\([0-9][0-9]*\)\. WhatsAppMd$/\1/p' | head -n1)"
  if [[ -z "${idx}" ]]; then
    echo "none"; return 0
  fi
  # Select WhatsApp, feed a dummy phone number, and let the Go client run up
  # to (and briefly into) the QR wait loop. The timeout must run inside the
  # container: timing out the docker client would leave the container behind.
  out="$(docker run --rm --platform "${platform}" \
    -e HOME=/tmp/h -v "${root}:/opt/nchat:ro" "${image}" /bin/sh -c "
      mkdir -p /tmp/h
      printf '%s\n%s\n' '${idx}' '+15551234567' |
        timeout 45 /opt/nchat/bin/nchat -s 2>&1
    " 2>&1)" || true
  # A rendered QR is made of half-block glyphs; the banner alone still proves
  # the Go c-archive runtime and whatsmeow/sqlite client init came up.
  if [[ "${out}" == *"▄"* || "${out}" == *"▀"* || "${out}" == *"█"* ]]; then
    echo "qr"
  elif [[ "${out}" == *"Scan the Qr code"* ]]; then
    echo "init"
  else
    printf '%s\n' "${out}" | tail -n 15 | sed 's/^/      /' >&2
    echo "crash"
  fi
  return 0
}

smoke_linux() {
  local tarball="$1" target="$2" arch="$3"
  local platform
  case "${arch}" in
    x86_64) platform="linux/amd64" ;;
    arm64) platform="linux/arm64" ;;
  esac

  if ! command -v docker >/dev/null 2>&1; then
    echo "  SKIP ${target}: docker not found (needed for Linux smoke tests)"
    record "SKIP ${target} (no docker)"; SKIP=$((SKIP + 1)); return
  fi

  local tree
  tree="$(mktemp -d)"
  if ! tar -xzf "${tarball}" -C "${tree}" 2>/dev/null; then
    echo "  FAIL ${target}: cannot extract tarball"
    record "FAIL ${target} (bad tarball)"; FAIL=$((FAIL + 1))
    rm -rf "${tree}"; return
  fi
  local root
  root="$(dirname "$(dirname "$(echo "${tree}"/*/bin/nchat)")")"
  if [[ ! -x "${root}/bin/nchat" ]]; then
    echo "  FAIL ${target}: no bin/nchat in tarball"
    record "FAIL ${target} (bad tarball)"; FAIL=$((FAIL + 1))
    rm -rf "${tree}"; return
  fi

  local image is_musl
  [[ "${target}" == *-musl ]] && is_musl=1 || is_musl=0
  for image in "${LINUX_CONTAINERS[@]}"; do
    # A glibc build cannot run on musl-only alpine; a musl static build runs
    # everywhere. Skip the impossible combo rather than reporting a failure.
    if [[ ${is_musl} -eq 0 && "${image}" == alpine* ]]; then
      echo "  SKIP ${target} @ ${image}: glibc build, alpine has no glibc"
      record "SKIP ${target} @ ${image} (glibc/alpine)"; SKIP=$((SKIP + 1))
      continue
    fi

    if run_core_linux "${image}" "${platform}" "${root}"; then
      local tui="n/a"
      if [[ "${TUI_PROBE}" != "0" ]]; then
        if run_tui_linux "${image}" "${platform}" "${root}"; then
          tui="tui:ok"
        else
          tui="tui:WARN"
        fi
      fi
      echo "  PASS ${target} @ ${image}  (${tui})"
      record "PASS ${target} @ ${image} ${tui}"; PASS=$((PASS + 1))
    else
      echo "  FAIL ${target} @ ${image}"
      record "FAIL ${target} @ ${image}"; FAIL=$((FAIL + 1))
    fi
  done

  # WhatsApp setup probe, musl tarballs only (see header): the Go c-archive
  # runtime used to segfault at startup on musl (#204); reaching the QR
  # banner is the fatal pass bar, an actual QR additionally proves network.
  if [[ ${is_musl} -eq 1 && "${WA_PROBE}" != "0" ]]; then
    case "$(run_wa_linux "${WA_IMAGE}" "${platform}" "${root}")" in
      qr)
        echo "  PASS ${target} wa-setup @ ${WA_IMAGE}  (qr:ok)"
        record "PASS ${target} wa-setup @ ${WA_IMAGE} qr:ok"; PASS=$((PASS + 1)) ;;
      init)
        echo "  PASS ${target} wa-setup @ ${WA_IMAGE}  (qr:WARN no QR received; no network?)"
        record "PASS ${target} wa-setup @ ${WA_IMAGE} qr:WARN"; PASS=$((PASS + 1)) ;;
      none)
        echo "  SKIP ${target} wa-setup: no WhatsApp protocol in binary"
        record "SKIP ${target} wa-setup (no WhatsApp)"; SKIP=$((SKIP + 1)) ;;
      *)
        echo "  FAIL ${target} wa-setup @ ${WA_IMAGE} (died before QR banner — see doc/MUSLGO.md / #204)"
        record "FAIL ${target} wa-setup @ ${WA_IMAGE}"; FAIL=$((FAIL + 1)) ;;
    esac
  fi

  rm -rf "${tree}"
}

smoke_macos() {
  local tarball="$1" target="$2"
  local tree root out rc
  tree="$(mktemp -d)"
  if ! tar -xzf "${tarball}" -C "${tree}" 2>/dev/null; then
    echo "  FAIL ${target}: cannot extract tarball"
    record "FAIL ${target} (bad tarball)"; FAIL=$((FAIL + 1))
    rm -rf "${tree}"; return
  fi
  root="$(dirname "$(dirname "$(echo "${tree}"/*/bin/nchat)")")"
  if [[ ! -x "${root}/bin/nchat" ]]; then
    echo "  FAIL ${target}: no bin/nchat in tarball"
    record "FAIL ${target} (bad tarball)"; FAIL=$((FAIL + 1))
    rm -rf "${tree}"; return
  fi

  out="$("${root}/bin/nchat" --version 2>&1)" && rc=0 || rc=$?
  if [[ ${rc} -ne 0 || "${out}" != *nchat* ]]; then
    echo "${out}" | sed 's/^/      /'
    echo "  FAIL ${target} @ host (--version)"
    record "FAIL ${target} @ host"; FAIL=$((FAIL + 1))
    rm -rf "${tree}"; return
  fi
  "${root}/bin/nchat" --help >/dev/null 2>&1 || {
    echo "  FAIL ${target} @ host (--help)"
    record "FAIL ${target} @ host"; FAIL=$((FAIL + 1))
    rm -rf "${tree}"; return
  }

  local tui="n/a"
  if [[ "${TUI_PROBE}" != "0" ]]; then
    local home
    home="$(mktemp -d)"
    out="$(HOME="${home}" TERM="${SMOKE_TERM}" \
      with_timeout 30 "${root}/bin/nchat" </dev/null 2>&1 || true)"
    [[ "${out}" == *"${SENTINEL}"* ]] && tui="tui:ok" || tui="tui:WARN"
    rm -rf "${home}"
  fi
  echo "  PASS ${target} @ host  (${tui})"
  record "PASS ${target} @ host ${tui}"; PASS=$((PASS + 1))

  rm -rf "${tree}"
}

TARBALLS=()
while IFS= read -r line; do
  [[ -n "${line}" ]] && TARBALLS+=("${line}")
done < <(resolve_args "$@")
if [[ ${#TARBALLS[@]} -eq 0 ]]; then
  echo "$0: no tarballs to test (run package.sh first)" >&2
  exit 1
fi

for tarball in "${TARBALLS[@]}"; do
  base="$(basename "${tarball}")"
  target="$(target_of "${base}")"
  if [[ -z "${target}" ]]; then
    echo "skip ${base}: unrecognized target"
    record "SKIP ${base} (unknown target)"; SKIP=$((SKIP + 1)); continue
  fi

  echo "== ${base} =="
  case "${target}" in
    macos-*)
      if [[ "${HOST_OS}" != "Darwin" || "${HOST_ARCH}" != "arm64" ]]; then
        echo "  SKIP ${target}: run on a macOS arm64 host"
        record "SKIP ${target} (needs macOS arm64 host)"; SKIP=$((SKIP + 1))
        continue
      fi
      smoke_macos "${tarball}" "${target}"
      ;;
    linux-*)
      arch="arm64"; [[ "${target}" == *x86_64* ]] && arch="x86_64"
      if [[ "${arch}" != "${HOST_ARCH}" ]]; then
        echo "  SKIP ${target}: ${arch} artifact, host is ${HOST_ARCH} (run on a ${arch} host)"
        record "SKIP ${target} (arch ${arch} != host ${HOST_ARCH})"; SKIP=$((SKIP + 1))
        continue
      fi
      smoke_linux "${tarball}" "${target}" "${arch}"
      ;;
  esac
done

echo
echo "== smoke summary =="
for line in "${SUMMARY[@]}"; do echo "  ${line}"; done
echo "  ---"
echo "  pass ${PASS}  fail ${FAIL}  skip ${SKIP}"
[[ ${FAIL} -eq 0 ]]
