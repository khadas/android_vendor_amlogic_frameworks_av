#ifndef __COMMON_H__
#define __COMMON_H__
#if 0
//-----------------------------------
typedef int (*_compar_lib)(const void *, const void *);
typedef void *(*_malloc_lib)(unsigned int );
typedef void *(*_realloc_lib)(void *,unsigned int);
typedef void  (*_free_lib)(void *);
typedef void *(*_memcpy_lib)(void*,const void*,unsigned int);
typedef void *(*_memset_lib)(void *,int,unsigned int);
typedef void *(*_memmove_lib)(void *,const void *,unsigned int);
typedef void *(*_qsort_lib)(void*,unsigned int,unsigned int,_compar_lib);

#ifndef NULL 
#define NULL 0
#endif

#if defined(_WIN32) && !defined(__MINGW32__)
typedef unsigned __int64 uint64_t;
typedef unsigned __int32 uint32_t;
typedef unsigned __int16 uint16_t;
typedef unsigned __int8 uint8_t;
typedef signed __int64 int64_t;
typedef signed __int32 int32_t;
typedef signed __int16 int16_t;
typedef signed __int8  int8_t;
typedef float float32_t;
#define INLINE __inline
#define av_always_inline __inline
#define ALWAYS_INLINE __inline
#else
//#include <inttypes.h>
#include <stdint.h>
#define  INLINE inline
#define  av_always_inline inline
#define  ALWAYS_INLINE inline
#endif
#endif


#endif




