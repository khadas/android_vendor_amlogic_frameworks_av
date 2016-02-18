
#ifndef __INTREADWRITE_H__
#define __INTREADWRITE_H__


#include "common.h"
#ifndef UINT_MAX
#define UINT_MAX 0xffffffff
#endif

#define AV_RL64(x)                                   \
    (((uint64_t)((const uint8_t*)(x))[7] << 56) |       \
	((uint64_t)((const uint8_t*)(x))[6] << 48) |       \
	((uint64_t)((const uint8_t*)(x))[5] << 40) |       \
	((uint64_t)((const uint8_t*)(x))[4] << 32) |       \
	((uint64_t)((const uint8_t*)(x))[3] << 24) |       \
	((uint64_t)((const uint8_t*)(x))[2] << 16) |       \
	((uint64_t)((const uint8_t*)(x))[1] <<  8) |       \
(uint64_t)((const uint8_t*)(x))[0])
#define AV_WL64(p, d) do {                   \
	((uint8_t*)(p))[0] = (d);               \
	((uint8_t*)(p))[1] = (d)>>8;            \
	((uint8_t*)(p))[2] = (d)>>16;           \
	((uint8_t*)(p))[3] = (d)>>24;           \
	((uint8_t*)(p))[4] = (d)>>32;           \
	((uint8_t*)(p))[5] = (d)>>40;           \
	((uint8_t*)(p))[6] = (d)>>48;           \
	((uint8_t*)(p))[7] = (d)>>56;           \
    } while(0)



#define AV_WL32(p, d) do {                   \
	((uint8_t*)(p))[0] = (d);               \
	((uint8_t*)(p))[1] = (d)>>8;            \
	((uint8_t*)(p))[2] = (d)>>16;           \
	((uint8_t*)(p))[3] = (d)>>24;           \
    } while(0)
#define AV_RL32(x)                           \
    ((((const uint8_t*)(x))[3] << 24) |         \
	(((const uint8_t*)(x))[2] << 16) |         \
	(((const uint8_t*)(x))[1] <<  8) |         \
((const uint8_t*)(x))[0])


#define AV_RL24(x)                           \
    ((((const uint8_t*)(x))[2] << 16) |         \
	(((const uint8_t*)(x))[1] <<  8) |         \
((const uint8_t*)(x))[0])
#define AV_WL24(p, d) do {                   \
	((uint8_t*)(p))[0] = (d);               \
	((uint8_t*)(p))[1] = (d)>>8;            \
	((uint8_t*)(p))[2] = (d)>>16;           \
    } while(0)

#define AV_RL16(x)                           \
    ((((const uint8_t*)(x))[1] << 8) |          \
((const uint8_t*)(x))[0])
#define AV_WL16(p, d) do {                   \
	((uint8_t*)(p))[0] = (d);               \
	((uint8_t*)(p))[1] = (d)>>8;            \
    } while(0)

#define AV_RB64(x)                                   \
    (((uint64_t)((const uint8_t*)(x))[0] << 56) |       \
	((uint64_t)((const uint8_t*)(x))[1] << 48) |       \
	((uint64_t)((const uint8_t*)(x))[2] << 40) |       \
	((uint64_t)((const uint8_t*)(x))[3] << 32) |       \
	((uint64_t)((const uint8_t*)(x))[4] << 24) |       \
	((uint64_t)((const uint8_t*)(x))[5] << 16) |       \
	((uint64_t)((const uint8_t*)(x))[6] <<  8) |       \
(uint64_t)((const uint8_t*)(x))[7])
#define AV_WB64(p, d) do {                   \
	((uint8_t*)(p))[7] = (d);               \
	((uint8_t*)(p))[6] = (d)>>8;            \
	((uint8_t*)(p))[5] = (d)>>16;           \
	((uint8_t*)(p))[4] = (d)>>24;           \
	((uint8_t*)(p))[3] = (d)>>32;           \
	((uint8_t*)(p))[2] = (d)>>40;           \
	((uint8_t*)(p))[1] = (d)>>48;           \
	((uint8_t*)(p))[0] = (d)>>56;           \
    } while(0)

#define AV_RB32(x)                           \
    ((((const uint8_t*)(x))[0] << 24) |         \
	(((const uint8_t*)(x))[1] << 16) |         \
	(((const uint8_t*)(x))[2] <<  8) |         \
((const uint8_t*)(x))[3])

#define AV_WB32(p, d) do {                   \
	((uint8_t*)(p))[3] = (d);               \
	((uint8_t*)(p))[2] = (d)>>8;            \
	((uint8_t*)(p))[1] = (d)>>16;           \
	((uint8_t*)(p))[0] = (d)>>24;           \
    } while(0)

#define AV_RB24(x)                           \
    ((((const uint8_t*)(x))[0] << 16) |         \
	(((const uint8_t*)(x))[1] <<  8) |         \
((const uint8_t*)(x))[2])

#define AV_WB24(p, d) do {                   \
	((uint8_t*)(p))[2] = (d);               \
	((uint8_t*)(p))[1] = (d)>>8;            \
	((uint8_t*)(p))[0] = (d)>>16;           \
    } while(0)

#define AV_RB16(x)                           \
    ((((const uint8_t*)(x))[0] << 8) |          \
((const uint8_t*)(x))[1])

#define AV_WB16(p, d) do {                   \
	((uint8_t*)(p))[1] = (d);               \
	((uint8_t*)(p))[0] = (d)>>8;            \
    } while(0)

#define AV_RB8(x)     (((const uint8_t*)(x))[0])
#define AV_WB8(p, d)  do { ((uint8_t*)(p))[0] = (d); } while(0)


#endif

