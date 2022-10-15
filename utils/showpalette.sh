#!/usr/bin/env bash

# showpalette.sh
#
# Copyright (C) 2022 Kristofer Berggren
# All rights reserved.
#
# See LICENSE for redistribution information.

if [[ ! -f "${1}" ]]; then
  echo "usage: showpalette.sh <file>"
  exit 1
fi

ID=17
while IFS='$\n' read -r LINE; do
  COLOR=""
  if [[ ${LINE} =~ ^#.* ]]; then
    COLOR="${LINE:1}"
  elif [[ ${LINE} =~ ^0x.* ]]; then
    COLOR="${LINE:2}"
  fi

  if [[ "${COLOR}" == "" ]]; then
    echo "unsupported color code ${LINE}"
  else
    let ID=ID+1
    R=${COLOR:0:2}
    G=${COLOR:2:2}
    B=${COLOR:4:2}
    printf "\e]4;${ID};rgb:${R}/${G}/${B}\a\e[38;5;${ID}m${COLOR}\e[m\n"
  fi
done < "${1}"
