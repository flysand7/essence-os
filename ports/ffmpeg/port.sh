set -e

SYSROOT=`realpath root`

if [ ! -f "bin/ffmpeg.tar.xz" ]; then
	curl https://ffmpeg.org/releases/ffmpeg-4.3.1.tar.xz > bin/ffmpeg.tar.xz
fi

tar -xJf bin/ffmpeg.tar.xz
mv ffmpeg-4.3.1 bin/ffmpeg

mkdir bin/build-ffmpeg

cd bin/build-ffmpeg
../ffmpeg/configure --disable-all --cc=x86_64-essence-gcc --cxx=x86_64-essence-g++ --enable-cross-compile --enable-avcodec --enable-avformat --enable-swscale --enable-decoder=h264 --enable-parser=h264 --enable-decoder=aac --enable-parser=aac --enable-demuxer=mov --enable-protocol=file --disable-pthreads --prefix=/Applications/POSIX 
# --disable-optimizations
make -j 4
make DESTDIR=$SYSROOT install
cd ../..

rm -r bin/ffmpeg bin/build-ffmpeg
