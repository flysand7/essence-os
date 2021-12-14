set -e

# Get the source.
rm -rf bin/uxn bin/noodle
git clone https://git.sr.ht/~rabbits/uxn bin/uxn
cd bin/uxn
git checkout d2cf7213d0287f9777fac583f6889ee18b188f8d
cd ../..
git clone https://git.sr.ht/~rabbits/noodle bin/noodle
cd bin/noodle
git checkout 17b6b2d48c6fa07adbf25f8c79b0e1b8675be8ad
cd ../..

# Build the assembler.
cc -DNDEBUG -Os -g0 -s bin/uxn/src/uxnasm.c -o bin/uxnasm

# Build the ROMs.
cd bin/noodle
../../bin/uxnasm src/main.tal ../../bin/noodle.rom
cd ../..

# Build the emulator.
echo > bin/uxn/src/devices/ppu.h
x86_64-essence-gcc -DNDEBUG -Os -g0 -s ports/uxn/emulator.c -ffreestanding -nostdlib -lgcc -z max-page-size=0x1000 -o bin/uxnemu
