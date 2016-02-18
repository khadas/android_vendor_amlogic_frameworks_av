/*
 * WMA compatible codec
 * Copyright (c) 2002-2007 The FFmpeg Project
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVCODEC_WMA_H
#define AVCODEC_WMA_H

#include "get_bits.h"
#include "util.h"
#include "wma-fixed-debug.h"
#include "wma_dec_api.h"


/* size of blocks */
#define BLOCK_MIN_BITS 7
#define BLOCK_MAX_BITS 11
#define BLOCK_MAX_SIZE (1 << BLOCK_MAX_BITS)

#define BLOCK_NB_SIZES (BLOCK_MAX_BITS - BLOCK_MIN_BITS + 1)

/* XXX: find exact max size */
#define HIGH_BAND_MAX_SIZE 16

#define NB_LSP_COEFS 10

/* XXX: is it a suitable value ? */
#define MAX_CODED_SUPERFRAME_SIZE 16384

#define MAX_CHANNELS 2

#define NOISE_TAB_SIZE 8192

#define LSP_POW_BITS 7

//FIXME should be in wmadec
#define VLCBITS 9
#define VLCMAX ((22+VLCBITS-1)/VLCBITS)

#   define AV_RL16(x)                           \
    ((((const uint8_t*)(x))[1] << 8) |          \
      ((const uint8_t*)(x))[0])

/**
 * all in native-endian format
 */
enum SampleFormat {
    SAMPLE_FMT_NONE = -1,
    SAMPLE_FMT_U8,              ///< unsigned 8 bits
    SAMPLE_FMT_S16,             ///< signed 16 bits
    SAMPLE_FMT_S32,             ///< signed 32 bits
    SAMPLE_FMT_FLT,             ///< float
    SAMPLE_FMT_DBL,             ///< double
    SAMPLE_FMT_NB               ///< Number of sample formats. DO NOT USE if dynamically linking to libavcodec
};

//---------------------------------
//code moidfied by myself:
#ifdef _WIN32 
#define DECLARE_ALIGNED(n,t,v)      t v
#else
#define DECLARE_ALIGNED(n,t,v)      t __attribute__ ((aligned (n))) v
#endif
//------------------------------------
#define FF_ARRAY_ELEMS(a) (sizeof(a) / sizeof((a)[0]))

typedef FIXP fixWMACoef;          ///< type for decoded coefficients, int16_t would be enough for wma 1/2
typedef FIXP FixFFTSample;

typedef struct CoefVLCTable {
    int n;                      ///< total number of codes
    int max_level;
    const uint32_t *huffcodes;  ///< VLC bit values
    const uint8_t *huffbits;    ///< VLC bit size
    const uint16_t *levels;     ///< table to build run/level tables
} CoefVLCTable;

