#!/usr/bin/env bash

# build-deps-macos.sh
#
# Builds pinned static third-party libraries for the macOS dist build
# into a cached prefix (~/.cache/nchat-dist/deps-macos-arm64, override
# base dir with NCHAT_DIST_CACHE). Sources are built from upstream
# release tarballs rather than homebrew kegs so anyone can reproduce
# the dist artifacts without a homebrew state dependency. Versions are
# kept in sync with utils/dist/Dockerfile.manylinux.
#
# Each package is skipped if its version stamp is already present in
# the prefix; remove the prefix to rebuild from scratch. Invoked
# automatically by utils/dist/build-macos.sh.
#
# nchat is distributed under the MIT license, see LICENSE for details.

set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]] || [[ "$(uname -m)" != "arm64" ]]; then
  echo "$0: must run on a macOS arm64 host" >&2
  exit 1
fi

# Deployment target for all dep builds; must match build-macos.sh so the
# final link does not mix objects with different minimum OS versions.
export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-12.0}"

CACHE_DIR="${NCHAT_DIST_CACHE:-${HOME}/.cache/nchat-dist}"
DEPS="${CACHE_DIR}/deps-macos-arm64"
JOBS="${JOBS:-$(sysctl -n hw.ncpu)}"

# Let configure scripts pick up already-built deps (zlib for libpng and
# libmagic, etc.) from the prefix.
export CPPFLAGS="-I${DEPS}/include"
export LDFLAGS="-L${DEPS}/lib"
export PKG_CONFIG_PATH="${DEPS}/lib/pkgconfig"

mkdir -p "${DEPS}"
WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT
cd "${WORK}"

# built <pkg>-<version>: true if the package stamp exists (skip build)
built() {
  if [[ -f "${DEPS}/.built-${1}" ]]; then
    echo "$(basename "$0"): ${1} already built, skipping"
    return 0
  fi
  echo "$(basename "$0"): building ${1}"
  return 1
}

# stamp <pkg>-<version>: mark package as built
stamp() {
  touch "${DEPS}/.built-${1}"
}

# fetch <url>: download and extract a release tarball in ${WORK}
fetch() {
  local url="$1" tarball="${1##*/}"
  curl -fsSL "${url}" -o "${tarball}"
  tar xf "${tarball}"
}

# zlib: needed by libpng, libmagic, tdlib.
ZLIB_VERSION=1.3.1
if ! built "zlib-${ZLIB_VERSION}"; then
  fetch "https://zlib.net/fossils/zlib-${ZLIB_VERSION}.tar.gz"
  ( cd "zlib-${ZLIB_VERSION}" &&
    ./configure --prefix="${DEPS}" --static &&
    make -j"${JOBS}" && make install )
  stamp "zlib-${ZLIB_VERSION}"
fi

# openssl: needed by tdlib (OPENSSL_USE_STATIC_LIBS). --libdir=lib keeps
# it out of lib64 so a single -L covers everything.
OPENSSL_VERSION=3.0.16
if ! built "openssl-${OPENSSL_VERSION}"; then
  fetch "https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz"
  ( cd "openssl-${OPENSSL_VERSION}" &&
    ./Configure --prefix="${DEPS}" --libdir=lib no-shared no-tests &&
    make -j"${JOBS}" && make install_sw )
  stamp "openssl-${OPENSSL_VERSION}"
fi

# bzip2 / xz / zstd: libmagic's decompression backends.
BZIP2_VERSION=1.0.8
if ! built "bzip2-${BZIP2_VERSION}"; then
  fetch "https://sourceware.org/pub/bzip2/bzip2-${BZIP2_VERSION}.tar.gz"
  ( cd "bzip2-${BZIP2_VERSION}" &&
    make -j"${JOBS}" libbz2.a && make install PREFIX="${DEPS}" )
  stamp "bzip2-${BZIP2_VERSION}"
fi

