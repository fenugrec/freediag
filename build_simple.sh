#!/bin/bash
#
## debian/ubuntu:
## sudo apt install build-essential cmake g++ make pkg-config libfltk1.3-dev
#
mkdir -p build
cd build
cmake -G"Unix Makefiles" \
      -DBUILD_DIAGTEST=ON \
      -DCMAKE_VERBOSE_MAKEFILE=0 \
      -S .. || exit 1
make