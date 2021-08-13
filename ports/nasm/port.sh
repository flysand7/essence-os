set -e

SYSROOT=`realpath root`
VERSION=2.15.05
NAME=nasm
URL="https://www.nasm.us/pub/nasm/releasebuilds/$VERSION/$NAME-$VERSION.tar.xz"
SOURCE="bin/$NAME-$VERSION.tar.xz"

if [ ! -f $SOURCE ]; then curl $URL > $SOURCE; fi
tar -xJf $SOURCE
mv $NAME-$VERSION bin/$NAME

cd bin/$NAME
./configure --host=x86_64-essence CC=x86_64-essence-gcc CXX=x86_64-essence-g++ --prefix=/Applications/POSIX
make -j 4
DESTDIR=$SYSROOT make install
cd ../..
rm -r bin/$NAME
