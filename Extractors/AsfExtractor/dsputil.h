/*
 * DSP utils
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard.
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/**
 * @file dsputil.h
 * DSP utils.
 * note, many functions in here may use MMX which trashes the FPU state, it is
 * absolutely necessary to call emms_c() between dsp & float/double code
 */

#ifndef DSPUTIL_H
#define DSPUTIL_H
#if 0
//#include "common.h"
#include "avcodec.h"




/* pixel operations */
#define MAX_NEG_CROP 384

/* temporary */
extern uint32_t squareTbl[512];
extern uint8_t cropTbl[256 + 2 * MAX_NEG_CROP];


/* minimum alignment rules ;)
if u notice errors in the align stuff, need more alignment for some asm code for some cpu
or need to use a function with less aligned data then send a mail to the ffmpeg-dev list, ...

!warning these alignments might not match reallity, (missing attribute((align)) stuff somewhere possible)
i (michael) didnt check them, these are just the alignents which i think could be reached easily ...

!future video codecs might need functions with less strict alignment
*/



#define CALL_2X_PIXELS(a, b, n)\
static void a(uint8_t *block, const uint8_t *pixels, int line_size, int h){\
    b(block  , pixels  , line_size, h);\
    b(block+n, pixels+n, line_size, h);\
}









#define	BYTE_VEC32(c)	((c)*0x01010101UL)

static __inline uint32_t rnd_avg32(uint32_t a, uint32_t b)
{
    return (a | b) - (((a ^ b) & ~BYTE_VEC32(0x01)) >> 1);
}

static __inline uint32_t no_rnd_avg32(uint32_t a, uint32_t b)
{
    return (a & b) + (((a ^ b) & ~BYTE_VEC32(0x01)) >> 1);
}

/**
 * Empty mmx state.
 * this must be called between any dsp function and float/double code.
 * for example sin(); dsp->idct_put(); emms_c(); cos()
 */
#define emms_c()

/* should be defined by architectures supporting
   one or more MultiMedia extension */


#if defined(HAVE_MMX)

#undef emms_c

#define MM_MMX    0x0001 /* standard MMX */
#define MM_3DNOW  0x0004 /* AMD 3DNOW */
#define MM_MMXEXT 0x0002 /* SSE integer functions or AMD MMX ext */
#define MM_SSE    0x0008 /* SSE functions */
#define MM_SSE2   0x0010 /* PIV SSE2 functions */




#define __align8 __attribute__ ((aligned (8)))

void dsputil_init_mmx(DSPContext* c, AVCodecContext *avctx);
void dsputil_init_pix_mmx(DSPContext* c, AVCodecContext *avctx);

#elif defined(ARCH_ARMV4L)

/* This is to use 4 bytes read to the IDCT pointers for some 'zero'
   line ptimizations */
#define __align8 __attribute__ ((aligned (4)))

void dsputil_init_armv4l(DSPContext* c, AVCodecContext *avctx);

#elif defined(HAVE_MLIB)

/* SPARC/VIS IDCT needs 8-byte aligned DCT blocks */
#define __align8 __attribute__ ((aligned (8)))

void dsputil_init_mlib(DSPContext* c, AVCodecContext *avctx);

#elif defined(ARCH_ALPHA)

#define __align8 __attribute__ ((aligned (8)))

void dsputil_init_alpha(DSPContext* c, AVCodecContext *avctx);

#elif defined(ARCH_POWERPC)

#define MM_ALTIVEC    0x0001 /* standard AltiVec */




#define __align8 __attribute__ ((aligned (16)))



#elif defined(HAVE_MMI)

#define __align8 __attribute__ ((aligned (16)))

void dsputil_init_mmi(DSPContext* c, AVCodecContext *avctx);

#elif defined(ARCH_SH4)

#define __align8 __attribute__ ((aligned (8)))

void dsputil_init_sh4(DSPContext* c, AVCodecContext *avctx);

#else

#define __align8

#endif





/* FFT computation */

/* NOTE: soon integer code will be added, so you must use the
   FFTSample type */
//typedef float FFTSample;
typedef int FFTSample;

typedef struct FFTComplex 
{
    FFTSample re, im;
	//float re,im;
} FFTComplex;

typedef struct FFTContext
{
    int nbits;
    int inverse;
    uint16_t *revtab;
    FFTComplex *exptab;
    FFTComplex *exptab1; /* only used by SSE code */
    void (*fft_calc)(struct FFTContext *s, FFTComplex *z);
} FFTContext;

int fft_inits(FFTContext *s, int i,int nbits, int inverse);

void fft_calc_c(FFTContext *s, FFTComplex *z);
void fft_calc_sse(FFTContext *s, FFTComplex *z);
void fft_calc_altivec(FFTContext *s, FFTComplex *z);

static __inline void fft_calc(FFTContext *s, FFTComplex *z)
{
    s->fft_calc(s, z);
}

/* MDCT computation */

typedef struct MDCTContext
{
    int n;  /* size of MDCT (i.e. number of input data * 2) */
    int nbits; /* n = 2^nbits */
    /* pre/post rotation tables */
    FFTSample *tcos;
    FFTSample *tsin;
    FFTContext fft;
} MDCTContext;

int ff_mdct_init(MDCTContext *s, int i,int nbits, int inverse);
void ff_imdct_calc(MDCTContext *s, FFTSample *output,
                const FFTSample *input, FFTSample *tmp);
//void ff_mdct_calc(MDCTContext *s, FFTSample *out,
 //              const FFTSample *input, FFTSample *tmp);


#ifndef HAVE_LRINTF
/* XXX: add ISOC specific test to avoid specific BSD testing. */
/* better than nothing implementation. */
/* btw, rintf() is existing on fbsd too -- alex */
static __inline long int lrintf(float x)
{
#ifdef CONFIG_WIN32
    /* XXX: incorrect, but make it compile */
    return (int)(x);
#else
    //return (int)(rint(x));
	return (int)(x);
#endif
}
#endif
#endif
#endif
