#ifndef __APEDEC_H__
#define __APEDEC_H__

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define av_malloc  malloc
#define av_free    free
#define av_cold
#define av_always_inline inline
#define AVERROR_INVALIDDATA  -1
#define AVERROR_PATCHWELCOME -2
#define AVERROR_EINVAL       -3
#define AVERROR_ENOMEM       -4

#define FFABS(a)      ((a) >= 0 ? (a) : (-(a)))
#define FFMAX(a,b)    ((a) > (b) ? (a) : (b))
#define FFMIN(a,b)    ((a) > (b) ? (b) : (a))
#define FFALIGN(x, a) (((x)+(a)-1)&~((a)-1))
#define INT_MAX        0x7fffffff
#define emms_c()

#define AV_BSWAP16C(x) (((x) << 8 & 0xff00)  | ((x) >> 8 & 0x00ff))
#define AV_BSWAP32C(x) (AV_BSWAP16C(x) << 16 | AV_BSWAP16C((x) >> 16))
#define AV_BSWAP64C(x) (AV_BSWAP32C(x) << 32 | AV_BSWAP32C((x) >> 32))

#ifndef AV_RL16
#define AV_RL16(x)                              \
    ((((const uint8_t*)(x))[1] << 8) |          \
      ((const uint8_t*)(x))[0])
#endif

#ifndef AV_RB32
#define AV_RB32(x)                                    \
    (((uint32_t) ( (const uint8_t*)(x))[0] << 24) |   \
                 (((const uint8_t*)(x))[1] << 16) |   \
                 (((const uint8_t*)(x))[2] <<  8) |   \
                 ( (const uint8_t*)(x))[3])
#endif

#ifndef AV_WB32
#define AV_WB32(p, darg) do {                   \
        unsigned d = (darg);                    \
        ((uint8_t*)(p))[3] = (d);               \
        ((uint8_t*)(p))[2] = (d)>>8;            \
        ((uint8_t*)(p))[1] = (d)>>16;           \
        ((uint8_t*)(p))[0] = (d)>>24;           \
    } while(0)
#endif

#define AV_RB8(x)     (((const uint8_t*)(x))[0])
#define AV_WB8(p, d)  do { ((uint8_t*)(p))[0] = (d); } while(0)

#define DEF(type, name, bytes, read, write)  \
    static av_always_inline type bytestream_get_##name(const uint8_t **b)        \
    {                                                                            \
        (*b) += bytes;                                                           \
        return read(*b - bytes);                                                 \
    }                                                                              


#ifndef av_bswap32
static av_always_inline  uint32_t av_bswap32(uint32_t x)
{
    return AV_BSWAP32C(x);
}
#endif


DEF(unsigned int, be32, 4, AV_RB32, AV_WB32) 
DEF(unsigned int, byte, 1, AV_RB8 , AV_WB8)


static void av_freep(void *arg)
{
    void **ptr= (void**)arg;
    av_free(*ptr);
    *ptr = NULL;
}

static void *av_mallocz(size_t size)
{
    void *ptr = av_malloc(size);
    if (ptr)
        memset(ptr, 0, size);
    return ptr;
}

static inline int ff_fast_malloc(void *ptr, unsigned int *size, size_t min_size, int zero_realloc)
{
    void **p = (void **)ptr;
    if (min_size < *size)
        return 0;
    min_size = FFMAX(17 * min_size / 16 + 32, min_size);
    av_free(*p);
    *p = zero_realloc ? av_mallocz(min_size) : av_malloc(min_size);
    if (!*p)
        min_size = 0;
    *size = min_size;
    return 1;
}
static void av_fast_malloc(void *ptr, unsigned int *size, size_t min_size)
{
    ff_fast_malloc(ptr, size, min_size, 0);
}

static inline  int16_t av_clip_int16(int a)
{
    if ((a+0x8000) & ~0xFFFF) return (a>>31) ^ 0x7FFF;
    else                      return a;
}

static void bswap_buf(uint32_t *dst, const uint32_t *src, int w)
{
    int i;
    for(i=0; i+8<=w; i+=8){
        dst[i+0]= av_bswap32(src[i+0]);
        dst[i+1]= av_bswap32(src[i+1]);
        dst[i+2]= av_bswap32(src[i+2]);
        dst[i+3]= av_bswap32(src[i+3]);
        dst[i+4]= av_bswap32(src[i+4]);
        dst[i+5]= av_bswap32(src[i+5]);
        dst[i+6]= av_bswap32(src[i+6]);
        dst[i+7]= av_bswap32(src[i+7]);
    }
    for(;i<w; i++){
        dst[i+0]= av_bswap32(src[i+0]);
    }
}

static int32_t scalarproduct_and_madd_int16_c(int16_t *v1, const int16_t *v2, const int16_t *v3, int order, int mul)
{
    int res = 0;
    while (order--) {
        res   += *v1 * *v2++;
        *v1++ += mul * *v3++;
    }
    return res;
}


