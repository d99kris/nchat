#!/bin/bash
docker run --rm -itv $(pwd):/data rust:1-alpine /data/update-ffi-docker-inner.sh
./update-ffi-version.sh
