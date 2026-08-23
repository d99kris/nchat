#!/usr/bin/env bash

# build-linux.sh
#
# Builds a portable nchat Linux binary in a container and stages the
# install tree in dist/nchat-linux-<arch>-<libc>/. Requires docker.
#
# Usage:
#   utils/dist/build-linux.sh musl     fully static (Alpine/musl)
#   utils/dist/build-linux.sh glibc    mostly static (manylinux/glibc)
#
# Builds are native-arch only (no cross-compilation); run this script on
# a host of each target architecture. ccache and Go caches persist under
# ~/.cache/nchat-dist/ (override with NCHAT_DIST_CACHE).
#
# nchat is distributed under the MIT license, see LICENSE for details.

set -euo pipefail

usage() {
  echo "usage: $0 <musl|glibc>" >&2
  exit 1
}

LIBC="${1:-}"
case "${LIBC}" in
  musl) DOCKERFILE="Dockerfile.alpine" ;;
  glibc) DOCKERFILE="Dockerfile.manylinux" ;;
  *) usage ;;
esac

ARCH="$(uname -m)"
case "${ARCH}" in
  aarch64|arm64) ARCH="arm64"; PLATFORM="linux/arm64" ;;
  x86_64|amd64) ARCH="x86_64"; PLATFORM="linux/amd64" ;;
  *) echo "$0: unsupported host arch ${ARCH}" >&2; exit 1 ;;
esac

# glibc build uses the arch-specific manylinux base image.
BUILD_ARGS=()
if [[ "${LIBC}" == "glibc" ]]; then
  case "${ARCH}" in
    arm64) BASEIMAGE="quay.io/pypa/manylinux_2_28_aarch64" ;;
    x86_64) BASEIMAGE="quay.io/pypa/manylinux_2_28_x86_64" ;;
  esac
  BUILD_ARGS+=(--build-arg "BASEIMAGE=${BASEIMAGE}")
fi

TARGET="linux-${ARCH}-${LIBC}"
IMAGE="nchat-dist-${LIBC}-${ARCH}"
REPO_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
CACHE_DIR="${NCHAT_DIST_CACHE:-${HOME}/.cache/nchat-dist}"

# CI can restore a cached build image (docker load) and set
# NCHAT_DIST_SKIP_IMAGE_BUILD=1 to reuse it instead of rebuilding the
# source-compiled dependency layers (see .github/workflows/release.yml). The
# tag must already exist in the daemon; local builds leave it unset and always
# (re)build the image from the Dockerfile, so a Dockerfile edit is never missed.
if [[ -n "${NCHAT_DIST_SKIP_IMAGE_BUILD:-}" && "${NCHAT_DIST_SKIP_IMAGE_BUILD}" != "0" ]]; then
  if ! docker image inspect "${IMAGE}" >/dev/null 2>&1; then
    echo "$0: NCHAT_DIST_SKIP_IMAGE_BUILD set but image ${IMAGE} is not loaded" >&2
    exit 1
  fi
  echo "$0: reusing existing image ${IMAGE} (build skipped)"
else
  docker build --platform "${PLATFORM}" -t "${IMAGE}" \
    "${BUILD_ARGS[@]}" \
    -f "${REPO_DIR}/utils/dist/${DOCKERFILE}" "${REPO_DIR}/utils/dist"
fi

mkdir -p "${CACHE_DIR}/ccache-${TARGET}" "${CACHE_DIR}/go-${TARGET}"

# Forward the CI signal so the in-container CMake auto-detects build origin
# "github" (CMakeLists checks DEFINED ENV{GITHUB_ACTIONS}); docker run does not
# inherit the host environment. Pass it only when actually set -- an empty
# `-e GITHUB_ACTIONS=` still reads as DEFINED to CMake, which would mislabel
# local dist builds as "github".
RUN_ENV=()
if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
  RUN_ENV+=(-e "GITHUB_ACTIONS=${GITHUB_ACTIONS}")
fi

docker run --rm --platform "${PLATFORM}" \
  --user "$(id -u):$(id -g)" \
  -e HOME=/tmp \
  -e CCACHE_DIR=/cache/ccache \
  -e GOCACHE=/cache/go/build \
  -e GOMODCACHE=/cache/go/mod \
  -e JOBS="${JOBS:-}" \
  "${RUN_ENV[@]}" \
  -v "${REPO_DIR}:/src" \
  -v "${CACHE_DIR}/ccache-${TARGET}:/cache/ccache" \
  -v "${CACHE_DIR}/go-${TARGET}:/cache/go" \
  "${IMAGE}" /src/utils/dist/build-in-container.sh "${LIBC}"

echo "artifact: dist/nchat-${TARGET}/bin/nchat"
