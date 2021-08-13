/***************************************************************************/
/*                                                                         */
/*  ftstdlib.h                                                             */
/*                                                                         */
/*    ANSI-specific library and header configuration file (specification   */
/*    only).                                                               */
/*                                                                         */
/*  Copyright 2002-2018 by                                                 */
/*  David Turner, Robert Wilhelm, and Werner Lemberg.                      */
/*                                                                         */
/*  This file is part of the FreeType project, and may only be used,       */
/*  modified, and distributed under the terms of the FreeType project      */
/*  license, LICENSE.TXT.  By continuing to use, modify, or distribute     */
/*  this file you indicate that you have read the license and              */
/*  understand and accept it fully.                                        */
/*                                                                         */
/***************************************************************************/


  /*************************************************************************/
  /*                                                                       */
  /* This file is used to group all #includes to the ANSI C library that   */
  /* FreeType normally requires.  It also defines macros to rename the     */
  /* standard functions within the FreeType source code.                   */
  /*                                                                       */
  /* Load a file which defines FTSTDLIB_H_ before this one to override it. */
  /*                                                                       */
  /*************************************************************************/


#ifndef FTSTDLIB_H_
#define FTSTDLIB_H_

#ifndef IncludedEssenceAPIHeader
#define ES_CRT
#define ES_API
#define ES_FORWARD(x) x
#define ES_DIRECT_API
#define ES_EXTERN_FORWARD extern
#include <essence.h>
#endif

#include <stddef.h>

#define ft_ptrdiff_t  ptrdiff_t


  /**********************************************************************/
  /*                                                                    */
  /*                           integer limits                           */
  /*                                                                    */
  /* UINT_MAX and ULONG_MAX are used to automatically compute the size  */
  /* of `int' and `long' in bytes at compile-time.  So far, this works  */
  /* for all platforms the library has been tested on.                  */
  /*                                                                    */
  /* Note that on the extremely rare platforms that do not provide      */
  /* integer types that are _exactly_ 16 and 32 bits wide (e.g. some    */
  /* old Crays where `int' is 36 bits), we do not make any guarantee    */
  /* about the correct behaviour of FT2 with all fonts.                 */
  /*                                                                    */
  /* In these case, `ftconfig.h' will refuse to compile anyway with a   */
  /* message like `couldn't find 32-bit type' or something similar.     */
  /*                                                                    */
  /**********************************************************************/


#include <limits.h>

#define FT_CHAR_BIT    CHAR_BIT
#define FT_USHORT_MAX  USHRT_MAX
#define FT_INT_MAX     INT_MAX
#define FT_INT_MIN     INT_MIN
#define FT_UINT_MAX    UINT_MAX
#define FT_LONG_MIN    LONG_MIN
#define FT_LONG_MAX    LONG_MAX
#define FT_ULONG_MAX   ULONG_MAX


  /**********************************************************************/
  /*                                                                    */
  /*                 character and string processing                    */
  /*                                                                    */
  /**********************************************************************/


// #include <string.h>

#define ft_memchr   EsCRTmemchr
#define ft_memcmp   EsCRTmemcmp
#define ft_memcpy   EsCRTmemcpy
#define ft_memmove  EsCRTmemmove
#define ft_memset   EsCRTmemset
#define ft_strcat   EsCRTstrcat
#define ft_strcmp   EsCRTstrcmp
#define ft_strcpy   EsCRTstrcpy
#define ft_strlen   EsCRTstrlen
#define ft_strncmp  EsCRTstrncmp
#define ft_strncpy  EsCRTstrncpy
#define ft_strrchr  EsCRTstrrchr
#define ft_strstr   EsCRTstrstr


  /**********************************************************************/
  /*                                                                    */
  /*                           file handling                            */
  /*                                                                    */
  /**********************************************************************/


// #include <stdio.h>

#define FT_FILE     FILE
#define ft_fclose   fclose
#define ft_fopen    fopen
#define ft_fread    fread
#define ft_fseek    fseek
#define ft_ftell    ftell
#define ft_sprintf  sprintf


  /**********************************************************************/
  /*                                                                    */
  /*                             sorting                                */
  /*                                                                    */
  /**********************************************************************/


// #include <stdlib.h>

#define ft_qsort  EsCRTqsort


  /**********************************************************************/
  /*                                                                    */
  /*                        memory allocation                           */
  /*                                                                    */
  /**********************************************************************/


#define ft_scalloc  EsCRTcalloc
#define ft_sfree    EsCRTfree
#define ft_smalloc  EsCRTmalloc
#define ft_srealloc EsCRTrealloc


  /**********************************************************************/
  /*                                                                    */
  /*                          miscellaneous                             */
  /*                                                                    */
  /**********************************************************************/


#define ft_strtol EsCRTstrtol
#define ft_getenv EsCRTgetenv


  /**********************************************************************/
  /*                                                                    */
  /*                         execution control                          */
  /*                                                                    */
  /**********************************************************************/


// #include <setjmp.h>

#define ft_jmp_buf     EsCRTjmp_buf  /* note: this cannot be a typedef since */
                                /*       jmp_buf is defined as a macro  */
                                /*       on certain platforms           */

#define ft_longjmp     EsCRTlongjmp
#define ft_setjmp( b ) EsCRTsetjmp( *(ft_jmp_buf*) &(b) ) /* same thing here */


  /* the following is only used for debugging purposes, i.e., if */
  /* FT_DEBUG_LEVEL_ERROR or FT_DEBUG_LEVEL_TRACE are defined    */

#include <stdarg.h>


#endif /* FTSTDLIB_H_ */


/* END */
