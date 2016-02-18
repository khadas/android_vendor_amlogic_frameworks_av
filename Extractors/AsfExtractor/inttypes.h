#ifndef INTTYPES_H
#define INTTYPES_H
#if 0
typedef char int8_t;
typedef short int16_t;
typedef int int32_t;

//#define int64_t s64_t

typedef int intptr_t1;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
//#define uint64_t u64_t
typedef unsigned int uintptr_t;

#if defined(WIN32)
        typedef signed __int64   int64_t;
        typedef unsigned __int64 uint64_t;
#   else /* other OS */
        typedef signed long long   int64_t;
        typedef unsigned long long uint64_t;
#   endif /* other OS */

#ifdef inline
#undef inline
#define inline 
#endif
#endif
#endif
