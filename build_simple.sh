#!/bin/bash

## debian/ubuntu: 
## sudo apt install build-essential cmake g++ make pkg-config libfltk1.3-dev

mkdir -p build
cd build
cmake ..
make