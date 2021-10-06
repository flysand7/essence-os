set -e

SYSROOT=`realpath root`
VERSION=2.6.9

bin/build get-source bochs-$VERSION https://netix.dl.sourceforge.net/project/bochs/bochs/$VERSION/bochs-$VERSION.tar.gz
cd bin/source

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
# ./configure --with-essence CC=x86_64-essence-gcc CXX=x86_64-essence-g++ CFLAGS=" -g -D _GNU_SOURCE " CXXFLAGS=" -g -D _GNU_SOURCE " --host=x86_64-essence --prefix=/Applications/POSIX --exec-prefix=/Applications/POSIX --enable-cpu-level=6 --enable-x86-64 
make -j 4
make DESTDIR=$SYSROOT install
echo Built Bochs without error.

cd ../..
rm -r bin/source
