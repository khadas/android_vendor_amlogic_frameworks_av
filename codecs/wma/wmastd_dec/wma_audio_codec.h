
#ifndef __LYX_AUDIO_CODEC_H__
#define __LYX_AUDIO_CODEC_H__


#include "common.h"
//#include <stdlib.h>
//#include <string.h>
//--------------------------------
#define MAX_EXTRADATA_SIZE 16
#define FFSWAP(type,a,b) do{type SWAP_tmp= b; b= a; a= SWAP_tmp;}while(0)
//----------------------------------
#if 1
#define av_realloc realloc
#define av_malloc malloc
#define av_free free
#else
static inline void *av_mallocz(unsigned int size)
{
    void *ptr = malloc(size);
    if (ptr)
        memset(ptr, 0, size);
    return ptr;
}
static inline void av_free(void *ptr)
{
    //XXX: this test should not be needed on most libcs 
    //if (ptr)
       free(ptr);
}
#endif
//-------------------------------------
#define av_clip_int16    av_clip_int16_c

/**
 * Clip a signed integer value into the -32768,32767 range.
 * @param a value to clip
 * @return clipped value
 */
static INLINE int16_t av_clip_int16_c(int a)
{
    if ((a+0x8000) & ~0xFFFF) return (a>>31) ^ 0x7FFF;
    else                      return a;
}

#ifndef av_log2
#   define av_log2(x) (31 - __builtin_clz((x)|1))
#endif /* av_log2 */

#ifndef av_bswap32
static INLINE uint32_t av_bswap32(uint32_t x)
{
    x= ((x<<8)&0xFF00FF00) | ((x>>8)&0x00FF00FF);
    x= (x>>16) | (x<<16);
    return x;
}
#endif

#define av_be2ne32(x) av_bswap32(x)


#endif

