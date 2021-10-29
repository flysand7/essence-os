if [ ! -d "bin/freetype" ]; then
	echo "Downloading FreeType..."

	if [ ! -f "bin/cache/freetype-2.9.tar" ]; then
		curl https://mirrors.up.pt/pub/nongnu/freetype/freetype-2.9.tar.gz > bin/cache/freetype-2.9.tar.gz 2> bin/freetype_dl.txt
		gunzip bin/cache/freetype-2.9.tar.gz
	fi

	tar -xf bin/cache/freetype-2.9.tar
	mv freetype-2.9 bin/freetype

	cp ports/freetype/patch-ftoption.h  bin/freetype/include/freetype/config/ftoption.h
	cp ports/freetype/patch-ftstdlib.h  bin/freetype/include/freetype/config/ftstdlib.h
	cp ports/freetype/patch-modules.cfg bin/freetype/modules.cfg
fi

if [ "$1" = "x86_64" ]; then
	if [ ! -f "bin/freetype/libfreetype_x86_64.a" ]; then
		echo "Building FreeType for x86_64..."
		cd bin/freetype
		rm -f objs/.libs/libfreetype.a
		./configure --without-zlib --without-bzip2 --without-png --without-harfbuzz \
			CC=x86_64-essence-gcc CFLAGS="-g -ffreestanding -Wno-unused-function -O3" \
			LDFLAGS="-nostdlib -lgcc" --host=x86_64-essence > ../freetype_configure.txt 2>&1
		make ANSIFLAGS="" > /dev/null
		cp objs/.libs/libfreetype.a libfreetype_x86_64.a
		cd ../..
	fi

	cp -p bin/freetype/libfreetype_x86_64.a root/Applications/POSIX/lib/libfreetype.a
fi

if [ "$1" = "x86_32" ]; then
	if [ ! -f "bin/freetype/libfreetype_x86_32.a" ]; then
		echo "Building FreeType for x86_32..."
		INC=`realpath root/Applications/POSIX/include`
		cd bin/freetype
		rm -f objs/.libs/libfreetype.a
		./configure --without-zlib --without-bzip2 --without-png --without-harfbuzz \
			CC=i686-elf-gcc CFLAGS="-g -ffreestanding -Wno-unused-function -O3 -I$INC" \
			LDFLAGS="-nostdlib -lgcc" --host=i686-elf > ../freetype_configure.txt 2>&1
		sed -i '/define FT_USE_AUTOCONF_SIZEOF_TYPES/d' builds/unix/ftconfig.h
		make ANSIFLAGS="" > /dev/null
		cp objs/.libs/libfreetype.a libfreetype_x86_32.a
		cd ../..
	fi

	cp -p bin/freetype/libfreetype_x86_32.a root/Applications/POSIX/lib/libfreetype.a
fi

cp -p bin/freetype/include/ft2build.h root/Applications/POSIX/include
cp -p -r bin/freetype/include/freetype root/Applications/POSIX/include
