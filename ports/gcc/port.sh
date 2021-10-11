set -e

SYSROOT=`realpath root`

BINUTILS_VERSION=2.36.1
GCC_VERSION=11.1.0
GMP_VERSION=6.2.1
MPFR_VERSION=4.1.0
MPC_VERSION=1.2.1

rm -rf bin/gcc-src bin/binutils-src bin/mpc-src bin/gmp-src bin/mpfr-src
bin/build get-source binutils-$BINUTILS_VERSION ftp://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_VERSION.tar.xz
mv bin/source bin/binutils-src
bin/build get-source gcc-$GCC_VERSION ftp://ftp.gnu.org/gnu/gcc/gcc-$GCC_VERSION/gcc-$GCC_VERSION.tar.xz
mv bin/source bin/gcc-src
bin/build get-source gmp-$GMP_VERSION ftp://ftp.gnu.org/gnu/gmp/gmp-$GMP_VERSION.tar.xz
mv bin/source bin/gmp-src
bin/build get-source mpfr-$MPFR_VERSION ftp://ftp.gnu.org/gnu/mpfr/mpfr-$MPFR_VERSION.tar.xz
mv bin/source bin/mpfr-src
bin/build get-source mpc-$MPC_VERSION ftp://ftp.gnu.org/gnu/mpc/mpc-$MPC_VERSION.tar.gz
mv bin/source bin/mpc-src

cp ports/gcc/changes/binutils_bfd_config.bfd bin/binutils-src/bfd/config.bfd
cp ports/gcc/changes/binutils_config.sub bin/binutils-src/config.sub
cp ports/gcc/changes/binutils_gas_configure.tgt bin/binutils-src/gas/configure.tgt
cp ports/gcc/changes/binutils_ld_configure.tgt bin/binutils-src/ld/configure.tgt

cp ports/gcc/changes/gcc_config.sub bin/gcc-src/config.sub
cp ports/gcc/changes/gcc_fixincludes_mkfixinc.sh bin/gcc-src/fixincludes/mkfixinc.sh
cp ports/gcc/changes/gcc_gcc_config_essence.h bin/gcc-src/gcc/config/essence.h
cp ports/gcc/changes/gcc_gcc_config_i386_t-x86_64-essence bin/gcc-src/gcc/config/i386/t-x86_64-essence
cp ports/gcc/changes/gcc_gcc_config.gcc bin/gcc-src/gcc/config.gcc
cp ports/gcc/changes/gcc_gcc_config_host_darwin.c bin/gcc-src/gcc/config/host-darwin.c
cp ports/gcc/changes/gcc_libgcc_config.host bin/gcc-src/libgcc/config.host
cp ports/gcc/changes/gcc_libstdc++-v3_configure bin/gcc-src/libstdc++-v3/configure

if [ "$1" = "download-only" ]; then
	exit 0
fi

