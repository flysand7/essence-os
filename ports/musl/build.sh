if [ ! -d "bin/musl" ]; then
	echo "Downloading Musl..."

	bin/build get-source-checked 68af6e18539f646f9c41a3a2bb25be4a5cfa5a8f65f0bb647fd2bbfdf877e84b musl-1.2.1 https://musl.libc.org/releases/musl-1.2.1.tar.gz
	mv bin/source bin/musl

	cp ports/musl/changes/config.mak                            bin/musl/config.mak
	cp ports/musl/changes/dist_config.mak                       bin/musl/dist/config.mak
	cp ports/musl/changes/arch_x86_64_syscall_arch.h            bin/musl/arch/x86_64/syscall_arch.h
	cp ports/musl/changes/src_env___init_tls.c                  bin/musl/src/env/__init_tls.c
	cp ports/musl/changes/src_process_x86_64_vfork.s            bin/musl/src/process/x86_64/vfork.s
	cp ports/musl/changes/src_signal_x86_64_restore.s           bin/musl/src/signal/x86_64/restore.s
	cp ports/musl/changes/src_thread_x86_64___unmapself.s       bin/musl/src/thread/x86_64/__unmapself.s
	cp ports/musl/changes/src_thread_x86_64_clone.s             bin/musl/src/thread/x86_64/clone.s
	cp ports/musl/changes/src_thread_x86_64_syscall_cp.s        bin/musl/src/thread/x86_64/syscall_cp.s
fi

# To rebuild:
# cd bin/musl
# make clean
# make -j 4 lib/libc.a
# cd ../..
# cp bin/musl/lib/libc.a ports/musl

mkdir -p root/Applications/POSIX/lib root/Applications/POSIX/include
cp -p ports/musl/libc.a "root/Applications/POSIX/lib/libc.a"
cp -p ports/musl/empty.a "root/Applications/POSIX/lib/libm.a"
cp -p ports/musl/empty.a "root/Applications/POSIX/lib/libpthread.a"
cp -p ports/musl/empty.a "root/Applications/POSIX/lib/librt.a"
cp -p -r bin/musl/include/* "root/Applications/POSIX/include/"
cp -p -r bin/musl/arch/generic/* "root/Applications/POSIX/include/"

if [ "$1" = "x86_64" ]; then
	cp -p -r bin/musl/arch/x86_64/* "root/Applications/POSIX/include/"
	cp -p -r ports/musl/obj_bits_x86_64/* "root/Applications/POSIX/include/"
fi

if [ "$1" = "x86_32" ]; then
	cp -p -r bin/musl/arch/i386/* "root/Applications/POSIX/include/"
	cp -p -r ports/musl/obj_bits_x86_32/* "root/Applications/POSIX/include/"
fi
