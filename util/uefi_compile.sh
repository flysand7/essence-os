set -e
CC="clang -target x86_64-unknown-windows -ffreestanding -fshort-wchar -mno-red-zone -I ports/efitoolkit/inc -c -Wall -Wextra"
LINK="clang -target x86_64-unknown-windows -nostdlib -Wl,-entry:efi_main -Wl,-subsystem:efi_application -fuse-ld=lld-link"
$CC -o bin/uefi.o boot/x86/uefi.c 
$LINK -o bin/uefi bin/uefi.o 
