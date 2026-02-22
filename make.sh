#!/usr/bin/env bash

# make.sh
#
# Copyright (C) 2020-2026 Kristofer Berggren
# All rights reserved.
#
# See LICENSE for redistribution information.

# helper functions
exiterr()
{
  >&2 echo "${1}"
  exit 1
}

show_usage()
{
  echo "usage: make.sh [OPTIONS] ACTION"
  echo ""
  echo "Options:"
  echo "  --no-telegram   - build without telegram support"
  echo "  --no-whatsapp   - build without whatsapp support"
  echo "  --yes,-y        - non-interactive mode, assume yes"
  echo ""
  echo "Action:"
  echo "  deps            - install project dependencies"
  echo "  build           - perform build"
  echo "  debug           - perform debug build"
  echo "  tests           - perform build and run tests"
  echo "  doc             - perform build and generate documentation"
  echo "  install         - perform build and install"
  echo "  all             - perform deps, build, tests, doc and install"
  echo "  src             - perform source code reformatting"
  echo "  bump            - perform version bump"
  echo ""
}

function version_ge() {
  test "$(printf '%s\n' "$@" | sort -V | head -n 1)" == "$2";
}

# process arguments
DEPS="0"
BUILD="0"
DEBUG="0"
TESTS="0"
DOC="0"
INSTALL="0"
SRC="0"
BUMP="0"
YES=""
CMAKEARGS="${NCHAT_CMAKEARGS:-}"

if [[ "${#}" == "0" ]]; then
  show_usage
  exit 1
fi

while [[ ${#} -gt 0 ]]; do
  case "${1%/}" in
    deps)
      DEPS="1"
      ;;

    build)
      BUILD="1"
      ;;

    debug)
      DEBUG="1"
      ;;

    test*)
      BUILD="1"
      TESTS="1"
      ;;

    doc)
      BUILD="1"
      DOC="1"
      ;;

    install)
      BUILD="1"
      INSTALL="1"
      ;;

    src)
      SRC="1"
      ;;

    bump)
      BUMP="1"
      ;;

    all)
      DEPS="1"
      BUILD="1"
      TESTS="1"
      DOC="1"
      INSTALL="1"
      ;;

    --no-telegram)
      CMAKEARGS="-DHAS_TELEGRAM=OFF ${CMAKEARGS}"
      ;;

    --no-whatsapp)
      CMAKEARGS="-DHAS_WHATSAPP=OFF ${CMAKEARGS}"
      ;;

    -y)
      YES="-y"
      ;;

    --yes)
      YES="-y"
      ;;

    *)
      show_usage
      exit 1
      ;;
  esac
  shift
done

# detect os / distro
OS="$(uname)"
if [ "${OS}" == "Linux" ]; then
  unset NAME
  eval $(grep "^NAME=" /etc/os-release 2> /dev/null)
  if [[ "${NAME}" != "" ]]; then
    DISTRO="${NAME}"
  else
    if [[ "${TERMUX_VERSION}" != "" ]]; then
      DISTRO="Termux"
    fi
  fi
fi

