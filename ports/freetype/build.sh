if [ ! -d "bin/freetype" ]; then
	echo "Downloading and building FreeType..."

	if [ ! -f "bin/freetype-2.9.tar" ]; then
		curl https://mirrors.up.pt/pub/nongnu/freetype/freetype-2.9.tar.gz > bin/freetype-2.9.tar.gz 2> bin/freetype_dl.txt
		gunzip bin/freetype-2.9.tar.gz
	fi

	tar -xf bin/freetype-2.9.tar
	mv freetype-2.9 bin/freetype

	cp ports/freetype/patch-ftoption.h  bin/freetype/include/freetype/config/ftoption.h
	cp ports/freetype/patch-ftstdlib.h  bin/freetype/include/freetype/config/ftstdlib.h
	cp ports/freetype/patch-modules.cfg bin/freetype/modules.cfg

	cd bin/freetype
	./configure --without-zlib --without-bzip2 --without-png --without-harfbuzz \
		CC=x86_64-essence-gcc CFLAGS="-g -ffreestanding -DARCH_X86_64 -Wno-unused-function" \
		LDFLAGS="-nostdlib -lgcc" --host=x86_64-essence > ../freetype_configure.txt
	make ANSIFLAGS="" > /dev/null
	cd ../..
fi

cp -p bin/freetype/objs/.libs/libfreetype.a root/Applications/POSIX/lib
cp -p bin/freetype/include/ft2build.h root/Applications/POSIX/include
cp -p -r bin/freetype/include/freetype root/Applications/POSIX/include
