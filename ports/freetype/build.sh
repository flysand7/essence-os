if [ ! -d "bin/freetype" ]; then
	echo "Downloading FreeType..."

	bin/build get-source-checked bf380e4d7c4f3b5b1c1a7b2bf3abb967bda5e9ab480d0df656e0e08c5019c5e6 freetype-2.9 https://download.savannah.gnu.org/releases/freetype/freetype-2.9.tar.gz
	mv bin/source bin/freetype

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
			--host=x86_64-essence > ../Logs/freetype_configure.txt 2>&1
		make -j`nproc` > /dev/null
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
			LDFLAGS="-nostdlib -lgcc" --host=i686-elf > ../Logs/freetype_configure.txt 2>&1
		sed -i '/define FT_USE_AUTOCONF_SIZEOF_TYPES/d' builds/unix/ftconfig.h
		make -j`nproc` > /dev/null
		cp objs/.libs/libfreetype.a libfreetype_x86_32.a
		cd ../..
	fi

	cp -p bin/freetype/libfreetype_x86_32.a root/Applications/POSIX/lib/libfreetype.a
fi

cp -p bin/freetype/include/ft2build.h root/Applications/POSIX/include
cp -p -r bin/freetype/include/freetype root/Applications/POSIX/include