export ac_cv_func_calloc_0_nonnull="yes"
export ac_cv_func_chown_works="yes"
export ac_cv_func_getgroups_works="yes"
export ac_cv_func_malloc_0_nonnull="yes"
export gl_cv_func_cbrtl_ieee="yes"
export gl_cv_func_ceil_ieee="yes"
export gl_cv_func_ceilf_ieee="yes"
export gl_cv_func_ceill_ieee="yes"
export gl_cv_func_chown_ctime_works="yes"
export gl_cv_func_chown_slash_works="yes"
export gl_cv_func_exp2l_ieee="yes"
export gl_cv_func_expm1_ieee="yes"
export gl_cv_func_fcntl_f_dupfd_works="yes"
export gl_cv_func_fdopendir_works="yes"
export gl_cv_func_floorf_ieee="yes"
export gl_cv_func_fma_works="yes"
export gl_cv_func_fmaf_works="yes"
export gl_cv_func_fmal_works="yes"
export gl_cv_func_fmod_ieee="yes"
export gl_cv_func_fmodf_ieee="yes"
export gl_cv_func_fmodl_ieee="yes"
export gl_cv_func_fpurge_works="yes"
export gl_cv_func_futimens_works="yes"
export gl_cv_func_futimesat_works="yes"
export gl_cv_func_getgroups_works="yes"
export gl_cv_func_gettimeofday_clobber="yes"
export gl_cv_func_hypot_ieee="yes"
export gl_cv_func_hypotf_ieee="yes"
export gl_cv_func_hypotl_ieee="yes"
export gl_cv_func_isfinitel_works="yes"
export gl_cv_func_isnanl_works="yes"
export gl_cv_func_link_works="yes"
export gl_cv_func_linkat_slash="yes"
export gl_cv_func_log10_ieee="yes"
export gl_cv_func_log10f_ieee="yes"
export gl_cv_func_log1p_ieee="yes"
export gl_cv_func_log1pf_ieee="yes"
export gl_cv_func_log1pl_ieee="yes"
export gl_cv_func_log2_ieee="yes"
export gl_cv_func_log2f_ieee="yes"
export gl_cv_func_log_ieee="yes"
export gl_cv_func_logf_ieee="yes"
export gl_cv_func_lstat_dereferences_slashed_symlink="yes"
export gl_cv_func_mbrlen_empty_input="yes"
export gl_cv_func_mbrtowc_empty_input="yes"
export gl_cv_func_memchr_works="yes"
export gl_cv_func_memmem_works_fast="yes"
export gl_cv_func_mkdir_trailing_dot_works="yes"
export gl_cv_func_mkdir_trailing_slash_works="yes"
export gl_cv_func_mkfifo_works="yes"
export gl_cv_func_mknod_works="yes"
export gl_cv_func_modf_ieee="yes"
export gl_cv_func_modff_ieee="yes"
export gl_cv_func_modfl_ieee="yes"
export gl_cv_func_nanosleep="yes"
export gl_cv_func_open_directory_works="yes"
export gl_cv_func_perror_works="yes"
export gl_cv_func_printf_directive_a="yes"
export gl_cv_func_printf_directive_f="yes"
export gl_cv_func_printf_directive_n="yes"
export gl_cv_func_printf_enomem="yes"
export gl_cv_func_printf_flag_zero="yes"
export gl_cv_func_printf_infinite="yes"
export gl_cv_func_printf_infinite_long_double="yes"
export gl_cv_func_printf_sizes_c99="yes"
export gl_cv_func_pselect_detects_ebadf="yes"
export gl_cv_func_ptsname_sets_errno="yes"
export gl_cv_func_readlink_works="yes"
export gl_cv_func_realpath_works="yes"
export gl_cv_func_remainder_ieee="yes"
export gl_cv_func_remainderf_ieee="yes"
export gl_cv_func_remainderl_iee="yes"
export gl_cv_func_rename_dest_works="yes"
export gl_cv_func_rename_link_works="yes"
export gl_cv_func_rename_slash_dst_works="yes"
export gl_cv_func_rename_slash_src_works="yes"
export gl_cv_func_rmdir_works="yes"
export gl_cv_func_round_ieee="yes"
export gl_cv_func_roundf_ieee="yes"
export gl_cv_func_select_detects_ebadf="yes"
export gl_cv_func_setenv_works="yes"
export gl_cv_func_signbit="yes"
export gl_cv_func_signbit_gcc="yes"
export gl_cv_func_sleep_works="yes"
export gl_cv_func_snprintf_directive_n="yes"
export gl_cv_func_snprintf_retval_c99="yes"
export gl_cv_func_snprintf_truncation_c99="yes"
export gl_cv_func_stat_dir_slash="yes"
export gl_cv_func_stat_file_slash="yes"
export gl_cv_func_stpncpy="yes"
export gl_cv_func_strcasestr_linear="yes"
export gl_cv_func_strchrnul_works="yes"
export gl_cv_func_strerror_0_works="yes"
export gl_cv_func_strstr_linear="yes"
export gl_cv_func_strtod_works="yes"
export gl_cv_func_svid_putenv="yes"
export gl_cv_func_symlink_works="yes"
export gl_cv_func_tdelete_works="yes"
export gl_cv_func_trunc_ieee="yes"
export gl_cv_func_truncf_ieee="yes"
export gl_cv_func_truncl_iee="yes"
export gl_cv_func_tzset_clobber="yes"
export gl_cv_func_ungetc_works="yes"
export gl_cv_func_unlink_honors_slashes="yes"
export gl_cv_func_unsetenv_works="yes"
export gl_cv_func_usleep_works="yes"
export gl_cv_func_utimensat_works="yes"
export gl_cv_func_vsnprintf_posix="yes"
export gl_cv_func_vsnprintf_zerosize_c99="yes"
export gl_cv_func_vsprintf_posix="yes"
export gl_cv_func_wcwidth_works="yes"
export gl_cv_func_working_getdelim="yes"
export gl_cv_func_working_mkstemp="yes"
export gl_cv_func_working_mktime="yes"
export gl_cv_func_working_strerror="yes"

mkdir bin/build-gmp
cd bin/build-gmp
../gmp-src/configure --host=x86_64-essence --prefix=/Applications/POSIX --without-readline CC=x86_64-essence-gcc CXX=x86_64-essence-g++
make
make DESTDIR=$SYSROOT install
cd ../..
rm -rf bin/build-gmp

mkdir bin/build-mpfr
cd bin/build-mpfr
../mpfr-src/configure --host=x86_64-essence --prefix=/Applications/POSIX CC=x86_64-essence-gcc CXX=x86_64-essence-g++
make
make DESTDIR=$SYSROOT install
cd ../..
rm -rf bin/build-mpfr

mkdir bin/build-mpc
cd bin/build-mpc
../mpc-src/configure --host=x86_64-essence --prefix=/Applications/POSIX CC=x86_64-essence-gcc CXX=x86_64-essence-g++
make
make DESTDIR=$SYSROOT install
cd ../..
rm -rf bin/build-mpc

mkdir bin/build-binutils
cd bin/build-binutils
../binutils-src/configure --host=x86_64-essence --target=x86_64-essence --prefix=/Applications/POSIX --with-local-prefix=/Applications/POSIX/local --with-build-sysroot=$SYSROOT --without-isl --disable-nls --disable-werror --without-target-bdw-gc CC=x86_64-essence-gcc CXX=x86_64-essence-g++
make -j4
make DESTDIR=$SYSROOT install
cd ../..
rm -rf bin/build-binutils

mkdir bin/build-gcc
cd bin/build-gcc
../gcc-src/configure --host=x86_64-essence --target=x86_64-essence --prefix=/Applications/POSIX --with-local-prefix=/Applications/POSIX/local --with-build-sysroot=$SYSROOT --without-isl --disable-nls --disable-werror --without-target-bdw-gc --enable-languages=c,c++ CC=x86_64-essence-gcc CXX=x86_64-essence-g++ LD=x86_64-essence-ld
make all-gcc -j4
make all-target-libgcc
make DESTDIR=$SYSROOT install-strip-gcc
make DESTDIR=$SYSROOT install-target-libgcc
cd ../..
rm -rf bin/build-gcc

rm -rf bin/gcc-src bin/binutils-src bin/mpc-src bin/gmp-src bin/mpfr-src
