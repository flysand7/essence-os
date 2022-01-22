set -e

SYSROOT=`realpath root`
VERSION=1.33.1

uname -a | grep Darwin > /dev/null
if [ $? -ne 1 ]; then
	PATH="/usr/local/opt/gnu-sed/libexec/gnubin:$PATH"
fi

bin/build get-source-checked 12cec6bd2b16d8a9446dd16130f2b92982f1819f6e1c5f5887b6db03f5660d28 busybox-$VERSION https://www.busybox.net/downloads/busybox-$VERSION.tar.bz2
cd bin/source
cp ../../ports/busybox/config .config
sed -i "51 i CONFIG_SYSROOT=\"$SYSROOT\"" .config

make -j `nproc`
cp busybox $SYSROOT/Applications/POSIX/bin
cp LICENSE ../BusyBox\ License.txt
cd ../..
rm -r bin/source
