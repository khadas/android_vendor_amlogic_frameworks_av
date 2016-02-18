
#ifndef __UTIL_H__
#define __UTIL_H__

#include <stdio.h>
#include <stdarg.h>

#define av_cold
//#define av_always_inline inline
#if 0
#define DECLARE_ALIGNED(n,t,v)      t __attribute__ ((aligned (n))) v
#else
#define DECLARE_ALIGNED(n,t,v)      t  v
#endif

/**
 * Required number of additionally allocated bytes at the end of the input bitstream for decoding.
 * This is mainly needed because some optimized bitstream readers read
 * 32 or 64 bit at once and could read over the end.<br>
 * Note: If the first 23 bits of the additional bytes are not 0, then damaged
 * MPEG bitstreams could cause overread and segfault.
 */
#define FF_INPUT_BUFFER_PADDING_SIZE 8


/* av_log API */

#define AV_LOG_QUIET    -8

/**
 * Something went really wrong and we will crash now.
 */
#define AV_LOG_PANIC     0

/**
 * Something went wrong and recovery is not possible.
 * For example, no header was found for a format which depends
 * on headers or an illegal combination of parameters is used.
 */
#define AV_LOG_FATAL     8

/**
 * Something went wrong and cannot losslessly be recovered.
 * However, not all future data is affected.
 */
#define AV_LOG_ERROR    16

/**
 * Something somehow does not look correct. This may or may not
 * lead to problems. An example would be the use of '-vstrict -2'.
 */
#define AV_LOG_WARNING  24

#define AV_LOG_INFO     32
#define AV_LOG_VERBOSE  40

/**
 * Stuff which is only useful for libav* developers.
 */
#define AV_LOG_DEBUG    48


static INLINE void av_log(void* avcl, int level, const char *fmt, ...)
{
    char buffer[128];
    va_list vl;
    va_start(vl, fmt);
    vsprintf(buffer, fmt, vl);
    va_end(vl);
    if(level < AV_LOG_VERBOSE)
        printf("%s", buffer);
}



#endif

