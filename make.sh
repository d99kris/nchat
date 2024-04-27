#!/usr/bin/env bash

# make.sh
#
# Copyright (C) 2020-2024 Kristofer Berggren
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
YES=""
CMAKEARGS=""

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

    all)
      DEPS="1"
      BUILD="1"
      TESTS="1"
      DOC="1"
      INSTALL="1"
      ;;

    --no-telegram)
      CMAKEARGS="${CMAKEARGS} -DHAS_TELEGRAM=OFF"
      ;;

    --no-whatsapp)
      CMAKEARGS="${CMAKEARGS} -DHAS_WHATSAPP=OFF"
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

# deps
if [[ "${DEPS}" == "1" ]]; then
  OS="$(uname)"
  if [ "${OS}" == "Linux" ]; then
    unset NAME
    eval $(grep "^NAME=" /etc/os-release 2> /dev/null)
    if [[ "${NAME}" == "Ubuntu" ]]; then
      sudo apt update && sudo apt ${YES} install ccache cmake build-essential gperf help2man libreadline-dev libssl-dev libncurses-dev libncursesw5-dev ncurses-doc zlib1g-dev libsqlite3-dev libmagic-dev || exiterr "deps failed (${NAME}), exiting."
      unset VERSION_ID
      eval $(grep "^VERSION_ID=" /etc/os-release 2> /dev/null)
      if version_ge "${VERSION_ID}" "22.04"; then
        sudo apt ${YES} install golang || exiterr "deps failed (${NAME} apt golang), exiting."
      else
        sudo snap install go --classic || exiterr "deps failed (${NAME} snap go), exiting."
      fi
    elif [[ "${NAME}" == "Raspbian GNU/Linux" ]] || [[ "${NAME}" == "Debian GNU/Linux" ]]; then
      sudo apt update && sudo apt ${YES} install ccache cmake build-essential gperf help2man libreadline-dev libssl-dev libncurses-dev libncursesw5-dev ncurses-doc zlib1g-dev libsqlite3-dev libmagic-dev golang || exiterr "deps failed (${NAME}), exiting."
    elif [[ "${NAME}" == "Gentoo" ]]; then
      sudo emerge -n dev-util/cmake dev-util/ccache dev-util/gperf sys-apps/help2man sys-libs/readline dev-libs/openssl sys-libs/ncurses sys-libs/zlib dev-db/sqlite sys-apps/file dev-lang/go || exiterr "deps failed (${NAME}), exiting."
    elif [[ "${NAME}" == "Fedora Linux" ]]; then
      sudo dnf ${YES} install git cmake clang golang ccache file-devel file-libs gperf readline-devel openssl-devel ncurses-devel sqlite-devel zlib-devel || exiterr "deps failed (${NAME}), exiting."
    elif [[ "${NAME}" == "Arch Linux" ]] || [[ "${NAME}" == "EndeavourOS" ]]; then
      sudo pacman -S ccache cmake file go gperf help2man ncurses openssl readline sqlite zlib base-devel || exiterr "deps failed (${NAME}), exiting."
    elif [[ "${NAME}" == "Void" ]]; then
      sudo xbps-install ${YES} base-devel go ccache cmake gperf help2man libmagick-devel readline-devel sqlite-devel file-devel openssl-devel || exiterr "deps failed (${NAME}), exiting."
    elif [[ "${NAME}" == "Alpine Linux" ]]; then
      sudo apk add git build-base cmake ncurses-dev openssl-dev sqlite-dev file-dev go linux-headers zlib-dev ccache gperf readline || exiterr "deps failed (${NAME}), exiting."
    elif [[ "${NAME}" == "openSUSE Tumbleweed" ]]; then
      sudo zypper install ${YES} -t pattern devel_C_C++ && sudo zypper install ${YES} go ccache cmake libopenssl-devel sqlite3-devel file-devel readline-devel || exiterr "deps failed (${NAME}), exiting."
    elif [[ "${NAME}" == "Chimera" ]]; then
      doas apk add git cmake clang go ccache gperf readline-devel openssl-devel ncurses-devel sqlite-devel zlib-devel file-devel || exiterr "deps failed (${NAME}), exiting."
    elif [[ "${NAME}" == "Rocky Linux" ]]; then
      sudo yum config-manager --set-enabled powertools && sudo yum ${YES} groupinstall "Development Tools" && sudo yum ${YES} install git go cmake gperf readline-devel openssl-devel ncurses-devel zlib-devel sqlite-devel file-devel || exiterr "deps failed (${NAME}), exiting."
    else
      exiterr "deps failed (unsupported linux distro ${NAME}), exiting."
    fi
  elif [ "${OS}" == "Darwin" ]; then
    if command -v brew &> /dev/null; then
      GOPKG="go"
      if [[ "${GITHUB_ACTIONS}" == "true" ]]; then
        GOPKG="" # skip go in github actions
      fi
      HOMEBREW_NO_AUTO_UPDATE=1 brew install ${GOPKG} gperf cmake openssl ncurses ccache readline sqlite libmagic || exiterr "deps failed (${OS} brew), exiting."
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
  go fmt lib/wmchat/go/*.go || \
    exiterr "go fmt failed, exiting."
  uncrustify --update-config-with-doc -c etc/uncrustify.cfg -o etc/uncrustify.cfg && \
  uncrustify -c etc/uncrustify.cfg --replace --no-backup src/*.{cpp,h} lib/common/src/*.h lib/duchat/src/*.{cpp,h} lib/ncutil/src/*.{cpp,h} lib/tgchat/src/*.{cpp,h} lib/wmchat/src/*.{cpp,h} || \
    exiterr "unrustify failed, exiting."
fi

# build
if [[ "${BUILD}" == "1" ]]; then
  OS="$(uname)"
  if [ "${OS}" == "Linux" ]; then
    MEM="$(( $(($(getconf _PHYS_PAGES) * $(getconf PAGE_SIZE) / (1000 * 1000 * 1000))) * 1000 ))" # in MB
  elif [ "${OS}" == "Darwin" ]; then
    MEM="$(( $(($(sysctl -n hw.memsize) / (1000 * 1000 * 1000))) * 1000 ))" # in MB
  fi

  MEM_NEEDED_PER_CORE="3500" # tdlib under g++ needs 3.5 GB
  if [[ "$(c++ -dM -E -x c++ - < /dev/null | grep CLANG_ATOMIC > /dev/null ; echo ${?})" == "0" ]]; then
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
  echo "-- Using ${MAKEARGS} (${CPU_MAX_THREADS} cores, ${MEM} MB phys mem, ${MEM_NEEDED_PER_CORE} MB mem per core needed)"

  mkdir -p build && cd build && cmake ${CMAKEARGS} .. && make -s ${MAKEARGS} && cd .. || exiterr "build failed, exiting."
fi

# debug
if [[ "${DEBUG}" == "1" ]]; then
  OS="$(uname)"
  if [ "${OS}" == "Linux" ]; then
    MEM="$(( $(($(getconf _PHYS_PAGES) * $(getconf PAGE_SIZE) / (1000 * 1000 * 1000))) * 1000 ))" # in MB
  elif [ "${OS}" == "Darwin" ]; then
    MEM="$(( $(($(sysctl -n hw.memsize) / (1000 * 1000 * 1000))) * 1000 ))" # in MB
  fi

  MEM_NEEDED_PER_CORE="3500" # tdlib under g++ needs 3.5 GB
  if [[ "$(c++ -dM -E -x c++ - < /dev/null | grep CLANG_ATOMIC > /dev/null ; echo ${?})" == "0" ]]; then
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
  echo "-- Using ${MAKEARGS} (${CPU_MAX_THREADS} cores, ${MEM} MB phys mem, ${MEM_NEEDED_PER_CORE} MB mem per core needed)"

  mkdir -p dbgbuild && cd dbgbuild && cmake -DCMAKE_BUILD_TYPE=Debug ${CMAKEARGS} .. && make -s ${MAKEARGS} && cd .. || exiterr "debug build failed, exiting."
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
    help2man -n "ncurses chat" -N -o src/nchat.1 ./build/bin/nchat && ${SED} "s/\.\\\\\" DO NOT MODIFY THIS FILE\!  It was generated by help2man.*/\.\\\\\" DO NOT MODIFY THIS FILE\!  It was generated by help2man./g" src/nchat.1 || exiterr "doc failed, exiting."
  fi
fi

# install
if [[ "${INSTALL}" == "1" ]]; then
  OS="$(uname)"
  if [ "${OS}" == "Linux" ]; then
    cd build && sudo make install && cd .. || exiterr "install failed (linux), exiting."
  elif [ "${OS}" == "Darwin" ]; then
    GHSUDO=""
    if [[ "${GITHUB_ACTIONS}" == "true" ]]; then
      GHSUDO="sudo"
    fi
    cd build && ${GHSUDO} make install && cd .. || exiterr "install failed (mac), exiting."
  else
    exiterr "install failed (unsupported os ${OS}), exiting."
  fi
fi

# exit
exit 0
