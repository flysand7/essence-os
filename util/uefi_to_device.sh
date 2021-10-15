# Run as root.
# Pass device file of ESP as first argument.
# Pass device file of EsFS partition as second argument.

set -e

util/uefi_compile.sh

mkdir -p mount
mount $1 mount
mkdir -p mount/EFI/BOOT
cp bin/uefi mount/EFI/BOOT/BOOTX64.EFI
cp bin/Kernel.esx mount/eskernel.esx
cp bin/uefi_loader mount/esloader.bin
cp bin/iid.dat mount/esiid.dat
umount $1
rmdir mount

SOURCE_OFFSET=`fdisk -l bin/drive | grep 'Linux' | awk '{print $3}'`
SOURCE_COUNT=`fdisk -l bin/drive | grep 'Linux' | awk '{print $5}'`
DESTINATION_COUNT=`blockdev --getsz $2`

if [ "$SOURCE_COUNT" -gt "$DESTINATION_COUNT" ]; then
	echo Please set Emulator.PrimaryDriveMB to fit on the drive.
	exit 1
fi

dd if=bin/drive of=$2 bs=512 count=$SOURCE_COUNT skip=$SOURCE_OFFSET conv=notrunc
