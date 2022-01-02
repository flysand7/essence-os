#!/bin/bash
set -eux

# TODO:
# Run stress tests.

cd essence
mkdir -p bin
echo "accepted_license=1" >> bin/build_config.ini
echo "desktop/api_tests.ini" >> bin/extra_applications.ini
echo `git log | head -n 1 | cut -b 8-14` > bin/commit.txt
./start.sh get-source prefix https://github.com/nakst/build-gcc/releases/download/gcc-11.1.0/gcc-x86_64-essence.tar.xz
./start.sh setup-pre-built-toolchain
./start.sh run-tests
rm -rf cross .git bin/cache bin/freetype bin/harfbuzz bin/musl root/Applications/POSIX/lib bin/drive
