set -e

CompileHarfbuzz() {
	$CC -c hb-aat-layout.cc -o hb-aat-layout.o
	$CC -c hb-aat-map.cc -o hb-aat-map.o
	$CC -c hb-blob.cc -o hb-blob.o
	$CC -c hb-buffer-serialize.cc -o hb-buffer-serialize.o
	$CC -c hb-buffer.cc -o hb-buffer.o
	$CC -c hb-common.cc -o hb-common.o
	$CC -c hb-face.cc -o hb-face.o
	$CC -c hb-fallback-shape.cc -o hb-fallback-shape.o
	$CC -c hb-font.cc -o hb-font.o
	$CC -c hb-map.cc -o hb-map.o
	$CC -c hb-number.cc -o hb-number.o
	$CC -c hb-ot-cff1-table.cc -o hb-ot-cff1-table.o
	$CC -c hb-ot-cff2-table.cc -o hb-ot-cff2-table.o
	$CC -c hb-ot-color.cc -o hb-ot-color.o
	$CC -c hb-ot-face.cc -o hb-ot-face.o
	$CC -c hb-ot-font.cc -o hb-ot-font.o
	$CC -c hb-ot-layout.cc -o hb-ot-layout.o
	$CC -c hb-ot-map.cc -o hb-ot-map.o
	$CC -c hb-ot-math.cc -o hb-ot-math.o
	$CC -c hb-ot-meta.cc -o hb-ot-meta.o
	$CC -c hb-ot-metrics.cc -o hb-ot-metrics.o
	$CC -c hb-ot-name.cc -o hb-ot-name.o
	$CC -c hb-ot-shape-complex-arabic.cc -o hb-ot-shape-complex-arabic.o
	$CC -c hb-ot-shape-complex-default.cc -o hb-ot-shape-complex-default.o
	$CC -c hb-ot-shape-complex-hangul.cc -o hb-ot-shape-complex-hangul.o
	$CC -c hb-ot-shape-complex-hebrew.cc -o hb-ot-shape-complex-hebrew.o
	$CC -c hb-ot-shape-complex-indic-table.cc -o hb-ot-shape-complex-indic-table.o
	$CC -c hb-ot-shape-complex-indic.cc -o hb-ot-shape-complex-indic.o
	$CC -c hb-ot-shape-complex-khmer.cc -o hb-ot-shape-complex-khmer.o
	$CC -c hb-ot-shape-complex-myanmar.cc -o hb-ot-shape-complex-myanmar.o
	$CC -c hb-ot-shape-complex-thai.cc -o hb-ot-shape-complex-thai.o
	$CC -c hb-ot-shape-complex-use-table.cc -o hb-ot-shape-complex-use-table.o
	$CC -c hb-ot-shape-complex-use.cc -o hb-ot-shape-complex-use.o
	$CC -c hb-ot-shape-complex-vowel-constraints.cc -o hb-ot-shape-complex-vowel-constraints.o
	$CC -c hb-ot-shape-fallback.cc -o hb-ot-shape-fallback.o
	$CC -c hb-ot-shape-normalize.cc -o hb-ot-shape-normalize.o
	$CC -c hb-ot-shape.cc -o hb-ot-shape.o
	$CC -c hb-ot-tag.cc -o hb-ot-tag.o
	$CC -c hb-ot-var.cc -o hb-ot-var.o
	$CC -c hb-set.cc -o hb-set.o
	$CC -c hb-shape-plan.cc -o hb-shape-plan.o
	$CC -c hb-shape.cc -o hb-shape.o
	$CC -c hb-shaper.cc -o hb-shaper.o
	$CC -c hb-static.cc -o hb-static.o
	$CC -c hb-ucd.cc -o hb-ucd.o
	$CC -c hb-unicode.cc -o hb-unicode.o
	$CC -c hb-ft.cc -o hb-ft.o

	$AR libharfbuzz.a hb-aat-layout.o hb-aat-map.o hb-blob.o hb-buffer-serialize.o hb-buffer.o hb-common.o hb-face.o hb-fallback-shape.o hb-font.o hb-map.o hb-number.o hb-ot-cff1-table.o hb-ot-cff2-table.o hb-ot-color.o hb-ot-face.o hb-ot-font.o hb-ot-layout.o hb-ot-map.o hb-ot-math.o hb-ot-meta.o hb-ot-metrics.o hb-ot-name.o hb-ot-shape-complex-arabic.o hb-ot-shape-complex-default.o hb-ot-shape-complex-hangul.o hb-ot-shape-complex-hebrew.o hb-ot-shape-complex-indic-table.o hb-ot-shape-complex-indic.o hb-ot-shape-complex-khmer.o hb-ot-shape-complex-myanmar.o hb-ot-shape-complex-thai.o hb-ot-shape-complex-use-table.o hb-ot-shape-complex-use.o hb-ot-shape-complex-vowel-constraints.o hb-ot-shape-fallback.o hb-ot-shape-normalize.o hb-ot-shape.o hb-ot-tag.o hb-ot-var.o hb-set.o hb-shape-plan.o hb-shape.o hb-shaper.o hb-static.o hb-ucd.o hb-unicode.o hb-ft.o
}

