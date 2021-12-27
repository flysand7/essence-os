set -ex

rm -rf bin/mesa
bin/build get-source mesa-20.1.8 https://archive.mesa3d.org//mesa-20.1.8.tar.xz
mv bin/source bin/mesa

echo "" > bin/meson_cross.txt
echo "[binaries]" >> bin/meson_cross.txt
echo "c = 'x86_64-essence-gcc'" >> bin/meson_cross.txt
echo "cpp = 'x86_64-essence-g++'" >> bin/meson_cross.txt
echo "ar = 'x86_64-essence-ar'" >> bin/meson_cross.txt
echo "strip = 'x86_64-essence-strip'" >> bin/meson_cross.txt
echo "[properties]" >> bin/meson_cross.txt
echo sys_root = \'$(realpath root)\' >> bin/meson_cross.txt
echo "c_args = ['-D_POSIX_SOURCE']" >> bin/meson_cross.txt
echo "cpp_args = c_args" >> bin/meson_cross.txt
echo "[host_machine]" >> bin/meson_cross.txt
echo "system = 'essence'" >> bin/meson_cross.txt
echo "cpu_family = 'x86_64'" >> bin/meson_cross.txt
echo "cpu = 'generic'" >> bin/meson_cross.txt
echo "endian = 'little'" >> bin/meson_cross.txt

cp ports/mesa/changes/include_c11_threads_posix.h bin/mesa/include/c11/threads_posix.h
cp ports/mesa/changes/src_util_detect_os.h bin/mesa/src/util/detect_os.h
cp ports/mesa/changes/src_util_u_thread.h bin/mesa/src/util/u_thread.h
cp ports/mesa/changes/src_util_anon_file.c bin/mesa/src/util/anon_file.c
cp ports/mesa/changes/src_util_os_misc.c bin/mesa/src/util/os_misc.c
cp ports/mesa/changes/meson.build bin/mesa/meson.build
cp ports/mesa/changes/src_gallium_targets_osmesa_meson.build bin/mesa/src/gallium/targets/osmesa/meson.build

cd bin/mesa
meson ../build-mesa --cross-file ../meson_cross.txt \
	-Dosmesa=gallium \
	-Ddefault_library=static
ninja -C ../build-mesa
cd ../..

cp bin/build-mesa/subprojects/expat-2.2.5/libexpat.a root/Applications/POSIX/lib
cp bin/build-mesa/subprojects/zlib-1.2.11/libz.a root/Applications/POSIX/lib
cp bin/build-mesa/src/gallium/targets/osmesa/libOSMesa.a root/Applications/POSIX/lib
cp -r bin/mesa/include/GL root/Applications/POSIX/include
cp -r bin/mesa/include/KHR root/Applications/POSIX/include

rm -r bin/mesa bin/build-mesa
