#!/usr/bin/env bash

# package.sh
#
# Assembles distributable tarballs from the staged install trees produced
# by build-linux.sh / build-macos.sh. For each staged tree
# dist/nchat-<target>/ it emits:
#
#   dist/nchat-<version>-<target>.tar.gz         (top dir: nchat-<version>-<target>/)
#   dist/nchat-<version>-<target>.tar.gz.sha256  (checksum sidecar)
#
# With NCHAT_DIST_BARE=1 it additionally emits (not published by default —
# the tarball is the complete/recommended artifact):
#
#   dist/nchat-<version>-<target>                 (the bare stripped bin/nchat)
#   dist/nchat-<version>-<target>.sha256          (checksum sidecar)
#
# The bare binary is a lower-friction, drop-into-a-bin-dir convenience: it
# carries no LICENSE or man page. musl bare binaries support Telegram and
# WhatsApp (Signal not supported), same as their tarball.
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
  trap 'rm -rf "${work}"' EXIT
  cp -R "${stage}" "${work}/${pkg}"
  # Surface the license notices at the archive top level for visibility; they
  # are also installed under share/doc/nchat/ by the CMake install rule.
  docdir="${work}/${pkg}/share/doc/nchat"
  for f in LICENSE THIRD_PARTY_LICENSES; do
    [[ -f "${docdir}/${f}" ]] && cp "${docdir}/${f}" "${work}/${pkg}/${f}"
  done
  tar -czf "${DIST_DIR}/${tarball}" -C "${work}" "${pkg}"
  rm -rf "${work}"
  trap - EXIT

  ( cd "${DIST_DIR}" && sha256 "${tarball}" > "${tarball}.sha256" )

  echo "packaged: dist/${tarball}"
  echo "          dist/${tarball}.sha256"

  # Optionally also emit the bare stripped binary on its own (opt-in, not
  # part of the default release artifact set).
  if [[ "${NCHAT_DIST_BARE:-0}" == "1" ]]; then
    cp "${stage}/bin/nchat" "${DIST_DIR}/${pkg}"
    chmod 0755 "${DIST_DIR}/${pkg}"
    ( cd "${DIST_DIR}" && sha256 "${pkg}" > "${pkg}.sha256" )
    echo "          dist/${pkg}"
    echo "          dist/${pkg}.sha256"
  fi
  packaged=$((packaged + 1))
done

if [[ ${packaged} -eq 0 ]]; then
  echo "$0: nothing packaged" >&2
  exit 1
fi

echo "$0: packaged ${packaged} artifact(s) for version ${VERSION}"
