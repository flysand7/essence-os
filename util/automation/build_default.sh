#!/bin/bash
set -eux

cd essence
mkdir -p bin root
echo "accepted_license=1" >> bin/build_config.ini
echo "automated_build=1"  >> bin/build_config.ini
./start.sh get-toolchain
./start.sh build
