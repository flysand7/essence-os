set -e
rm -rf bin/uxn bin/noodle
git clone --depth=1 https://git.sr.ht/~rabbits/uxn bin/uxn
git clone --depth=1 https://git.sr.ht/~rabbits/noodle bin/noodle
cc -DNDEBUG -Os -g0 -s bin/uxn/src/uxnasm.c -o bin/uxnasm
bin/uxnasm bin/noodle/src/main.tal bin/noodle.rom
echo > bin/uxn/src/devices/ppu.h
x86_64-essence-gcc -DNDEBUG -Os -g0 -s ports/uxn/emulator.c -ffreestanding -nostdlib -lgcc -z max-page-size=0x1000 -o bin/uxnemu
