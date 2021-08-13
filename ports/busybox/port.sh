set -e

SYSROOT=`realpath root`
VERSION=1.33.1
NAME=busybox
URL="https://www.busybox.net/downloads/$NAME-$VERSION.tar.bz2"
SOURCE="bin/$NAME-$VERSION.tar.bz2"

if [ ! -f $SOURCE ]; then curl $URL > $SOURCE; fi
tar -xjf $SOURCE
mv $NAME-$VERSION bin/$NAME

cd bin/$NAME
cp ../../ports/$NAME/config .config
sed -i "51 i CONFIG_SYSROOT=\"$SYSROOT\"" .config
make -j 4
cp busybox $SYSROOT/Applications/POSIX/bin
cd ../..
rm -r bin/$NAME
