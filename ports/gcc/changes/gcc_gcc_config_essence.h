#undef TARGET_ESSENCE
#define TARGET_ESSENCE 1
/* Default arguments to ld */
#undef LIB_SPEC
#define LIB_SPEC " -lc "
#undef LINK_SPEC
#define LINK_SPEC " -z max-page-size=0x1000 "
/* Files that are linked before user code. The %s tells GCC to look for these files in the library directory. */
#undef STARTFILE_SPEC
#define STARTFILE_SPEC " %{!shared: crt1.o%s} crti.o%s crtbegin.o%s crtglue.o%s "
/* Files that are linked after user code. */
#undef ENDFILE_SPEC
#define ENDFILE_SPEC " crtend.o%s crtn.o%s "
/* Additional predefined macros. */
#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()        \
  do {                                  \
    builtin_define ("OS_ESSENCE");      \
  } while(0)
