#!/bin/bash

## debian/ubuntu : 
## sudo apt install build-essential cmake g++ pkg-config libfltk1.3-dev

mkdir -p tmp
cd tmp 
cmake ..
make
