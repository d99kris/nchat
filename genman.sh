#!/bin/bash

# genman.sh builds application and (re-)generates its man-page

mkdir -p build && cd build && cmake .. && make -s -j2 && cd .. && \
help2man -n "ncurses chat" -N -o src/nchat.1 ./build/bin/nchat
exit ${?}

