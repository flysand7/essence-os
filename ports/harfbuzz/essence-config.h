#define ALIGNOF_STRUCT_CHAR__ 1
#define HAVE_ATEXIT 1
#define HAVE_CXX11 1
#define HAVE_DLFCN_H 1
#define HAVE_INTEL_ATOMIC_PRIMITIVES 1
#define HAVE_INTTYPES_H 1
#define HAVE_ISATTY 1
#define HAVE_MEMORY_H 1
#define HAVE_MMAP 1
#define HAVE_MPROTECT 1
#define HAVE_PTHREAD 1
#define HAVE_PTHREAD_PRIO_INHERIT 1
#define HAVE_STDBOOL_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_STRTOD_L 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1
#define LT_OBJDIR ".libs/"
#define PACKAGE_BUGREPORT "https://github.com/harfbuzz/harfbuzz/issues/new"
#define PACKAGE_NAME "HarfBuzz"
#define PACKAGE_STRING "HarfBuzz 2.6.4"
#define PACKAGE_TARNAME "harfbuzz"
#define PACKAGE_URL "http://harfbuzz.org/"
#define PACKAGE_VERSION "2.6.4"
#define STDC_HEADERS 1
#define HAVE_FREETYPE 1

#define ES_FORWARD

#define abs EsCRTabs
#define assert EsCRTassert
#define calloc EsCRTcalloc
#define ceil EsCRTceil
#define fabs EsCRTfabs
#define floor EsCRTfloor
#define free EsCRTfree
#define malloc EsCRTmalloc
#define memcmp EsCRTmemcmp
#define memcpy EsCRTmemcpy
#define memmove EsCRTmemmove
#define memset EsCRTmemset
#define qsort EsCRTqsort
#define realloc EsCRTrealloc
#define snprintf EsCRTsnprintf
#define strchr EsCRTstrchr
#define strcmp EsCRTstrcmp
#define strerror EsCRTstrerror
#define strlen EsCRTstrlen
#define strncmp EsCRTstrncmp
#define strncpy EsCRTstrncpy
#define strstr EsCRTstrstr
#define strtol EsCRTstrtol
#define strtoul EsCRTstrtoul

#include <stdarg.h>

#define FLT_MIN 1.17549435082228750797e-38F
#define FLT_MAX 3.40282346638528859812e+38F
#define DBL_MIN 2.22507385850720138309e-308
#define DBL_MAX 1.79769313486231570815e+308

// These are never used in the release build.
struct FILE;
int fprintf(FILE *stream, const char *format, ...);
int vfprintf(FILE *stream, const char *format, va_list ap);
#define stderr ((FILE *) NULL)
