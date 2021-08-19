set -e
SYSROOT=`realpath root`
VERSION=1.33.1
bin/build get-source busybox-$VERSION https://www.busybox.net/downloads/busybox-$VERSION.tar.bz2
cd bin/source
cp ../../ports/busybox/config .config
sed -i "51 i CONFIG_SYSROOT=\"$SYSROOT\"" .config
make -j 4
cp busybox $SYSROOT/Applications/POSIX/bin
cd ../..
rm -r bin/source
