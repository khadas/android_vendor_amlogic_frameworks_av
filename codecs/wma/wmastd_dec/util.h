
#ifndef __UTIL_H__
#define __UTIL_H__
//-----------------------------
//code modified my myself:
#include "common.h"
//-----------------------------

//#include <stdio.h>
//#include <stdarg.h>

#define av_cold


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
/*
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
*/
#endif