XZ_VERSION=5.4.7
if ! built "xz-${XZ_VERSION}"; then
  fetch "https://github.com/tukaani-project/xz/releases/download/v${XZ_VERSION}/xz-${XZ_VERSION}.tar.gz"
  ( cd "xz-${XZ_VERSION}" &&
    ./configure --prefix="${DEPS}" --disable-shared --enable-static \
      --disable-xzdec --disable-lzmadec --disable-lzmainfo \
      --disable-scripts --disable-doc &&
    make -j"${JOBS}" && make install )
  stamp "xz-${XZ_VERSION}"
fi

ZSTD_VERSION=1.5.6
if ! built "zstd-${ZSTD_VERSION}"; then
  fetch "https://github.com/facebook/zstd/releases/download/v${ZSTD_VERSION}/zstd-${ZSTD_VERSION}.tar.gz"
  ( cd "zstd-${ZSTD_VERSION}" &&
    make -j"${JOBS}" -C lib install-static install-includes install-pc \
      PREFIX="${DEPS}" )
  stamp "zstd-${ZSTD_VERSION}"
fi

# libmagic (file): mime/type detection in lib/ncutil. Also ships magic.mgc.
FILE_VERSION=5.46
if ! built "file-${FILE_VERSION}"; then
  fetch "https://astron.com/pub/file/file-${FILE_VERSION}.tar.gz"
  ( cd "file-${FILE_VERSION}" &&
    ./configure --prefix="${DEPS}" --disable-shared --enable-static \
      --disable-libseccomp &&
    make -j"${JOBS}" && make install )
  stamp "file-${FILE_VERSION}"
fi

# ncursesw (+ tinfow). Unlike the Linux builds, no compiled-in terminfo
# fallbacks: /usr/share/terminfo is part of macOS so they are not needed,
# and generating them requires the ancient system tic, which cannot
# compile the modern terminfo.src. AWK is pinned to the system awk:
# config.status runs awk with the locale scrubbed (LC_ALL=C), and
# homebrew gawk 5.4 then silently emits nothing from mk-1st.awk, leaving
# the generated Makefiles without library rules ("No rule to make
# target '../lib/libtinfow.a'").
NCURSES_VERSION=6.5
if ! built "ncurses-${NCURSES_VERSION}"; then
  fetch "https://ftp.gnu.org/gnu/ncurses/ncurses-${NCURSES_VERSION}.tar.gz"
  ( cd "ncurses-${NCURSES_VERSION}" &&
    AWK=/usr/bin/awk ./configure --prefix="${DEPS}" \
      --without-shared --without-debug --without-ada --without-tests \
      --without-manpages --enable-widec --with-termlib --enable-pc-files \
      --with-pkg-config-libdir="${DEPS}/lib/pkgconfig" \
      --with-default-terminfo-dir=/usr/share/terminfo \
      --with-terminfo-dirs="/usr/share/terminfo:/etc/terminfo" \
      --disable-db-install &&
    make -j"${JOBS}" && make install )
  stamp "ncurses-${NCURSES_VERSION}"
fi

# sqlite3: message cache backend in lib/ncutil.
SQLITE_URL=https://www.sqlite.org/2024/sqlite-autoconf-3470200.tar.gz
SQLITE_TARBALL="${SQLITE_URL##*/}"
SQLITE_NAME="${SQLITE_TARBALL%.tar.gz}"
if ! built "${SQLITE_NAME}"; then
  fetch "${SQLITE_URL}"
  ( cd "${SQLITE_NAME}" &&
    ./configure --prefix="${DEPS}" --disable-shared --enable-static &&
    make -j"${JOBS}" && make install )
  stamp "${SQLITE_NAME}"
fi

# libpng: image clipboard support (needs zlib above).
LIBPNG_VERSION=1.6.44
if ! built "libpng-${LIBPNG_VERSION}"; then
  fetch "https://download.sourceforge.net/libpng/libpng-${LIBPNG_VERSION}.tar.gz"
  ( cd "libpng-${LIBPNG_VERSION}" &&
    ./configure --prefix="${DEPS}" --disable-shared --enable-static &&
    make -j"${JOBS}" && make install )
  stamp "libpng-${LIBPNG_VERSION}"
fi

echo "$(basename "$0"): all deps present in ${DEPS}"