# deps
if [[ "${DEPS}" == "1" ]]; then
  if [ "${OS}" == "Linux" ]; then
    # add wl-clipboard package for wayland systems
    WL_CLIPBOARD=$([[ "$XDG_SESSION_TYPE" == "wayland" || -n "$WAYLAND_DISPLAY" ]] && echo "wl-clipboard")

    if [[ "${DISTRO}" == "Ubuntu" ]]; then
      sudo apt update && sudo apt ${YES} install ccache cmake build-essential gperf help2man libreadline-dev libssl-dev libncurses-dev libncursesw5-dev ncurses-doc zlib1g-dev libsqlite3-dev libmagic-dev ${WL_CLIPBOARD} || exiterr "deps failed (${DISTRO}), exiting."
      sudo apt ${YES} install golang-1.23 || exiterr "deps failed (${DISTRO} apt golang), exiting."
      sudo update-alternatives --install /usr/bin/go go /usr/lib/go-1.23/bin/go 123 || exiterr "deps failed (${DISTRO} select golang), exiting."
    elif [[ "${DISTRO}" == "Debian GNU/Linux" ]]; then
      sudo apt update && sudo apt ${YES} install ccache cmake build-essential gperf help2man libreadline-dev libssl-dev libncurses-dev libncursesw5-dev ncurses-doc zlib1g-dev libsqlite3-dev libmagic-dev ${WL_CLIPBOARD} || exiterr "deps failed (${DISTRO}), exiting."
      RELEASE=$(lsb_release -a | grep 'Codename:' | awk -F':' '{print $2}' | awk '{$1=$1;print}')
      if [[ "${RELEASE}" == "bookworm" ]]; then
        sudo apt install ${YES} -t bookworm-backports golang-1.23
        if [[ "${?}" != "0" ]]; then
          echo "Please ensure backports are enabled, see https://backports.debian.org/Instructions/#index2h2"
          exiterr "deps failed (${DISTRO} ${RELEASE}), exiting."
        fi
        sudo update-alternatives --install /usr/bin/go go /usr/lib/go-1.23/bin/go 123 || exiterr "deps failed (${DISTRO} select golang), exiting."
      elif [[ "${RELEASE}" == "unstable" ]]; then
        sudo apt ${YES} install golang || exiterr "deps failed (${DISTRO} ${RELEASE}), exiting."
      else
        echo "Unsupported ${DISTRO} version ${RELEASE}. Install golang-1.23 or newer manually, for example by running:"
        echo "wget https://go.dev/dl/go1.24.4.linux-amd64.tar.gz && sudo tar xf go.1.24.4.linux-amd64.tar.gz -C /usr/local"
        exiterr "deps failed (${DISTRO} ${RELEASE}), exiting."
      fi
    elif [[ "${DISTRO}" == "Raspbian GNU/Linux" ]] || [[ "${DISTRO}" == "Pop!_OS" ]]; then
      sudo apt update && sudo apt ${YES} install ccache cmake build-essential gperf help2man libreadline-dev libssl-dev libncurses-dev libncursesw5-dev ncurses-doc zlib1g-dev libsqlite3-dev libmagic-dev golang ${WL_CLIPBOARD} || exiterr "deps failed (${DISTRO}), exiting."
    elif [[ "${DISTRO}" == "Gentoo" ]]; then
      WL_CLIPBOARD=${WL_CLIPBOARD:+gui-apps/$WL_CLIPBOARD}
      sudo emerge -n dev-build/cmake dev-util/ccache dev-util/gperf sys-apps/help2man sys-libs/readline dev-libs/openssl sys-libs/ncurses sys-libs/zlib dev-db/sqlite sys-apps/file dev-lang/go ${WL_CLIPBOARD} || exiterr "deps failed (${DISTRO}), exiting."
    elif [[ "${DISTRO}" == "Fedora Linux" ]]; then
      sudo dnf ${YES} install git cmake clang golang ccache file-devel file-libs gperf readline-devel openssl-devel ncurses-devel sqlite-devel zlib-devel ${WL_CLIPBOARD} || exiterr "deps failed (${DISTRO}), exiting."
    elif [[ "${DISTRO}" == "Arch Linux" ]] || [[ "${DISTRO}" == "Arch Linux ARM" ]] || [[ "${DISTRO}" == "EndeavourOS" ]]; then
      sudo pacman -S ccache cmake file go gperf help2man ncurses openssl readline sqlite zlib base-devel ${WL_CLIPBOARD} || exiterr "deps failed (${DISTRO}), exiting."
    elif [[ "${DISTRO}" == "Void" ]]; then
      sudo xbps-install ${YES} base-devel go ccache cmake gperf help2man libmagick-devel readline-devel sqlite-devel file-devel openssl-devel ${WL_CLIPBOARD} || exiterr "deps failed (${DISTRO}), exiting."
    elif [[ "${DISTRO}" == "Alpine Linux" ]]; then
      sudo apk add git build-base cmake ncurses-dev openssl-dev sqlite-dev file-dev go linux-headers zlib-dev ccache gperf readline ${WL_CLIPBOARD} || exiterr "deps failed (${DISTRO}), exiting."
    elif [[ "${DISTRO}" == "openSUSE Tumbleweed" ]]; then
      sudo zypper install ${YES} -t pattern devel_C_C++ && sudo zypper install ${YES} go ccache cmake libopenssl-devel sqlite3-devel file-devel readline-devel ${WL_CLIPBOARD} || exiterr "deps failed (${DISTRO}), exiting."
    elif [[ "${DISTRO}" == "Chimera" ]]; then
      doas apk add git cmake clang go ccache gperf readline-devel openssl-devel ncurses-devel sqlite-devel zlib-devel file-devel ${WL_CLIPBOARD} || exiterr "deps failed (${DISTRO}), exiting."
    elif [[ "${DISTRO}" == "Rocky Linux" ]]; then
      eval $(grep "^VERSION_ID=" /etc/os-release 2> /dev/null)
      MAJOR="${VERSION_ID%%.*}"
      MAJOR="${MAJOR:-0}"
      EXTRA_REPOSITORY="powertools"
      if [[ ${MAJOR} -ge 9 ]]; then
        EXTRA_REPOSITORY="crb"
      fi
      sudo yum config-manager --set-enabled ${EXTRA_REPOSITORY} && sudo yum ${YES} groupinstall "Development Tools" && sudo yum ${YES} install git go cmake gperf readline-devel openssl-devel ncurses-devel zlib-devel sqlite-devel file-devel || exiterr "deps failed (${DISTRO}), exiting."
    elif [[ "${DISTRO}" == "Termux" ]]; then
      pkg install cmake clang golang ccache gperf file readline libsqlite openssl libandroid-wordexp || exiterr "deps failed (${DISTRO}), exiting."
    else
      exiterr "deps failed (unsupported linux distro ${DISTRO}), exiting."
    fi
  elif [ "${OS}" == "Darwin" ]; then
    if command -v brew &> /dev/null; then
      HOMEBREW_NO_INSTALL_UPGRADE=1 HOMEBREW_NO_AUTO_UPDATE=1 brew install go gperf cmake openssl ncurses ccache readline sqlite libmagic 2> >(grep -vE "already installed|To reinstall|brew reinstall" >&2) || exiterr "deps failed (${OS} brew), exiting."
    elif command -v port &> /dev/null; then
      sudo port -N install go gperf cmake openssl ncurses ccache readline sqlite3 libmagic || exiterr "deps failed (${OS} port), exiting."
    else
      exiterr "deps failed (${OS} missing brew and port), exiting."
    fi
  else
    exiterr "deps failed (unsupported os ${OS}), exiting."
  fi
