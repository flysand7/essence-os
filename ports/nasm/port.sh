set -e
SYSROOT=`realpath root`
VERSION=2.15.05
bin/build get-source nasm-$VERSION https://www.nasm.us/pub/nasm/releasebuilds/$VERSION/nasm-$VERSION.tar.xz
cd bin/source
./configure --host=x86_64-essence CC=x86_64-essence-gcc CXX=x86_64-essence-g++ --prefix=/Applications/POSIX
make -j 4
DESTDIR=$SYSROOT make install
cd ../..
rm -r bin/source
