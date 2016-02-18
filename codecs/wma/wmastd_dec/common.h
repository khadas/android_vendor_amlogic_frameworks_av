#ifndef __COMMON_H__
#define __COMMON_H__


//-----------------------------------

#ifndef NULL 
#define NULL 0
#endif

#define INLINE_ASM
//#define FFT_MACRO
//#define BUILD_IN

#ifdef _WIN32
	#undef BUILD_IN
	#undef INLINE_ASM
	#undef FFT_MACRO
#elif defined(INLINE_ASM)
	#undef BUILD_IN
#elif defined(BUILD_IN)
	#undef INLINE_ASM
#endif

//------------------------------------
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
#elif defined(__aarch64__)
typedef unsigned long   uint64_t;
typedef unsigned int    uint32_t;
typedef unsigned short  uint16_t;
typedef unsigned char   uint8_t;
typedef long            int64_t;
typedef signed int      int32_t;
typedef signed short    int16_t;
typedef signed char     int8_t;
typedef float float32_t;

#define INLINE __inline
#define av_always_inline __inline
#define ALWAYS_INLINE __inline
#else

//#include <inttypes.h>
//#include <stdint.h>
typedef unsigned long long  uint64_t;
typedef unsigned int    uint32_t;
typedef unsigned short  uint16_t;
typedef unsigned char   uint8_t;
typedef long long       int64_t;
typedef signed int      int32_t;
typedef signed short    int16_t;
typedef signed char     int8_t;
typedef float float32_t;


#define INLINE __inline
#define av_always_inline __inline
#define ALWAYS_INLINE __inline
#endif




#endif