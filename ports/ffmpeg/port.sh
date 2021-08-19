set -e
SYSROOT=`realpath root`
VERSION=4.3.1
bin/build get-source ffmpeg-$VERSION https://ffmpeg.org/releases/ffmpeg-$VERSION.tar.xz
mkdir bin/build-ffmpeg
cd bin/build-ffmpeg
../source/configure --disable-all --cc=x86_64-essence-gcc --cxx=x86_64-essence-g++ --enable-cross-compile --enable-avcodec --enable-avformat --enable-swscale --enable-decoder=h264 --enable-parser=h264 --enable-decoder=aac --enable-parser=aac --enable-demuxer=mov --enable-protocol=file --disable-pthreads --prefix=/Applications/POSIX 
# --disable-optimizations
make -j 4
make DESTDIR=$SYSROOT install
cd ../..
rm -r bin/source bin/build-ffmpeg