fi

# src
if [[ "${SRC}" == "1" ]]; then
  go fmt lib/wmchat/go/*.go || exiterr "go fmt failed, exiting."
  go fmt lib/sgchat/go/*.go || exiterr "go fmt failed, exiting."
  uncrustify --update-config-with-doc -c etc/uncrustify.cfg -o etc/uncrustify.cfg && \
  uncrustify -c etc/uncrustify.cfg --replace --no-backup src/*.{cpp,h} \
             lib/common/src/*.h \
             lib/duchat/src/*.{cpp,h} \
             lib/ncutil/src/*.{cpp,h} \
             lib/sgchat/src/*.{cpp,h} \
             lib/tgchat/src/*.{cpp,h} \
             lib/wmchat/src/*.{cpp,h} \
    || exiterr "unrustify failed, exiting."
fi

# bump
if [[ "${BUMP}" == "1" ]]; then
  CURRENT_VERSION=$(grep NCHAT_VERSION lib/common/src/version.h | head -1 | awk -F'"' '{print $2}') # ex: 5.1.1
  CURRENT_MAJMIN="$(echo ${CURRENT_VERSION} | cut -d'.' -f1,2)" # ex: 5.1
  URL="https://github.com/d99kris/nchat.git"
  LATEST_TAG=$(git -c 'versionsort.suffix=-' ls-remote --tags --sort='v:refname' ${URL} | tail -n1 | cut -d'/' -f3)
  LATEST_VERSION=$(echo "${LATEST_TAG}" | cut -c2-) # ex: 5.1.3
  LATEST_MAJMIN="$(echo ${LATEST_VERSION} | cut -d'.' -f1,2)" # ex: 5.1
  SED="sed"
  if [[ "$(uname)" == "Darwin" ]]; then
    SED="gsed"
  fi
  if [[ "${CURRENT_MAJMIN}" == "${LATEST_MAJMIN}" ]]; then
    NEW_MAJ="$(echo ${CURRENT_VERSION} | cut -d'.' -f1)" # ex: 5
    let NEW_MIN=$(echo ${CURRENT_VERSION} | cut -d'.' -f2)+1
    NEW_PATCH="1" # use 1-based build/snapshot number
    NEW_VERSION="${NEW_MAJ}.${NEW_MIN}.${NEW_PATCH}"
    echo "Current:      ${CURRENT_MAJMIN} == ${LATEST_MAJMIN} Latest"
    echo "Bump release: ${NEW_VERSION}"
    ${SED} -i "s/^#define NCHAT_VERSION .*/#define NCHAT_VERSION \"${NEW_VERSION}\"/g" lib/common/src/version.h
  else
    NEW_MAJ="$(echo ${CURRENT_VERSION} | cut -d'.' -f1)" # ex: 5
    NEW_MIN="$(echo ${CURRENT_VERSION} | cut -d'.' -f2)" # ex: 1
    let NEW_PATCH=$(echo ${CURRENT_VERSION} | cut -d'.' -f3)+1
    NEW_VERSION="${NEW_MAJ}.${NEW_MIN}.${NEW_PATCH}"
    echo "Current:      ${CURRENT_MAJMIN} != ${LATEST_MAJMIN} Latest"
    echo "Bump build:   ${NEW_VERSION}"
    ${SED} -i "s/^#define NCHAT_VERSION .*/#define NCHAT_VERSION \"${NEW_VERSION}\"/g" lib/common/src/version.h
  fi
