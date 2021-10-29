@ECHO OFF

ECHO 101 TEXT bin\embed.dat > bin\embed.rc
rc /nologo bin\embed.rc

SET CFLAGS=%2 -g -fno-exceptions -Ibin -I. -DES_ARCH_X86_64 -DES_BITS_64 -DNO_API_TABLE -DUSE_PLATFORM_HEAP -DUSE_HARFBUZZ -DUSE_FREETYPE -DUSE_STB_SPRINTF -Wall -Wextra -Wno-empty-body -Wno-deprecated-declarations -Wno-unknown-pragmas -Wno-unknown-attributes -Wno-c99-designator -Wno-missing-field-initializers -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
SET LINKFLAGS=/DEBUG /NOLOGO /NODEFAULTLIB /IGNORE:4217 /subsystem:windows /OPT:REF /OPT:ICF /STACK:0x100000,0x100000
SET APPFLAGS=-D_start=_ApplicationStart -DES_FORWARD -DES_EXTERN_FORWARD=ES_EXTERN_C -DES_DIRECT_API

clang -o bin\header_generator.exe util\header_generator.c %CFLAGS%
bin\header_generator c bin\essence.h

clang++ -o bin\desktop.o -c desktop\api.cpp -DOS_ESSENCE %CFLAGS%
clang++ -o bin\w32.o -c util\w32\platform.cpp %CFLAGS%
clang++ -o bin\application.o -c %1 %APPFLAGS% %CFLAGS%
link %LINKFLAGS% /OUT:bin\desktop.exe bin\desktop.o bin\w32.o bin\application.o bin\freetype.lib bin\harfbuzz.lib bin\embed.res kernel32.lib user32.lib gdi32.lib dwmapi.lib shell32.lib
