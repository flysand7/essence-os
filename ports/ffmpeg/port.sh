set -e
SYSROOT=`realpath root`
VERSION=4.3.1
bin/script util/get_source.script checksum=ad009240d46e307b4e03a213a0f49c11b650e445b1f8be0dda2a9212b34d2ffb directoryName=ffmpeg-$VERSION url=https://ffmpeg.org/releases/ffmpeg-$VERSION.tar.xz
mkdir bin/build-ffmpeg
cd bin/build-ffmpeg
../source/configure --disable-all --cc=x86_64-essence-gcc --cxx=x86_64-essence-g++ --enable-cross-compile --enable-avcodec --enable-avformat --enable-swscale --enable-decoder=h264 --enable-parser=h264 --enable-decoder=aac --enable-parser=aac --enable-demuxer=mov --enable-protocol=file --disable-pthreads --prefix=/Applications/POSIX 
# --disable-optimizations
make -j `nproc`
make DESTDIR=$SYSROOT install
mkdir -p ../FFmpeg\ License
cp ../source/COPYING.* ../FFmpeg\ License
cp ../source/LICENSE.md ../FFmpeg\ License
cd ../..
rm -r bin/source bin/build-ffmpeg