#define MAX_CHANNELS        2
#define MAX_BYTESPERSAMPLE  3

#define APE_FRAMECODE_MONO_SILENCE    1
#define APE_FRAMECODE_STEREO_SILENCE  3
#define APE_FRAMECODE_PSEUDO_STEREO   4

#define HISTORY_SIZE 512
#define PREDICTOR_ORDER 8
/** Total size of all predictor histories */
#define PREDICTOR_SIZE 50

#define YDELAYA (18 + PREDICTOR_ORDER*4)
#define YDELAYB (18 + PREDICTOR_ORDER*3)
#define XDELAYA (18 + PREDICTOR_ORDER*2)
#define XDELAYB (18 + PREDICTOR_ORDER)

#define YADAPTCOEFFSA 18
#define XADAPTCOEFFSA 14
#define YADAPTCOEFFSB 10
#define XADAPTCOEFFSB 5


/**
 * Possible compression levels
 * @{
 */
enum APECompressionLevel {
    COMPRESSION_LEVEL_FAST       = 1000,
    COMPRESSION_LEVEL_NORMAL     = 2000,
    COMPRESSION_LEVEL_HIGH       = 3000,
    COMPRESSION_LEVEL_EXTRA_HIGH = 4000,
    COMPRESSION_LEVEL_INSANE     = 5000
};
/** @} */

#define APE_FILTER_LEVELS 3

/** Filter orders depending on compression level */
static const uint16_t ape_filter_orders[5][APE_FILTER_LEVELS] = {
    {  0,   0,    0 },
    { 16,   0,    0 },
    { 64,   0,    0 },
    { 32, 256,    0 },
    { 16, 256, 1280 }
};

/** Filter fraction bits depending on compression level */
static const uint8_t ape_filter_fracbits[5][APE_FILTER_LEVELS] = {
    {  0,  0,  0 },
    { 11,  0,  0 },
    { 11,  0,  0 },
    { 10, 13,  0 },
    { 11, 13, 15 }
};


/** Filters applied to the decoded data */
typedef struct APEFilter {
    int16_t *coeffs;        ///< actual coefficients used in filtering
    int16_t *adaptcoeffs;   ///< adaptive filter coefficients used for correcting of actual filter coefficients
    int16_t *historybuffer; ///< filter memory
    int16_t *delay;         ///< filtered values

    int avg;
} APEFilter;

typedef struct APERice {
    uint32_t k;
    uint32_t ksum;
} APERice;

typedef struct APERangecoder {
    uint32_t low;           ///< low end of interval
    uint32_t range;         ///< length of interval
    uint32_t help;          ///< bytes_to_follow resp. intermediate value
    unsigned int buffer;    ///< buffer for input/output
} APERangecoder;

/** Filter histories */
typedef struct APEPredictor {
    int32_t *buf;

    int32_t lastA[2];

    int32_t filterA[2];
    int32_t filterB[2];

    int32_t coeffsA[2][4];  ///< adaption coefficients
    int32_t coeffsB[2][5];  ///< adaption coefficients
    int32_t historybuffer[HISTORY_SIZE + PREDICTOR_SIZE];
} APEPredictor;

/** Decoder context */
typedef struct APEContext {
    int channels;
    int samples;                             ///< samples left to decode in current frame
    int bps;

    int fileversion;                         ///< codec version, very important in decoding process
    int compression_level;                   ///< compression levels
    int fset;                                ///< which filter set to use (calculated from compression level)
    int flags;                               ///< global decoder flags

    uint32_t CRC;                            ///< frame CRC
    int frameflags;                          ///< frame flags
    APEPredictor predictor;                  ///< predictor used for final reconstruction

    int32_t *decoded_buffer;
    int decoded_size;
    int32_t *decoded[MAX_CHANNELS];          ///< decoded data for each channel
    int blocks_per_loop;                     ///< maximum number of samples to decode for each call

    int16_t* filterbuf[APE_FILTER_LEVELS];   ///< filter memory

    APERangecoder rc;                        ///< rangecoder used to decode actual values
    APERice riceX;                           ///< rice code parameters for the second channel
    APERice riceY;                           ///< rice code parameters for the first channel
    APEFilter filters[APE_FILTER_LEVELS][2]; ///< filters used for reconstruction

    uint8_t *data;                           ///< current frame data
    uint8_t *data_end;                       ///< frame data end
    int data_size;                           ///< frame data allocated size
    const uint8_t *ptr;                      ///< current position in frame data

    int error;
} APEContext;

int ape_decode_frame(APEContext *s,uint8_t *inbuf,int inlen,void *outbuf,int *outlen);
av_cold int ape_decode_init(APEContext *s,uint8_t *extradata,int extradata_size,int channels,int bits_per_coded_sample);
av_cold int ape_decode_close(APEContext *s);
void ape_flush(APEContext *s);

#ifdef __cplusplus
}
#endif

#endif

