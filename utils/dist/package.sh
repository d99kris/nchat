#!/usr/bin/env bash

# package.sh
#
# Assembles distributable tarballs from the staged install trees produced
# by build-linux.sh / build-macos.sh. For each staged tree
# dist/nchat-<target>/ it emits:
#
#   dist/nchat-<version>-<target>.tar.gz     (top dir: nchat-<version>-<target>/)
#   dist/symbols-<version>-<target>.tar.gz   detached debug symbols (if any)
#
# The build scripts leave the detached debug info beside the binary in the
# staged tree (bin/nchat.debug on Linux, bin/nchat.dSYM on macOS). package.sh
# pulls it out into a per-target symbols-<version>-<target>.tar.gz so the main
# tarball stays lean. That per-target tarball is an intermediate: each build job
# uploads its own, and the release workflow folds them into one cross-platform
# symbols-<version>.tar.gz (a <target>/ subdir per build) and publishes only
# that single combined tarball — download it and extract the matching target's
# symbols beside the installed binary. The symbols- prefix sorts it last on the release
# page (after every nchat-* asset and sha256sums.txt) — an obvious "not for
# regular users" bucket.
#
# With NCHAT_DIST_BARE=1 it additionally emits (not published by default —
# the tarball is the complete/recommended artifact):
#
#   dist/nchat-<version>-<target>          (the bare stripped bin/nchat)
#
# The bare binary is a lower-friction, drop-into-a-bin-dir convenience: it
# carries no LICENSE or man page. musl bare binaries support Telegram and
# WhatsApp (Signal not supported), same as their tarball.
#
# Alongside the artifacts it writes one aggregate checksum file covering
# everything packaged in the run (rewritten each run, `sha256sum -c` format
# with bare filenames so it verifies from within dist/):
#
#   dist/sha256sums.txt
#
# This mirrors the release layout: the published GitHub release ships a
# single sha256sums.txt (recreated across every target's tarball) rather than
# a per-asset sidecar. The name sorts after the nchat-* assets on the release
# page (unlike the old checksums.txt, which sorted above them).
#
# Version is derived from the git tag at HEAD (leading "v" stripped),
# falling back to NCHAT_VERSION in lib/common/src/version.h (so the
# tarball version matches what `nchat --version` reports). Override with
# NCHAT_DIST_VERSION.
#
# Usage:
#   utils/dist/package.sh                  package every staged tree in dist/
#   utils/dist/package.sh <target> ...     package only the named target(s)
#                                            e.g. linux-x86_64-glibc, macos-arm64
#
# nchat is distributed under the MIT license, see LICENSE for details.

set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
DIST_DIR="${REPO_DIR}/dist"

dist_version() {
  if [[ -n "${NCHAT_DIST_VERSION:-}" ]]; then
    echo "${NCHAT_DIST_VERSION}"
    return
  fi
  # A tag pointing exactly at HEAD marks a release build; use it verbatim
  # (minus a leading "v"). Untagged/dev builds fall back to version.h.
  local tag
  tag="$(git -C "${REPO_DIR}" describe --tags --exact-match 2>/dev/null || true)"
  if [[ -n "${tag}" ]]; then
    echo "${tag#v}"
    return
  fi
  awk -F'"' '/#define NCHAT_VERSION/ { print $2; exit }' \
    "${REPO_DIR}/lib/common/src/version.h"
}

sha256() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$@"
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$@"
  else
    echo "$0: no sha256 tool (need sha256sum or shasum)" >&2
    return 1
  fi
}

VERSION="$(dist_version)"
if [[ -z "${VERSION}" ]]; then
  echo "$0: unable to determine version" >&2
  exit 1
fi

