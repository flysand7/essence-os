set -eu
mkdir -p bin/include_x11
# TODO Generate bundle and headers here, rather than using existing.
cp root/Applications/POSIX/include/essence.h bin/include_x11
cp root/Essence/Desktop.esx bin/bundle.dat
ld -r -b binary -o bin/Object\ Files/bundle.o bin/bundle.dat
g++ -o bin/hello_x11 util/x11/platform.cpp $1 bin/Object\ Files/bundle.o -lfreetype -lharfbuzz -lX11 -pthread -g -fno-exceptions -Ibin/include_x11 -I. -I/usr/include/freetype2 -DNO_API_TABLE -DUSE_PLATFORM_HEAP -DUSE_FREETYPE_AND_HARFBUZZ -DUSE_STB_SPRINTF -D_start=_StartApplication -D_init=EsHeapValidate -DES_FORWARD -DARRAY_DEFINITIONS_ONLY -Wall -Wextra -Wno-empty-body -Wno-deprecated-declarations -Wno-unknown-pragmas -Wno-missing-field-initializers -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -fsanitize=address