typedef struct WMACodecContext {
    WmaAudioContext* avctx;
    GetBitContext gb;
    //PutBitContext pb;
    int sample_rate;
    int nb_channels;
    int bit_rate;
    int version;                            ///< 1 = 0x160 (WMAV1), 2 = 0x161 (WMAV2)
    int block_align;
    int use_bit_reservoir;
    int use_variable_block_len;
    int use_exp_vlc;                        ///< exponent coding: 0 = lsp, 1 = vlc + delta
    int use_noise_coding;                   ///< true if perceptual noise is added
    int byte_offset_bits;
    VLC exp_vlc;
    int exponent_sizes[BLOCK_NB_SIZES];
    uint16_t exponent_bands[BLOCK_NB_SIZES][25];
    int high_band_start[BLOCK_NB_SIZES];    ///< index of first coef in high band
    int coefs_start;                        ///< first coded coef
    int coefs_end[BLOCK_NB_SIZES];          ///< max number of coded coefficients
    int exponent_high_sizes[BLOCK_NB_SIZES];
    int exponent_high_bands[BLOCK_NB_SIZES][HIGH_BAND_MAX_SIZE];
    VLC hgain_vlc;

    /* coded values in high bands */
    int high_band_coded[MAX_CHANNELS][HIGH_BAND_MAX_SIZE];
    int high_band_values[MAX_CHANNELS][HIGH_BAND_MAX_SIZE];

    /* there are two possible tables for spectral coefficients */
//FIXME the following 3 tables should be shared between decoders
    VLC coef_vlc[2];
    uint16_t *run_table[2];
    FIXP *level_table[2];
    uint16_t *int_table[2];
    const CoefVLCTable *coef_vlcs[2];
    /* frame info */
    int frame_len;                          ///< frame length in samples
    int frame_len_bits;                     ///< frame_len = 1 << frame_len_bits
    int nb_block_sizes;                     ///< number of block sizes
    /* block info */
    int reset_block_lengths;
    int block_len_bits;                     ///< log2 of current block length
    int next_block_len_bits;                ///< log2 of next block length
    int prev_block_len_bits;                ///< log2 of prev block length
    int block_len;                          ///< block length in samples
    int block_num;                          ///< block number in current frame
    int block_pos;                          ///< current position in frame
    uint8_t ms_stereo;                      ///< true if mid/side stereo mode
    uint8_t channel_coded[MAX_CHANNELS];    ///< true if channel is coded
    int exponents_bsize[MAX_CHANNELS];      ///< log2 ratio frame/exp. length
    //DECLARE_ALIGNED(16, float, exponents)[MAX_CHANNELS][BLOCK_MAX_SIZE];//由于该变量会关联到其它编码解码模块,所以暂时保留
    //gas add
    //FIXP exponents_exp[MAX_CHANNELS][BLOCK_MAX_SIZE];
    //FIXP exponents_frac[MAX_CHANNELS][BLOCK_MAX_SIZE];
    DECLARE_ALIGNED(16, FIXPU, fixexponents)[MAX_CHANNELS][BLOCK_MAX_SIZE];//0:20:12
    FIXPU fixmax_exponent[MAX_CHANNELS];
    
    fixWMACoef coefs1[MAX_CHANNELS][BLOCK_MAX_SIZE];
    DECLARE_ALIGNED(16, FIXP, coefs)[MAX_CHANNELS][BLOCK_MAX_SIZE];
    DECLARE_ALIGNED(16, FixFFTSample, output)[BLOCK_MAX_SIZE * 2];
    //FFTContext mdct_ctx[BLOCK_NB_SIZES];
    FIXP *windows[BLOCK_NB_SIZES];
    /* output buffer for one frame and the last for IMDCT windowing */
    DECLARE_ALIGNED(16, FIXP, frame_out)[MAX_CHANNELS][BLOCK_MAX_SIZE * 2];
    /* last frame info */
    uint8_t last_superframe[MAX_CODED_SUPERFRAME_SIZE + 4]; /* padding added */
    int last_bitoffset;
    int last_superframe_len;
    FIXP noise_table[NOISE_TAB_SIZE];
    int noise_index;
    int noise_shift;//gas add
    /* lsp_to_curve tables */
    FIXP lsp_cos_table[BLOCK_MAX_SIZE];
    FIXP lsp_pow_m_table1[(1 << LSP_POW_BITS)];
    FIXP lsp_pow_m_table2[(1 << LSP_POW_BITS)];
    //DSPContext dsp;

#ifdef TRACE
    int frame_count;
#endif
} WMACodecContext;

extern const uint16_t ff_wma_critical_freqs[25];
extern const uint16_t ff_wma_hgain_huffcodes[37];
extern const uint8_t ff_wma_hgain_huffbits[37];
extern const uint32_t ff_aac_scalefactor_code[121];
extern const uint8_t  ff_aac_scalefactor_bits[121];
extern const int32_t fix_cos_table2048[BLOCK_MAX_SIZE];
extern int32_t fix_lsp_pow_m_table1[1<<LSP_POW_BITS];
extern int32_t fix_lsp_pow_m_table2[1<<LSP_POW_BITS];
extern const int32_t sincos_lookup0[1026];
extern const int32_t sincos_lookup1[1024];
extern const uint16_t revtab[1<<12];
extern const uint32_t ff_aac_scalefactor_code[121];
extern const uint8_t ff_aac_scalefactor_bits[121];

int av_cold ff_wma_get_frame_len_bits(int sample_rate, int version,
                                      unsigned int decode_flags);
int ff_wma_init(WmaAudioContext * avctx, int flags2);
int ff_wma_total_gain_to_bits(int total_gain);
int ff_wma_end(WmaAudioContext *avctx);
unsigned int ff_wma_get_large_val(GetBitContext* gb);
int ff_wma_run_level_decode(WmaAudioContext* avctx, GetBitContext* gb,
                            VLC *vlc,
                            const FIXP *level_table, const uint16_t *run_table,
                            int version, fixWMACoef *ptr, int offset,
                            int num_coefs, int block_len, int frame_len_bits,
                            int coef_nb_bits);

int wma_decode_init(WmaAudioContext * avctx);
int wma_decode_superframe(WmaAudioContext *avctx,
                                 void *data, int *data_size,//output
                                 uint8_t *buf, int buf_size);//input
void wma_decode_flush(WmaAudioContext *avctx);
                                 

#endif /* AVCODEC_WMA_H */