fi

# cmake args
if [[ "${BUILD}" == "1" ]] || [[ "${DEBUG}" == "1" ]]; then
  if [[ "${OS}" == "Linux" ]] && [[ "${DISTRO}" == "Termux" ]]; then
    CMAKEARGS="-DCMAKE_INSTALL_PREFIX=${PREFIX} -DHAS_DYNAMICLOAD=OFF -DHAS_STATICGOLIB=OFF ${CMAKEARGS}"
    export CC="clang"
    export CXX="clang++"
  fi
fi

# make args
if [[ "${BUILD}" == "1" ]] || [[ "${DEBUG}" == "1" ]]; then
  if [ "${OS}" == "Linux" ]; then
    MEM="$(( $(($(getconf _PHYS_PAGES) * $(getconf PAGE_SIZE) / (1000 * 1000 * 1000))) * 1000 ))" # in MB
  elif [ "${OS}" == "Darwin" ]; then
    MEM="$(( $(($(sysctl -n hw.memsize) / (1000 * 1000 * 1000))) * 1000 ))" # in MB
  fi

  MEM_NEEDED_PER_CORE="3500" # tdlib under g++ needs 3.5 GB
  if [[ "$(${CXX:-c++} -dM -E -x c++ - < /dev/null | grep CLANG_ATOMIC > /dev/null ; echo ${?})" == "0" ]]; then
    MEM_NEEDED_PER_CORE="1500" # tdlib under clang++ needs 1.5 GB
  fi

  MEM_MAX_THREADS="$((${MEM} / ${MEM_NEEDED_PER_CORE}))"
  if [[ "${MEM_MAX_THREADS}" == "0" ]]; then
    MEM_MAX_THREADS="1" # minimum 1 core
  fi

  if [[ "${OS}" == "Darwin" ]]; then
    CPU_MAX_THREADS="$(sysctl -n hw.ncpu)"
  else
    CPU_MAX_THREADS="$(nproc)"
  fi

  if [[ ${MEM_MAX_THREADS} -gt ${CPU_MAX_THREADS} ]]; then
    MAX_THREADS=${CPU_MAX_THREADS}
  else
    MAX_THREADS=${MEM_MAX_THREADS}
  fi

  MAKEARGS="-j${MAX_THREADS}"
