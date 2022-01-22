#!/bin/bash
set -eux

cd essence
mkdir -p bin root
echo "accepted_license=1"               >> bin/build_config.ini
echo "automated_build=1"                >> bin/build_config.ini
echo "Flag.DEBUG_BUILD=0"               >> bin/config.ini
echo "BuildCore.NoImportPOSIX=1"        >> bin/config.ini
echo "BuildCore.RequiredFontsOnly=1"    >> bin/config.ini
echo "Emulator.PrimaryDriveMB=32"       >> bin/config.ini
echo "Dependency.ACPICA=0"              >> bin/config.ini
echo "Dependency.stb_image=0"           >> bin/config.ini
echo "Dependency.stb_image_write=0"     >> bin/config.ini
echo "Dependency.stb_sprintf=0"         >> bin/config.ini
echo "Dependency.FreeTypeAndHarfBuzz=0" >> bin/config.ini
./start.sh get-toolchain
./start.sh build-optimised