CFLAGS="-DHAVE_CONFIG_H -I. -I.. -ffreestanding -fno-rtti -g -O3 -DHB_TINY -fno-exceptions -fno-threadsafe-statics \
	-fvisibility-inlines-hidden -DHB_NO_PRAGMA_GCC_DIAGNOSTIC_ERROR"

if [ ! -d "bin/harfbuzz" ]; then
	echo "Downloading Harfbuzz..."

	bin/build get-source-checked 9413b8d96132d699687ef914ebb8c50440efc87b3f775d25856d7ec347c03c12 harfbuzz-2.6.4 https://www.freedesktop.org/software/harfbuzz/release/harfbuzz-2.6.4.tar.xz
	mv bin/source bin/harfbuzz

	cd bin/harfbuzz
	./configure --with-glib=no --with-icu=no --with-freetype=no --with-cairo=no --with-fontconfig=no --enable-shared \
		CFLAGS="-g -O3 -DHB_TINY" CXXFLAGS="-g -O3 -DHB_TINY" > ../Logs/harfbuzz_configure.txt
	cd ../..

	cp ports/harfbuzz/essence-config.h bin/harfbuzz/config.h

	cd bin/harfbuzz/src

	SED=sed
	uname -a | grep Darwin && SED=gsed

	find . -type f -exec $SED -i 's/#include <assert.h>/#include <essence.h>/g' {} \;
	find . -type f -exec $SED -i 's/#include <atomic.h>/#include <essence.h>/g' {} \;
	find . -type f -exec $SED -i 's/#include <builtins.h>/#include <essence.h>/g' {} \;
	find . -type f -exec $SED -i 's/#include <float.h>/#include <essence.h>/g' {} \;
	find . -type f -exec $SED -i 's/#include <locale.h>/#include <essence.h>/g' {} \;
	find . -type f -exec $SED -i 's/#include <math.h>/#include <essence.h>/g' {} \;
	find . -type f -exec $SED -i 's/#include <stdio.h>/#include <essence.h>/g' {} \;
	find . -type f -exec $SED -i 's/#include <stdlib.h>/#include <essence.h>/g' {} \;
	find . -type f -exec $SED -i 's/#include <string.h>/#include <essence.h>/g' {} \;
	find . -type f -exec $SED -i 's/#include <unistd.h>/#include <essence.h>/g' {} \;
	find . -type f -exec $SED -i 's/#include <xlocale.h>/#include <essence.h>/g' {} \;

	cd ../../..
fi

if [ "$1" = "x86_64" ]; then
	if [ ! -f "bin/harfbuzz/libharfbuzz_x86_64.a" ]; then
		cd bin/harfbuzz/src
		CC="x86_64-essence-g++ $CFLAGS"
		AR="x86_64-essence-ar cr"
		echo "Building Harfbuzz for x86_64..."
		CompileHarfbuzz
		cp libharfbuzz.a ../libharfbuzz_x86_64.a
		cd ../../..
	fi

	cp -p bin/harfbuzz/libharfbuzz_x86_64.a root/Applications/POSIX/lib/libharfbuzz.a
fi

if [ "$1" = "x86_32" ]; then
	if [ ! -f "bin/harfbuzz/libharfbuzz_x86_32.a" ]; then
		INC=`realpath root/Applications/POSIX/include`
		cd bin/harfbuzz/src
		CC="i686-elf-g++ $CFLAGS -I$INC"
		AR="i686-elf-ar cr"
		echo "Building Harfbuzz for x86_32..."
		CompileHarfbuzz
		i686-elf-ranlib libharfbuzz.a
		cp libharfbuzz.a ../libharfbuzz_x86_32.a
		cd ../../..
	fi

	cp -p bin/harfbuzz/libharfbuzz_x86_32.a root/Applications/POSIX/lib/libharfbuzz.a
fi

mkdir -p root/Applications/POSIX/include/harfbuzz
cp -p bin/harfbuzz/src/*.h root/Applications/POSIX/include/harfbuzz