# Collect target names from args, or auto-detect staged trees in dist/.
targets=("$@")
if [[ ${#targets[@]} -eq 0 ]]; then
  shopt -s nullglob
  for stage in "${DIST_DIR}"/nchat-*/; do
    [[ -f "${stage}bin/nchat" ]] || continue
    name="$(basename "${stage}")"   # nchat-<target>
    targets+=("${name#nchat-}")
  done
  shopt -u nullglob
fi

if [[ ${#targets[@]} -eq 0 ]]; then
  echo "$0: no staged install trees found under dist/ (run build-*.sh first)" >&2
  exit 1
fi

packaged=0
artifacts=()
for target in "${targets[@]}"; do
  stage="${DIST_DIR}/nchat-${target}"
  if [[ ! -f "${stage}/bin/nchat" ]]; then
    echo "$0: skipping ${target}: no staged tree at ${stage}" >&2
    continue
  fi

  pkg="nchat-${VERSION}-${target}"
  tarball="${pkg}.tar.gz"

  # Assemble the versioned top-level dir in a temp area so the staged tree
  # (reused by re-packaging) keeps its unversioned name.
  work="$(mktemp -d)"
  dbgwork="$(mktemp -d)"
  trap 'rm -rf "${work}" "${dbgwork}"' EXIT
  cp -R "${stage}" "${work}/${pkg}"

  # The detached debug info (bin/nchat.debug on Linux, bin/nchat.dSYM on macOS,
  # produced by the build scripts) rides in its own symbols-* tarball, not the
  # main one — move it out of the staged copy before packaging so the main
  # tarball stays lean. Absent (e.g. an older staged tree) it is simply skipped.
  dbgpkg="symbols-${VERSION}-${target}"
  mkdir -p "${dbgwork}/${dbgpkg}/bin"
  moved_debug=0
  for sym in nchat.debug nchat.dSYM; do
    if [[ -e "${work}/${pkg}/bin/${sym}" ]]; then
      cp -R "${work}/${pkg}/bin/${sym}" "${dbgwork}/${dbgpkg}/bin/${sym}"
      rm -rf "${work}/${pkg}/bin/${sym}"
      moved_debug=1
    fi
  done

  # Surface the license notices at the archive top level for visibility; they
  # are also installed under share/doc/nchat/ by the CMake install rule.
  docdir="${work}/${pkg}/share/doc/nchat"
  for f in LICENSE THIRD_PARTY_LICENSES; do
    [[ -f "${docdir}/${f}" ]] && cp "${docdir}/${f}" "${work}/${pkg}/${f}"
  done
  tar -czf "${DIST_DIR}/${tarball}" -C "${work}" "${pkg}"
  artifacts+=("${tarball}")
  echo "packaged: dist/${tarball}"

  if [[ "${moved_debug}" == "1" ]]; then
    dbgtar="${dbgpkg}.tar.gz"
    tar -czf "${DIST_DIR}/${dbgtar}" -C "${dbgwork}" "${dbgpkg}"
    artifacts+=("${dbgtar}")
    echo "          dist/${dbgtar}"
  fi

  rm -rf "${work}" "${dbgwork}"
  trap - EXIT

  # Optionally also emit the bare stripped binary on its own (opt-in, not
  # part of the default release artifact set).
  if [[ "${NCHAT_DIST_BARE:-0}" == "1" ]]; then
    cp "${stage}/bin/nchat" "${DIST_DIR}/${pkg}"
    chmod 0755 "${DIST_DIR}/${pkg}"
    artifacts+=("${pkg}")
    echo "          dist/${pkg}"
  fi
  packaged=$((packaged + 1))
done

if [[ ${packaged} -eq 0 ]]; then
  echo "$0: nothing packaged" >&2
  exit 1
fi

# One aggregate checksum file over everything packaged in this run, in
# `sha256sum -c` format with bare names (sha256 lists files in the order
# given, so sort for a stable, readable listing). The release recreates the
# same file across all targets' tarballs (see .github/workflows/release.yml).
( cd "${DIST_DIR}" && sha256 "${artifacts[@]}" | sort -k2 > sha256sums.txt )
echo "checksums: dist/sha256sums.txt"

echo "$0: packaged ${packaged} artifact(s) for version ${VERSION}"
