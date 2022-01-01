set -e
SYSROOT=`realpath root`
VERSION=2.15.05
bin/build get-source-checked 3caf6729c1073bf96629b57cee31eeb54f4f8129b01902c73428836550b30a3f nasm-$VERSION https://www.nasm.us/pub/nasm/releasebuilds/$VERSION/nasm-$VERSION.tar.xz
cd bin/source
./configure --host=x86_64-essence CC=x86_64-essence-gcc CXX=x86_64-essence-g++ --prefix=/Applications/POSIX
make -j `nproc`
DESTDIR=$SYSROOT make install
mv LICENSE ../Nasm\ License.txt
cd ../..
rm -r bin/source
