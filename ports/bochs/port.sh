set -e

SYSROOT=`realpath root`
VERSION=2.6.9
NAME=bochs
URL="https://netix.dl.sourceforge.net/project/$NAME/$NAME/$VERSION/$NAME-$VERSION.tar.gz"
SOURCE="bin/$NAME-$VERSION.tar.gz"

if [ ! -f $SOURCE ]; then curl $URL > $SOURCE; fi
tar -xzf $SOURCE
mv $NAME-$VERSION bin/$NAME

cd bin/$NAME

cp ../../ports/bochs/config.cc .
cp ../../ports/bochs/config.h.in .
cp ../../ports/bochs/configure.in .
cp ../../ports/bochs/main.cc .
cp ../../ports/bochs/Makefile.in .
cp ../../ports/bochs/plugin.cc .
cp ../../ports/bochs/plugin.h .
cp ../../ports/bochs/essence.cc gui
cp ../../ports/bochs/gui_Makefile.in gui/Makefile.in

autoconf
./configure --with-essence CC=x86_64-essence-gcc CXX=x86_64-essence-g++ CFLAGS=" -O2 -D _GNU_SOURCE " CXXFLAGS=" -O2 -D _GNU_SOURCE " --host=x86_64-essence --prefix=/Applications/POSIX --exec-prefix=/Applications/POSIX --enable-cpu-level=6 --enable-x86-64 --enable-all-optimizations
make -j 4
make DESTDIR=$SYSROOT install

cd ../..
rm -r bin/$NAME
