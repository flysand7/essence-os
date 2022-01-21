#!/bin/bash
set -eux

# TODO:
# Run stress tests.

cd essence
mkdir -p bin
echo "accepted_license=1" >> bin/build_config.ini
echo "Flag.ENABLE_POSIX_SUBSYSTEM=1" >> bin/config.ini
echo "desktop/api_tests.ini" >> bin/extra_applications.ini
echo `git log | head -n 1 | cut -b 8-14` > bin/commit.txt
./start.sh build
./start.sh build-port busybox
util/x11/build.sh apps/samples/hello.c
./start.sh run-tests
rm -rf cross .git bin/cache bin/freetype bin/harfbuzz bin/musl root/Applications/POSIX/lib bin/drive