fi

# ccache zero stats
if [[ "${BUILD}" == "1" ]] || [[ "${DEBUG}" == "1" ]]; then
  if command -v ccache &> /dev/null; then
    ccache -z > /dev/null
  fi
fi

# build
if [[ "${BUILD}" == "1" ]]; then
  echo "-- Using cmake ${CMAKEARGS}"
  echo "-- Using ${MAKEARGS} (${CPU_MAX_THREADS} cores, ${MEM} MB phys mem, ${MEM_NEEDED_PER_CORE} MB mem per core needed)"
  mkdir -p build && cd build && cmake ${CMAKEARGS} .. && make -s ${MAKEARGS} && cd .. || exiterr "build failed, exiting."
fi

# debug
if [[ "${DEBUG}" == "1" ]]; then
  CMAKEARGS="-DCMAKE_BUILD_TYPE=Debug ${CMAKEARGS}"
  echo "-- Using cmake ${CMAKEARGS}"
  echo "-- Using ${MAKEARGS} (${CPU_MAX_THREADS} cores, ${MEM} MB phys mem, ${MEM_NEEDED_PER_CORE} MB mem per core needed)"
  mkdir -p dbgbuild && cd dbgbuild && cmake ${CMAKEARGS} .. && make -s ${MAKEARGS} && cd .. || exiterr "debug build failed, exiting."
fi

# ccache stats
if [[ "${BUILD}" == "1" ]] || [[ "${DEBUG}" == "1" ]]; then
  if command -v ccache &> /dev/null; then
    CCACHE_STATS="$(ccache -s)"
    HITS="$(echo "${CCACHE_STATS}" | grep "Hits:" | head -1 | awk '{print $2}')"
    MISSES="$(echo "${CCACHE_STATS}" | grep "Misses:" | head -1 | awk '{print $2}')"
    TOTAL="$(echo "${CCACHE_STATS}" | grep "Hits:" | head -1 | awk '{print $4}')"
    if [ -n "${HITS}" ]; then
      echo "-- Ccache stats: ${HITS} hits, ${MISSES} misses, ${TOTAL} total."
    fi
  fi
fi

# tests
if [[ "${TESTS}" == "1" ]]; then
  true || exiterr "tests failed, exiting."
fi

# doc
if [[ "${DOC}" == "1" ]]; then
  if [[ -x "$(command -v help2man)" ]]; then
    if [[ "$(uname)" == "Darwin" ]]; then
      SED="gsed -i"
    else
      SED="sed -i"
    fi
    help2man -n "ncurses chat" -N -o src/nchat.1 ./build/bin/nchat && ${SED} "s/\.\\\\\" DO NOT MODIFY THIS FILE\!  It was generated by help2man.*/\.\\\\\" DO NOT MODIFY THIS FILE\!  It was generated by help2man./g" src/nchat.1 && ${SED} "s/^Combined distribution subject to.*/License varies by build configuration.\n.br\nSee \`nchat \-\-version\` for details./g" src/nchat.1 || exiterr "doc failed, exiting."
  fi
fi

# install
if [[ "${INSTALL}" == "1" ]]; then
  if [[ -z ${INSTALL_CMD+x} ]]; then
    if [[ "${OS}" == "Linux" ]]; then
      if [[ "${DISTRO}" != "Termux" ]]; then
        INSTALL_CMD="$(basename $(which sudo doas | head -1))"
      fi
    elif [[ "${OS}" == "Darwin" ]]; then
      if [[ "${GITHUB_ACTIONS}" == "true" ]]; then
        INSTALL_CMD="sudo"
      fi
    fi
  fi

  echo "-- Using ${INSTALL_CMD:+$INSTALL_CMD }make install"
  cd build && ${INSTALL_CMD} make install && cd .. || exiterr "install failed (${OS}), exiting."
fi

# exit
exit 0
