/*
** FAAD - Freeware Advanced Audio Decoder
** Copyright (C) 2002 M. Bakker
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
**
** $Id: common.h,v 1.8 2003/05/26 23:23:26 miguelfreitas Exp $
**/

#ifndef __COMMON_H__
#define __COMMON_H__

#ifdef __cplusplus
extern "C" {
#endif

#if	__STDC_VERSION__+0 >= 199901L
#define INLINE inline
#elif	defined(__GNUC__)
#define INLINE __inline
#else
#define INLINE 
#endif

#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif


/* COMPILE TIME DEFINITIONS */

/* use double precision */
/* #define USE_DOUBLE_PRECISION */
/* use fixed point reals */
//#define FIXED_POINT

#define ERROR_RESILIENCE


/* Allow decoding of MAIN profile AAC */
#define MAIN_DEC
/* Allow decoding of SSR profile AAC */
//#define SSR_DEC
/* Allow decoding of LTP profile AAC */
#define LTP_DEC
/* Allow decoding of LD profile AAC */
#define LD_DEC

/* LD can't do without LTP */
#ifdef LD_DEC
#ifndef ERROR_RESILIENCE
#define ERROR_RESILIENCE
#endif
#ifndef LTP_DEC
#define LTP_DEC
#endif
#endif


/* END COMPILE TIME DEFINITIONS */

#ifndef FIXED_POINT
#define POW_TABLE_SIZE 200
#endif


#if defined(_WIN32)


typedef unsigned __int64 uint64_t;
typedef unsigned __int32 uint32_t;
typedef unsigned __int16 uint16_t;
typedef unsigned __int8 uint8_t;
typedef __int64 int64_t;
typedef __int32 int32_t;
typedef __int16 int16_t;
typedef __int8  int8_t;
typedef float float32_t;


#else

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#if HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#if STDC_HEADERS
# include <stdlib.h>
# include <stddef.h>
#else
# if HAVE_STDLIB_H
#  include <stdlib.h>
# endif
#endif
#if HAVE_STRING_H
# if !STDC_HEADERS && HAVE_MEMORY_H
#  include <memory.h>
# endif
# include <string.h>
#endif
#if HAVE_STRINGS_H
# include <strings.h>
#endif
#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# if HAVE_STDINT_H
#  include <stdint.h>
# else
#  if !defined(__int8_t_defined)
/* we need these... */
#define __int8_t_defined
typedef unsigned long long uint64_t;
typedef unsigned long uint32_t;
typedef unsigned short uint16_t;
typedef unsigned char uint8_t;
typedef long long int64_t;
typedef long int32_t;
typedef short int16_t;
typedef char int8_t;
#  endif
# endif
#endif
#if HAVE_UNISTD_H
# include <unistd.h>
#endif

#ifndef HAVE_FLOAT32_T
typedef float float32_t;
#endif

#if STDC_HEADERS
# include <string.h>
#else
# if !HAVE_STRCHR
#  define strchr index
#  define strrchr rindex
# endif
char *strchr(), *strrchr();
# if !HAVE_MEMCPY
#  define memcpy(d, s, n) bcopy((s), (d), (n))
#  define memmove(d, s, n) bcopy((s), (d), (n))
# endif
#endif

#endif

#ifdef WORDS_BIGENDIAN
#define ARCH_IS_BIG_ENDIAN
#endif

/* FIXED_POINT doesn't work with MAIN and SSR yet */
#ifdef FIXED_POINT
  #undef MAIN_DEC
  #undef SSR_DEC
#endif


#if defined(FIXED_POINT)

  #ifdef HAS_MATHF_H
    #include <mathf.h>
  #else
    #include <math.h>
  #endif

  #include "fixed.h"

#elif defined(USE_DOUBLE_PRECISION)

  typedef double real_t;

  #include <math.h>

  #define MUL(A,B) ((A)*(B))
  #define MUL_C_C(A,B) ((A)*(B))
  #define MUL_R_C(A,B) ((A)*(B))

  #define REAL_CONST(A) ((real_t)A)
  #define COEF_CONST(A) ((real_t)A)

#else /* Normal floating point operation */

  typedef float real_t;

  #define MUL(A,B) ((A)*(B))
  #define MUL_C_C(A,B) ((A)*(B))
  #define MUL_R_C(A,B) ((A)*(B))

  #define REAL_CONST(A) ((real_t)A)
  #define COEF_CONST(A) ((real_t)A)

  #ifdef __ICL /* only Intel C compiler has fmath ??? */

    #include <mathf.h>

    #define sin sinf
    #define cos cosf
    #define log logf
    #define floor floorf
    #define ceil ceilf
    #define sqrt sqrtf

  #else

    #include <math.h>

#ifdef HAVE_SINF
#  define sin sinf
#error
#endif
#ifdef HAVE_COSF
#  define cos cosf
#endif
#ifdef HAVE_LOGF
#  define log logf
#endif
#ifdef HAVE_EXPF
#  define exp expf
#endif
#ifdef HAVE_FLOORF
#  define floor floorf
#endif
#ifdef HAVE_CEILF
#  define ceil ceilf
#endif
#ifdef HAVE_SQRTF
#  define sqrt sqrtf
#endif

  #endif

#endif

typedef real_t complex_t[2];
#define RE(A) A[0]
#define IM(A) A[1]


/* common functions */
uint32_t int_log2(uint32_t val);

uint8_t get_sr_index(uint32_t samplerate);
int8_t can_decode_ot(uint8_t object_type);

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif
#ifndef M_PI_2 /* PI/2 */
#define M_PI_2 1.57079632679489661923
#endif


#ifdef __cplusplus
}
#endif
#endif
