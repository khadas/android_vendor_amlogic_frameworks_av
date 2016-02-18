#ifndef __ALAC_H__
#define __ALAC_H__

#include "libavutil/audioconvert.h"
#include "avcodec.h"
#include "get_bits.h"
#include "bytestream.h"
#include "unary.h"
#include "mathops.h"
#include "libavutil/internal.h"

typedef struct {
    AVCodecContext *avctx;
    AVFrame frame;
    GetBitContext gb;
    int channels;

    int32_t *predict_error_buffer[2];
    int32_t *output_samples_buffer[2];
    int32_t *extra_bits_buffer[2];

    uint32_t max_samples_per_frame;
    uint8_t  sample_size;
    uint8_t  rice_history_mult;
    uint8_t  rice_initial_history;
    uint8_t  rice_limit;

    int extra_bits;     /**< number of extra bits beyond 16-bit */
    int nb_samples;     /**< number of samples in the current frame */

    int direct_output;
} ALACContext;

#endif