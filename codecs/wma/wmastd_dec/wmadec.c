/*
* WMA compatible decoder
* Copyright (c) 2002 The FFmpeg Project
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

/**
* @file
* WMA compatible decoder.
* This decoder handles Microsoft Windows Media Audio data, versions 1 & 2.
* WMA v1 is identified by audio format 0x160 in Microsoft media files
* (ASF/AVI/WAV). WMA v2 is identified by audio format 0x161.
*
* To use this decoder, a calling application must supply the extra data
* bytes provided with the WMA data. These are the extra, codec-specific
* bytes at the end of a WAVEFORMATEX data structure. Transmit these bytes
* to the decoder using the extradata[_size] fields in AVCodecContext. There
* should be 4 extra bytes for v1 data and 6 extra bytes for v2 data.
*/

#include "wma.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NDEBUG
#define EXPVLCBITS 8
#define EXPMAX ((19+EXPVLCBITS-1)/EXPVLCBITS)

#define HGAINVLCBITS 9
#define HGAINMAX ((13+HGAINVLCBITS-1)/HGAINVLCBITS)

static void wma_lsp_to_curve_init(WMACodecContext *s, int frame_len);

#ifdef __i386__
#define XPROD31(_a, _b, _t, _v, _x, _y)     \
{ *(_x)=MULT31(_a,_t)+MULT31(_b,_v);      \
	*(_y)=MULT31(_b,_t)-MULT31(_a,_v); }
#define XNPROD31(_a, _b, _t, _v, _x, _y)    \
{ *(_x)=MULT31(_a,_t)-MULT31(_b,_v);      \
	*(_y)=MULT31(_b,_t)+MULT31(_a,_v); }

#elif 0
static inline void XPROD31(int32_t a, int32_t b,
						   int32_t t, int32_t v,
						   int32_t *x, int32_t *y)
{
	*x = MULT31(a, t) + MULT31(b, v);
	*y = MULT31(b, t) - MULT31(a, v);
}

static inline void XNPROD31(int32_t a, int32_t b,
							int32_t  t, int32_t  v,
							int32_t *x, int32_t *y)
{
	*x = MULT31(a, t) - MULT31(b, v);
	*y = MULT31(b, t) + MULT31(a, v);
}

#elif defined(ARC)

#define XPROD31(_a, _b, _t, _v, _x, _y)\
{\
	register int32_t ra=(_a), rb=(_b),rt=_t,rv=_v,rx,ry;\
	__asm__ volatile(\
	"mullw 0,%2,%4		\n"\
	"machlw 0,%2,%4		\n"\
	"maclw 0,%3,%5		\n"\
	"machlw %0,%3,%5	\n"\
	"mullw 0,%3,%4		\n"\
	"machlw 0,%3,%4		\n"\
	"negs %2,%2			\n"\
	"maclw 0,%2,%5		\n"\
	"machlw %1,%2,%5	\n"\
	"asl	%0,	%0,1	\n"\
	"asl	%1,	%1,1	\n"\
	:"+r"(rx),"+r"(ry)\
	:"r"(ra),"r"(rb),"r"(rt),"r"(rv)\
	);\
	*(_x)=rx;*(_y)=ry;\
}

#define XNPROD31(_a, _b, _t, _v, _x, _y)\
{\
	register int32_t ra=_a, rb=_b,rt=_t,rv=_v,rx,ry;\
	__asm__ volatile(\
	"mullw 0,%3,%4		\n"\
	"machlw 0,%3,%4		\n"\
	"maclw 0,%2,%5		\n"\
	"machlw %1,%2,%5	\n"\
	"mullw 0,%2,%4		\n"\
	"machlw 0,%2,%4		\n"\
	"negs %3,%3\n"\
	"maclw 0,%3,%5		\n"\
	"machlw %0,%3,%5	\n"\
	"asl	%1,	%1,1	\n"\
	"asl	%0,	%0,1	\n"\
	:"+r"(rx),"+r"(ry)\
	:"r"(ra),"r"(rb),"r"(rt),"r"(rv)\
	);\
	*(_x)=rx; *(_y)=ry;\
}

#else

static INLINE void XPROD31(int32_t a, int32_t b,
						   int32_t t, int32_t v,
						   int32_t *x, int32_t *y)
{
	*x =(int32_t) ((MADD64(FIX64_MUL(a, t), b, v)>>32)<<1);
	*y =(int32_t) ((MSUB64(FIX64_MUL(b, t), a, v)>>32)<<1);

}
static INLINE void XNPROD31(int32_t a, int32_t b,
							int32_t  t, int32_t  v,
							int32_t *x, int32_t *y)
{   
	*x =(int32_t) ((MSUB64(FIX64_MUL(a, t), b, v)>>32)<<1);
	*y =(int32_t) ((MADD64(FIX64_MUL(b, t), a, v)>>32)<<1);
}
#endif




#if 0
#define XPROD31_R(_a, _b, _t, _v, _x, _y)\
{\
	_x = (FIX_MUL32(_a, _t) + FIX_MUL32(_b, _v))<<1;\
	_y = (FIX_MUL32(_b, _t) - FIX_MUL32(_a, _v))<<1;\
}

#define XNPROD31_R(_a, _b, _t, _v, _x, _y)\
{\
	_x = (FIX_MUL32(_a, _t) - FIX_MUL32(_b, _v))<<1;\
	_y = (FIX_MUL32(_b, _t) + FIX_MUL32(_a, _v))<<1;\
}
#elif defined(ARC)

#define XPROD31_R(_a, _b, _t, _v, _x, _y)\
{\
	register int32_t ra=_a, rb=_b,rt=_t,rv=_v,rx=_x,ry=_y;\
	__asm__ volatile(\
	"mullw 0,%2,%4		\n"\
	"machlw 0,%2,%4		\n"\
	"maclw 0,%3,%5		\n"\
	"machlw %0,%3,%5	\n"\
	"mullw 0,%3,%4		\n"\
	"machlw 0,%3,%4		\n"\
	"negs %2,%2			\n"\
	"maclw 0,%2,%5		\n"\
	"machlw %1,%2,%5	\n"\
	"asl	%0,	%0,1	\n"\
	"asl	%1,	%1,1	\n"\
	:"+r"(rx),"+r"(ry)\
	:"r"(ra),"r"(rb),"r"(rt),"r"(rv)\
	);\
	_x=rx;_y=ry;\
}

#define XNPROD31_R(_a, _b, _t, _v, _x, _y)\
{\
	register int32_t ra=_a, rb=_b,rt=_t,rv=_v,rx=_x,ry=_y;\
	__asm__ volatile(\
	"mullw 0,%3,%4		\n"\
	"machlw 0,%3,%4		\n"\
	"maclw 0,%2,%5		\n"\
	"machlw %1,%2,%5	\n"\
	"mullw 0,%2,%4		\n"\
	"machlw 0,%2,%4		\n"\
	"negs %3,%3\n"\
	"maclw 0,%3,%5		\n"\
	"machlw %0,%3,%5	\n"\
	"asl	%1,	%1,1	\n"\
	"asl	%0,	%0,1	\n"\
	:"+r"(rx),"+r"(ry)\
	:"r"(ra),"r"(rb),"r"(rt),"r"(rv)\
	);\
	_x=rx;_y=ry;\
}
#else
#define XPROD31_R(_a, _b, _t, _v, _x, _y)\
{\
	_x =(int32_t) (MADD64(FIX64_MUL(_a, _t), _b, _v)>>32)<<1;\
	_y =(int32_t) (MSUB64(FIX64_MUL(_b, _t), _a, _v)>>32)<<1;\
}

#define XNPROD31_R(_a, _b, _t, _v, _x, _y)\
{\
	_x =(int32_t) (MSUB64(FIX64_MUL(_a, _t), _b, _v)>>32)<<1;\
	_y =(int32_t) (MADD64(FIX64_MUL(_b, _t), _a, _v)>>32)<<1;\
}
#endif




typedef int32_t fixed32; 
typedef int64_t fixed64;

//typedef int32_t FixFFTSample;

typedef struct FixFFTComplex {
	FixFFTSample re, im;
} FixFFTComplex;

typedef struct FixFFTContext {
	int nbits;
	int inverse;
	uint16_t *revtab;
	int mdct_size; /* size of MDCT (i.e. number of input data * 2) */
	int mdct_bits; /* n = 2^nbits */
	/* pre/post rotation tables */
	FixFFTSample *tcos;
	FixFFTSample *tsin;
	void (*fft_permute)(struct FixFFTContext *s, FixFFTComplex *z);
	void (*fft_calc)(struct FixFFTContext *s, FixFFTComplex *z);
	void (*imdct_calc)(struct FixFFTContext *s, FixFFTSample *output, const FixFFTSample *input);
	void (*imdct_half)(struct FixFFTContext *s, FixFFTSample *output, const FixFFTSample *input);
	void (*mdct_calc)(struct FixFFTContext *s, FixFFTSample *output, const FixFFTSample *input);
	int split_radix;
	int permutation;
#define FF_MDCT_PERM_NONE       0
#define FF_MDCT_PERM_INTERLEAVE 1
} FixFFTContext;

/* Use to give gcc hints on which branch is most likely taken */
#if defined(__GNUC__) && __GNUC__ >= 3
#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LIKELY(x)   (x)
#define UNLIKELY(x) (x)
#endif


/* constants for fft_16 (same constants as in mdct_arm.S ... ) */
#define cPI1_8 (0x7641af3d) /* cos(pi/8) s.31 */
#define cPI2_8 (0x5a82799a) /* cos(2pi/8) = 1/sqrt(2) s.31 */
#define cPI3_8 (0x30fbc54d) /* cos(3pi/8) s.31 */



#define BF(x,y,a,b) {\
	x = a - b;\
	y = a + b;\
}

#define BF_REV(x,y,a,b) {\
	x = a + b;\
	y = a - b;\
}

#define BUTTERFLIES(a0,a1,a2,a3) {\
{\
	FixFFTSample temp1,temp2;\
	BF(temp1, temp2, t5, t1);\
	BF(a2.re, a0.re, a0.re, temp2);\
	BF(a3.im, a1.im, a1.im, temp1);\
}\
{\
	FixFFTSample temp1,temp2;\
	BF(temp1, temp2, t2, t6);\
	BF(a3.re, a1.re, a1.re, temp1);\
	BF(a2.im, a0.im, a0.im, temp2);\
}\
}


#define BUTTERFLIES_BIG(a0,a1,a2,a3) {\
	FixFFTSample r0=a0.re, i0=a0.im, r1=a1.re, i1=a1.im;\
{\
	FixFFTSample temp1, temp2;\
	BF(temp1, temp2, t5, t1);\
	BF(a2.re, a0.re, r0, temp2);\
	BF(a3.im, a1.im, i1, temp1);\
}\
{\
	FixFFTSample temp1, temp2;\
	BF(temp1, temp2, t2, t6);\
	BF(a3.re, a1.re, r1, temp1);\
	BF(a2.im, a0.im, i0, temp2);\
}\
}

static INLINE void TRANSFORM(FixFFTComplex * z, unsigned int n, FixFFTSample wre, FixFFTSample wim)
{
	register FixFFTSample t1,t2,t5,t6,r_re,r_im;
	r_re = z[n*2].re;
	r_im = z[n*2].im;
	XPROD31_R(r_re, r_im, wre, wim, t1,t2);
	r_re = z[n*3].re;
	r_im = z[n*3].im;
	XNPROD31_R(r_re, r_im, wre, wim, t5,t6);
	BUTTERFLIES(z[0],z[n],z[n*2],z[n*3]);
}

static INLINE void TRANSFORM_W01(FixFFTComplex * z, unsigned int n, const FixFFTSample * w)
{
	register const FixFFTSample wre=w[0],wim=w[1];
	register FixFFTSample t1,t2,t5,t6,r_re,r_im;
	r_re = z[n*2].re;
	r_im = z[n*2].im;
	XPROD31_R(r_re, r_im, wre, wim, t1,t2);
	r_re = z[n*3].re;
	r_im = z[n*3].im;
	XNPROD31_R(r_re, r_im, wre, wim, t5,t6);
	BUTTERFLIES(z[0],z[n],z[n*2],z[n*3]);
}

static INLINE void TRANSFORM_W10(FixFFTComplex * z, unsigned int n, const FixFFTSample * w)
{
	register const FixFFTSample wim=w[0],wre=w[1];
	register FixFFTSample t1,t2,t5,t6,r_re,r_im;
	r_re = z[n*2].re;
	r_im = z[n*2].im;
	XPROD31_R(r_re, r_im, wre, wim, t1,t2);
	r_re = z[n*3].re;
	r_im = z[n*3].im;
	XNPROD31_R(r_re, r_im, wre, wim, t5,t6);
	BUTTERFLIES(z[0],z[n],z[n*2],z[n*3]);
}

static INLINE void TRANSFORM_EQUAL(FixFFTComplex * z, unsigned int n)
{
	register FixFFTSample t1,t2,t5,t6,temp1,temp2;
	register FixFFTSample * my_z = (FixFFTSample *)(z);
	my_z += n*4;
	t2    = MULT31(my_z[0], cPI2_8);
	temp1 = MULT31(my_z[1], cPI2_8);
	my_z += n*2;
	temp2 = MULT31(my_z[0], cPI2_8);
	t5    = MULT31(my_z[1], cPI2_8);
	t1 = ( temp1 + t2 );
	t2 = ( temp1 - t2 );
	t6 = ( temp2 + t5 );
	t5 = ( temp2 - t5 );
	my_z -= n*6;
	BUTTERFLIES(z[0],z[n],z[n*2],z[n*3]);
}

static INLINE void TRANSFORM_ZERO(FixFFTComplex * z, unsigned int n)
{
	FixFFTSample t1,t2,t5,t6;
	t1 = z[n*2].re;
	t2 = z[n*2].im;
	t5 = z[n*3].re;
	t6 = z[n*3].im;
	BUTTERFLIES(z[0],z[n],z[n*2],z[n*3]);
}

/* z[0...8n-1], w[1...2n-1] */
void pass(FixFFTComplex *z_arg, unsigned int STEP_arg, unsigned int n_arg)
{
	register FixFFTComplex * z = z_arg;
	register unsigned int STEP = STEP_arg;
	register unsigned int n = n_arg;

	register const FixFFTSample *w = sincos_lookup0+STEP;
	/* wre = *(wim+1) .  ordering is sin,cos */
	register const FixFFTSample *w_end = sincos_lookup0+1024;

	/* first two are special (well, first one is special, but we need to do pairs) */
	TRANSFORM_ZERO(z,n);
	z++;
	TRANSFORM_W10(z,n,w);
	w += STEP;
	/* first pass forwards through sincos_lookup0*/
	do {
		z++;
		TRANSFORM_W10(z,n,w);
		w += STEP;
		z++;
		TRANSFORM_W10(z,n,w);
		w += STEP;
	} while(LIKELY(w < w_end));
	/* second half: pass backwards through sincos_lookup0*/
	/* wim and wre are now in opposite places so ordering now [0],[1] */
	w_end=sincos_lookup0;
	while(LIKELY(w>w_end))
	{
		z++;
		TRANSFORM_W01(z,n,w);
		w -= STEP;
		z++;
		TRANSFORM_W01(z,n,w);
		w -= STEP;
	}
}

/* what is STEP?
sincos_lookup0 has sin,cos pairs for 1/4 cycle, in 1024 points
so half cycle would be 2048 points
ff_cos_16 has 8 elements corresponding to 4 cos points and 4 sin points
so each of the 4 points pairs corresponds to a 256*2-byte jump in sincos_lookup0
8192/16 (from "ff_cos_16") is 512 bytes.
i.e.  for fft16, STEP = 8192/16 */
#define DECL_FFT(n,n2,n4)\
	void fft##n(FixFFTComplex *z)\
{\
	fft##n2(z);\
	fft##n4(z+n4*2);\
	fft##n4(z+n4*3);\
	pass(z,8192/n,n4);\
}

//-----------------------------------
#ifndef FFT_MACRO
static INLINE void fft4(FixFFTComplex *z)
{
	FixFFTSample t1, t2, t3, t4, t5, t6, t7, t8;
	BF(t3, t1, z[0].re, z[1].re); 
	BF(t8, t6, z[3].re, z[2].re); 
	BF(z[2].re, z[0].re, t1, t6); 
	BF(t4, t2, z[0].im, z[1].im); 
	BF(t7, t5, z[2].im, z[3].im); 
	BF(z[3].im, z[1].im, t4, t8); 
	BF(z[3].re, z[1].re, t3, t7); 
	BF(z[2].im, z[0].im, t2, t5); 
}
#else
#define fft4(Z)  \
{\
	FixFFTSample t1, t2, t3, t4, t5, t6, t7, t8;\
	FixFFTComplex *tz=Z;\
	BF(t3, t1, tz[0].re, tz[1].re);\ 
BF(t8, t6, tz[3].re, tz[2].re);\ 
BF(tz[2].re, tz[0].re, t1, t6);\ 
BF(t4, t2, tz[0].im, tz[1].im);\ 
BF(t7, t5, tz[2].im, tz[3].im);\ 
BF(tz[3].im, tz[1].im, t4, t8);\ 
BF(tz[3].re, tz[1].re, t3, t7);\ 
BF(tz[2].im, tz[0].im, t2, t5);\ 
}
#endif

//-----------------------------------------
#ifndef FFT_MACRO
static INLINE void fft8(FixFFTComplex *z)
{

	FixFFTSample t1,t2,t3,t4,t7,t8;
	fft4(z);
	BF(t1, z[5].re, z[4].re, -z[5].re);
	BF(t2, z[5].im, z[4].im, -z[5].im);
	BF(t3, z[7].re, z[6].re, -z[7].re);
	BF(t4, z[7].im, z[6].im, -z[7].im);
	BF(t8, t1, t3, t1);
	BF(t7, t2, t2, t4);
	BF(z[4].re, z[0].re, z[0].re, t1);
	BF(z[4].im, z[0].im, z[0].im, t2);
	BF(z[6].re, z[2].re, z[2].re, t7);
	BF(z[6].im, z[2].im, z[2].im, t8);
	z++;
	TRANSFORM_EQUAL(z,2);
}
#else
#define fft8(Z)    \
{\
	FixFFTSample t1,t2,t3,t4,t7,t8;\
	FixFFTComplex *z8=Z;\
	fft4(z8);\
	BF(t1, z8[5].re, z8[4].re, -z8[5].re);\
	BF(t2, z8[5].im, z8[4].im, -z8[5].im);\
	BF(t3, z8[7].re, z8[6].re, -z8[7].re);\
	BF(t4, z8[7].im, z8[6].im, -z8[7].im);\
	BF(t8, t1, t3, t1);\
	BF(t7, t2, t2, t4);\
	BF(z8[4].re, z8[0].re, z8[0].re, t1);\
	BF(z8[4].im, z8[0].im, z8[0].im, t2);\
	BF(z8[6].re, z8[2].re, z8[2].re, t7);\
	BF(z8[6].im, z8[2].im, z8[2].im, t8);\
	z8++;\
	TRANSFORM_EQUAL(z8,2);\
}
#endif

//-----------------------------------------
#if 1
void fft16(FixFFTComplex *z){
	fft8(z);
	fft4(z+8);
	fft4(z+12);
	TRANSFORM_ZERO(z,4);
	z+=2;
	TRANSFORM_EQUAL(z,4);
	z-=1;
	TRANSFORM(z,4,cPI1_8,cPI3_8);
	z+=2;
	TRANSFORM(z,4,cPI3_8,cPI1_8);
}
#else
#define fft16(Z) \
{\ 
FixFFTComplex *z16=Z;\
fft8(z16);\
fft4(z16+8);\
fft4(z16+12);\
TRANSFORM_ZERO(z16,4);\
z16+=2;\
TRANSFORM_EQUAL(z16,4);\
z16-=1;\
TRANSFORM(z16,4,cPI1_8,cPI3_8);\
z16+=2;\
TRANSFORM(z16,4,cPI3_8,cPI1_8);\
}
#endif

//-----------------------------------------
DECL_FFT(32,16,8)
DECL_FFT(64,32,16)
DECL_FFT(128,64,32)
DECL_FFT(256,128,64)
DECL_FFT(512,256,128)
DECL_FFT(1024,512,256)
DECL_FFT(2048,1024,512)
DECL_FFT(4096,2048,1024)


//-------------------------------------------------
static void fft4_dispatch(FixFFTComplex *z){
	fft4(z);
}
static void fft8_dispatch(FixFFTComplex *z){
	fft8(z);
}
static void fft16_dispatch(FixFFTComplex *z){
	fft16(z);
}
static void fft32_dispatch(FixFFTComplex *z){
	fft32(z);
}
static void fft64_dispatch(FixFFTComplex *z){
	fft64(z);
}
static void fft128_dispatch(FixFFTComplex *z){
	fft128(z);
}
static void fft256_dispatch(FixFFTComplex *z){
	fft256(z);
}
static void fft512_dispatch(FixFFTComplex *z){
	fft512(z);
}
static void fft1024_dispatch(FixFFTComplex *z){
	fft1024(z);
}
static void fft2048_dispatch(FixFFTComplex *z){
	fft2048(z);
}
static void fft4096_dispatch(FixFFTComplex *z){
	fft4096(z);
}
//----------------------------------------------------


static void (*fixfft_dispatch[])(FixFFTComplex*) = {
	fft4_dispatch,   fft8_dispatch,   fft16_dispatch, 
	fft32_dispatch,  fft64_dispatch,  fft128_dispatch, 
	fft256_dispatch, fft512_dispatch, fft1024_dispatch,
	fft2048_dispatch,fft4096_dispatch
};

void fix_fft_calc_c(int nbits, FixFFTComplex *z)
{
	fixfft_dispatch[nbits-2](z);
}

void fix_imdct_half(unsigned int nbits, fixed32 *output, const fixed32 *input)
{
	int n8, n4, n2, n, j;
	const fixed32 *in1, *in2;
	//--------------------------
	FixFFTComplex *z;
	const int revtab_shift=(14- nbits);
	const int32_t *T=sincos_lookup0;
	const int step= 2<<(12-nbits);
	const uint16_t * p_revtab=revtab;
	//-------------------------
	n = 1 << nbits;
	n2 = n >> 1;
	n4 = n >> 2;
	n8 = n >> 3;
	z = (FixFFTComplex *)output;
	/* pre rotation */
	in1 = input;
	in2 = input + n2 - 1;
	{
		const uint16_t * const p_revtab_end = p_revtab + n8;
		while(LIKELY(p_revtab < p_revtab_end))
		{   

			j = (*p_revtab)>>revtab_shift;
			XNPROD31(*in2, *in1, T[1], T[0], &z[j].re, &z[j].im );
			T += step;
			in1 += 2;
			in2 -= 2;
			p_revtab++;
			j = (*p_revtab)>>revtab_shift;
			XNPROD31(*in2,*in1, T[1], T[0], &z[j].re, &z[j].im );

			T += step;
			in1 += 2;
			in2 -= 2;
			p_revtab++;
		}
	}
	{
		const uint16_t * const p_revtab_end = p_revtab + n8;
		while(LIKELY(p_revtab < p_revtab_end))
		{    
			j = (*p_revtab)>>revtab_shift;
			XNPROD31(*in2, *in1, T[0], T[1], &z[j].re, &z[j].im);
			T -= step;
			in1 += 2;
			in2 -= 2;
			p_revtab++;
			j = (*p_revtab)>>revtab_shift;
			XNPROD31(*in2, *in1, T[0], T[1], &z[j].re, &z[j].im);
			T -= step;
			in1 += 2;
			in2 -= 2;
			p_revtab++;
		}
	}


	fix_fft_calc_c(nbits-2, z);

	switch( nbits )
	{
	default:
		{
			fixed32 * z1 = (fixed32 *)(&z[0]);
			fixed32 * z2 = (fixed32 *)(&z[n4-1]);
			int magic_step = step>>2;
			int newstep;
			if(n<=1024)
			{
				T = sincos_lookup0 + magic_step;
				newstep = step>>1;
			}
			else
			{   
				T = sincos_lookup1;
				newstep = 2;
			}

			while(z1<z2)
			{   
				fixed32 r0,i0,r1,i1;
				XNPROD31_R(z1[1], z1[0], T[0], T[1], r0, i1); 
				T+=newstep;
				XNPROD31_R(z2[1], z2[0], T[1], T[0], r1, i0);
				T+=newstep;
				z1[0] = -r0;
				z1[1] = -i0;
				z2[0] = -r1;
				z2[1] = -i1;
				z1+=2;
				z2-=2;
			}

			break;
		}

	case 12: /* n=4096 */
		{
			int32_t t0,t1,v0,v1;
			const int32_t * V = sincos_lookup1;
			fixed32 * z1 = (fixed32 *)(&z[0]);
			fixed32 * z2 = (fixed32 *)(&z[n4-1]);
			T = sincos_lookup0;
			t0 = T[0]>>1; t1=T[1]>>1;

			while(z1<z2)
			{   

				fixed32 r0,i0,r1,i1;
				t0 += (v0 = (V[0]>>1));
				t1 += (v1 = (V[1]>>1));
				XNPROD31_R(z1[1], z1[0], t0, t1, r0, i1 );            
				T+=2;
				v0 += (t0 = (T[0]>>1));
				v1 += (t1 = (T[1]>>1));
				XNPROD31_R(z2[1], z2[0], v1, v0, r1, i0 );
				z1[0] = -r0;
				z1[1] = -i0;
				z2[0] = -r1;
				z2[1] = -i1;
				z1+=2;
				z2-=2;
				V+=2;
			}

			break;
		}

	case 13: /* n = 8192 */
		{
			const int32_t * V = sincos_lookup1;
			int32_t t0,t1,v0,v1,q0,q1;
			fixed32 * z1 = (fixed32 *)(&z[0]);
			fixed32 * z2 = (fixed32 *)(&z[n4-1]);
			T = sincos_lookup0;
			t0 = T[0]; t1=T[1];

			while(z1<z2)
			{
				fixed32 r0,i0,r1,i1;
				v0 = V[0]; v1 = V[1];
				t0 += (q0 = (v0-t0)>>1);
				t1 += (q1 = (v1-t1)>>1);
				XNPROD31_R(z1[1], z1[0], t0, t1, r0, i1 );
				t0 = v0-q0;
				t1 = v1-q1;
				XNPROD31_R(z2[1], z2[0], t1, t0, r1, i0 );
				z1[0] = -r0;
				z1[1] = -i0;
				z2[0] = -r1;
				z2[1] = -i1;
				z1+=2;
				z2-=2;
				T+=2;

				t0 = T[0]; t1 = T[1];
				v0 += (q0 = (t0-v0)>>1);
				v1 += (q1 = (t1-v1)>>1);
				XNPROD31_R(z1[1], z1[0], v0, v1, r0, i1 );
				v0 = t0-q0;
				v1 = t1-q1;
				XNPROD31_R(z2[1], z2[0], v1, v0, r1, i0 );
				z1[0] = -r0;
				z1[1] = -i0;
				z2[0] = -r1;
				z2[1] = -i1;
				z1+=2;
				z2-=2;
				V+=2;
			}
			break;
		}
	}
} 

void fix_imdct_calc(unsigned int nbits, fixed32 *output, const fixed32 *input)
{
	const int n = (1<<nbits);
	const int n2 = (n>>1);
	const int n4 = (n>>2);
	fixed32 * in_r, * in_r2, * out_r, * out_r2;

	fix_imdct_half(nbits,output+n2,input);

	out_r = output;
	out_r2 = output+n2-8;
	in_r  = output+n2+n4-8;
	while(out_r<out_r2)
	{
		out_r[0]     = -(out_r2[7] = in_r[7]);
		out_r[1]     = -(out_r2[6] = in_r[6]);
		out_r[2]     = -(out_r2[5] = in_r[5]);
		out_r[3]     = -(out_r2[4] = in_r[4]);
		out_r[4]     = -(out_r2[3] = in_r[3]);
		out_r[5]     = -(out_r2[2] = in_r[2]);
		out_r[6]     = -(out_r2[1] = in_r[1]);
		out_r[7]     = -(out_r2[0] = in_r[0]);
		in_r -= 8;
		out_r += 8;
		out_r2 -= 8;
	}
	in_r = output + n2+n4;
	in_r2 = output + n-4;
	out_r = output + n2;
	out_r2 = output + n2 + n4 - 4;
	while(in_r<in_r2)
	{
		register fixed32 t0,t1,t2,t3;
		register fixed32 s0,s1,s2,s3;

		t0=in_r[0]; t1=in_r[1]; t2=in_r[2]; t3=in_r[3];
		out_r[0]=t0;out_r[1]=t1;out_r[2]=t2;out_r[3]=t3;
		s0=in_r2[0];s1=in_r2[1];s2=in_r2[2];s3=in_r2[3];
		out_r2[0]=s0;out_r2[1]=s1;out_r2[2]=s2;out_r2[3]=s3;
		in_r[0]=s3;in_r[1]=s2;in_r[2]=s1;in_r[3]=s0;
		in_r2[0]=t3;in_r2[1]=t2;in_r2[2]=t1;in_r2[3]=t0;

		in_r += 4;
		in_r2 -= 4;
		out_r += 4;
		out_r2 -= 4;
	}
}



int wma_decode_init(WmaAudioContext * avctx)
{
	//static WMACodecContext static_s;
	//WMACodecContext *s = avctx->priv_data = &static_s;
	WMACodecContext *s = avctx->priv_data =malloc(sizeof(WMACodecContext));
	if(s==NULL)
		return WMA_DEC_ERR_INIT;
	int flags2;
	uint8_t *extradata;
    memset(s,0,sizeof(WMACodecContext));
	s->avctx = avctx;
     
	/* extract flag infos */
	flags2 = 0;
	extradata = avctx->extradata;
	if (avctx->codec_id == CODEC_ID_WMAV11 && avctx->extradata_size >= 4) {
		flags2 = AV_RL16(extradata+2);
	} else if (avctx->codec_id == CODEC_ID_WMAV22 && avctx->extradata_size >= 6) {
		flags2 = AV_RL16(extradata+4);
	}

	s->use_exp_vlc = flags2 & 0x0001;

	s->use_bit_reservoir = flags2 & 0x0002;

	s->use_variable_block_len = flags2 & 0x0004;

	if(ff_wma_init(avctx, flags2)<0)
		return WMA_DEC_ERR_INIT;

	if (s->use_noise_coding) {
		init_vlc(&s->hgain_vlc, HGAINVLCBITS, sizeof(ff_wma_hgain_huffbits),
			ff_wma_hgain_huffbits, 1, 1,
			ff_wma_hgain_huffcodes, 2, 2, 0);
	}

	if (s->use_exp_vlc) {
		init_vlc(&s->exp_vlc, EXPVLCBITS, sizeof(ff_aac_scalefactor_bits), //FIXME move out of context
			ff_aac_scalefactor_bits, 1, 1,
			ff_aac_scalefactor_code, 4, 4, 0);
	} else {
		wma_lsp_to_curve_init(s, s->frame_len);
	}

	avctx->sample_fmt = SAMPLE_FMT_S16;
	return 0;
}

static INLINE FIXPU pow_m1_4_fix(WMACodecContext *s, int exp, uint32_t frac)
{
	FIXPU fixm, re;
	FIXP fixa, fixb, ie;
	static const uint32_t pow2_table[] =
	{
		QU1_CONST(1.0),
		QU1_CONST(1.1892071150027210667174999705605), 
		QU1_CONST(1.4142135623730950488016887242097), 
		QU1_CONST(1.6817928305074290860622509524664) 
	};

	exp++;
	ie = (-exp)>>2;
	re = (-exp)&3;
	re = pow2_table[re];
	fixm = frac>>(31-LSP_POW_BITS) & ((1 << LSP_POW_BITS) - 1);
	/* build interpolation scale: 1 <= t < 2. */
	fixa = (s->lsp_pow_m_table1[fixm]);
	fixb = (s->lsp_pow_m_table2[fixm]);
	fixm = ((frac<<(LSP_POW_BITS-1)) & 0x7fffffff) | 0x40000000;
	fixm = fixa + (FIX_MUL32(fixb, fixm)>>7);
	fixm = FIXU_MUL(re, fixm, (31+30-14)-ie);
	return fixm;
}

static void wma_lsp_to_curve_init(WMACodecContext *s, int frame_len)
{
	int i, m;

	m = BLOCK_MAX_SIZE/frame_len;
	for(i=0;i<frame_len;i++)
		s->lsp_cos_table[i] = (fix_cos_table2048[i*m]);

	/* tables for x^-0.25 computation */

	/* NOTE: these two tables are needed to avoid two operations in
	pow_m1_4 */
	for(i=(1 << LSP_POW_BITS) - 1;i>=0;i--) {
		s->lsp_pow_m_table1[i] = (fix_lsp_pow_m_table1[i]);
		s->lsp_pow_m_table2[i] = (fix_lsp_pow_m_table2[i]);
	}
}

/**
* NOTE: We use the same code as Vorbis here
* @todo optimize it further with SSE/3Dnow
*/
static void wma_lsp_to_curve(WMACodecContext *s,
							 FIXPU *out, FIXPU *val_max_ptr,
							 int n, FIXP *lsp)
{
	int i, j;
	FIXPU fixp, fixq, fixv;
	FIXP fixw, fixval_max=0;
	int p_exp, q_exp;
	int tmpexp = 0;

	for(i=0;i<n;i++) {
		fixp = QU0_CONST(0.5f); fixq = QU0_CONST(0.5f);
		p_exp = q_exp = 0;
		fixw = (s->lsp_cos_table[i]);
		for(j=1;j<NB_LSP_COEFS;j+=2){
			fixq = FIXU_MUL32(fixq, FASTABS(fixw-(lsp[j - 1])));
			fixp = FIXU_MUL32(fixp, FASTABS(fixw-(lsp[j])));
			tmpexp = FASTCLZ(fixq);
			fixq <<= tmpexp;
			q_exp += 3-tmpexp;
			tmpexp = FASTCLZ(fixp);
			fixp <<= tmpexp;
			p_exp += 3-tmpexp;
		}
		fixp = FIXU_MUL32(fixp, fixp);
		fixq = FIXU_MUL32(fixq, fixq);
		p_exp <<= 1;
		q_exp <<= 1;
		fixp = FIXU_MUL32(fixp, Q3_CONST(2.0f)-(fixw>>1));
		fixq = FIXU_MUL32(fixq, Q3_CONST(2.0f)+(fixw>>1));
		if(p_exp >= q_exp)
		{
			tmpexp = (p_exp-q_exp);
			if(tmpexp > 31) fixq = 0;
			else fixq = fixq>>tmpexp;
			fixv = (fixp) + (fixq);
			p_exp = p_exp;
		}
		else
		{
			tmpexp = (q_exp-p_exp);
			if(tmpexp > 31) fixp = 0;
			else fixp = fixp>>tmpexp;
			fixv = (fixp) + (fixq);
			p_exp = q_exp;
		}
		tmpexp = FASTCLZ(fixv);
		fixv <<= tmpexp;
		p_exp += 3-tmpexp;
		fixv = pow_m1_4_fix(s, p_exp, fixv);
		if (fixv > fixval_max)
			fixval_max = fixv;
		out[i] = (fixv);;
	}
	*val_max_ptr = (fixval_max);
}

const FIXP fix_wma_lsp_codebook[NB_LSP_COEFS][16] = {
	{ Q2_CONST(1.98732877), Q2_CONST(1.97944528), Q2_CONST(1.97179088), Q2_CONST(1.96260549), Q2_CONST(1.95038374), Q2_CONST(1.93336114), Q2_CONST(1.90719232), Q2_CONST(1.86191415), },
	{ Q2_CONST(1.97260000), Q2_CONST(1.96083160), Q2_CONST(1.94982586), Q2_CONST(1.93806164), Q2_CONST(1.92516608), Q2_CONST(1.91010199), Q2_CONST(1.89232331), Q2_CONST(1.87149812),
	Q2_CONST(1.84564818), Q2_CONST(1.81358067), Q2_CONST(1.77620070), Q2_CONST(1.73265264), Q2_CONST(1.67907855), Q2_CONST(1.60959081), Q2_CONST(1.50829650), Q2_CONST(1.33120330), },
	{ Q2_CONST(1.90109110), Q2_CONST(1.86482426), Q2_CONST(1.83419671), Q2_CONST(1.80168452), Q2_CONST(1.76650116), Q2_CONST(1.72816320), Q2_CONST(1.68502700), Q2_CONST(1.63738256),
	Q2_CONST(1.58501580), Q2_CONST(1.51795181), Q2_CONST(1.43679906), Q2_CONST(1.33950585), Q2_CONST(1.24176208), Q2_CONST(1.12260729), Q2_CONST(0.96749668), Q2_CONST(0.74048265), },
	{ Q2_CONST(1.76943864), Q2_CONST(1.67822463), Q2_CONST(1.59946365), Q2_CONST(1.53560582), Q2_CONST(1.47470796), Q2_CONST(1.41210167), Q2_CONST(1.34509536), Q2_CONST(1.27339507),
	Q2_CONST(1.19303814), Q2_CONST(1.09765169), Q2_CONST(0.98818722), Q2_CONST(0.87239446), Q2_CONST(0.74369172), Q2_CONST(0.59768184), Q2_CONST(0.43168630), Q2_CONST(0.17977021), },
	{ Q2_CONST(1.43428349), Q2_CONST(1.32038354), Q2_CONST(1.21074086), Q2_CONST(1.10577988), Q2_CONST(1.00561746), Q2_CONST(0.90335924), Q2_CONST(0.80437489), Q2_CONST(0.70709671),
	Q2_CONST(0.60427395), Q2_CONST(0.49814048), Q2_CONST(0.38509539), Q2_CONST(0.27106800), Q2_CONST(0.14407416), Q2_CONST(0.00219910), -Q2_CONST(0.16725141), -Q2_CONST(0.36936085), },
	{ Q2_CONST(0.99895687), Q2_CONST(0.84188166), Q2_CONST(0.70753739), Q2_CONST(0.57906595), Q2_CONST(0.47055563), Q2_CONST(0.36966965), Q2_CONST(0.26826648), Q2_CONST(0.17163380),
	Q2_CONST(0.07208392), -Q2_CONST(0.03062936), -Q2_CONST(1.40037388), -Q2_CONST(0.25128968), -Q2_CONST(0.37213937), -Q2_CONST(0.51075646), -Q2_CONST(0.64887512), -Q2_CONST(0.80308031), },
	{ Q2_CONST(0.26515280), Q2_CONST(0.06313551), -Q2_CONST(0.08872080), -Q2_CONST(0.21103548), -Q2_CONST(0.31069678), -Q2_CONST(0.39680323), -Q2_CONST(0.47223474), -Q2_CONST(0.54167135),
	-Q2_CONST(0.61444740), -Q2_CONST(0.68943343), -Q2_CONST(0.76580211), -Q2_CONST(0.85170082), -Q2_CONST(0.95289061), -Q2_CONST(1.06514703), -Q2_CONST(1.20510707), -Q2_CONST(1.37617746), },
	{ -Q2_CONST(0.53940301), -Q2_CONST(0.73770929), -Q2_CONST(0.88424876), -Q2_CONST(1.01117930), -Q2_CONST(1.13389091), -Q2_CONST(1.26830073), -Q2_CONST(1.42041987), -Q2_CONST(1.62033919),
	-Q2_CONST(1.10158808), -Q2_CONST(1.16512566), -Q2_CONST(1.23337128), -Q2_CONST(1.30414401), -Q2_CONST(1.37663312), -Q2_CONST(1.46853845), -Q2_CONST(1.57625798), -Q2_CONST(1.66893638), },
	{ -Q2_CONST(0.38601997), -Q2_CONST(0.56009350), -Q2_CONST(0.66978483), -Q2_CONST(0.76028471), -Q2_CONST(0.83846064), -Q2_CONST(0.90868087), -Q2_CONST(0.97408881), -Q2_CONST(1.03694962), },
	{ -Q2_CONST(1.56144989), -Q2_CONST(1.65944032), -Q2_CONST(1.72689685), -Q2_CONST(1.77857740), -Q2_CONST(1.82203011), -Q2_CONST(1.86220079), -Q2_CONST(1.90283983), -Q2_CONST(1.94820479), },
};

/**
* decode exponents coded with LSP coefficients (same idea as Vorbis)
*/
void decode_exp_lsp(WMACodecContext *s, int ch)
{
	FIXP lsp_coefs[NB_LSP_COEFS];
	int val, i;

	for(i = 0; i < NB_LSP_COEFS; i++) {
		if (i == 0 || i >= 8)
			val = get_bits(&s->gb, 3);
		else
			val = get_bits(&s->gb, 4);
		lsp_coefs[i] = (fix_wma_lsp_codebook[i][val]);
	}

	wma_lsp_to_curve(s, s->fixexponents[ch], &s->fixmax_exponent[ch],
		s->block_len, lsp_coefs);
	//s->fixmax_exponent[ch] = QU18_CONST(s->max_exponent[ch]);
}

static const FIXPU fix_pow_tab[] = {
	QU18_CONST(1.7782794100389e-04), QU18_CONST(2.0535250264571e-04),
	QU18_CONST(2.3713737056617e-04), QU18_CONST(2.7384196342644e-04),
	QU18_CONST(3.1622776601684e-04), QU18_CONST(3.6517412725484e-04),
	QU18_CONST(4.2169650342858e-04), QU18_CONST(4.8696752516586e-04),
	QU18_CONST(5.6234132519035e-04), QU18_CONST(6.4938163157621e-04),
	QU18_CONST(7.4989420933246e-04), QU18_CONST(8.6596432336006e-04),
	QU18_CONST(1.0000000000000e-03), QU18_CONST(1.1547819846895e-03),
	QU18_CONST(1.3335214321633e-03), QU18_CONST(1.5399265260595e-03),
	QU18_CONST(1.7782794100389e-03), QU18_CONST(2.0535250264571e-03),
	QU18_CONST(2.3713737056617e-03), QU18_CONST(2.7384196342644e-03),
	QU18_CONST(3.1622776601684e-03), QU18_CONST(3.6517412725484e-03),
	QU18_CONST(4.2169650342858e-03), QU18_CONST(4.8696752516586e-03),
	QU18_CONST(5.6234132519035e-03), QU18_CONST(6.4938163157621e-03),
	QU18_CONST(7.4989420933246e-03), QU18_CONST(8.6596432336006e-03),
	QU18_CONST(1.0000000000000e-02), QU18_CONST(1.1547819846895e-02),
	QU18_CONST(1.3335214321633e-02), QU18_CONST(1.5399265260595e-02),
	QU18_CONST(1.7782794100389e-02), QU18_CONST(2.0535250264571e-02),
	QU18_CONST(2.3713737056617e-02), QU18_CONST(2.7384196342644e-02),
	QU18_CONST(3.1622776601684e-02), QU18_CONST(3.6517412725484e-02),
	QU18_CONST(4.2169650342858e-02), QU18_CONST(4.8696752516586e-02),
	QU18_CONST(5.6234132519035e-02), QU18_CONST(6.4938163157621e-02),
	QU18_CONST(7.4989420933246e-02), QU18_CONST(8.6596432336007e-02),
	QU18_CONST(1.0000000000000e-01), QU18_CONST(1.1547819846895e-01),
	QU18_CONST(1.3335214321633e-01), QU18_CONST(1.5399265260595e-01),
	QU18_CONST(1.7782794100389e-01), QU18_CONST(2.0535250264571e-01),
	QU18_CONST(2.3713737056617e-01), QU18_CONST(2.7384196342644e-01),
	QU18_CONST(3.1622776601684e-01), QU18_CONST(3.6517412725484e-01),
	QU18_CONST(4.2169650342858e-01), QU18_CONST(4.8696752516586e-01),
	QU18_CONST(5.6234132519035e-01), QU18_CONST(6.4938163157621e-01),
	QU18_CONST(7.4989420933246e-01), QU18_CONST(8.6596432336007e-01),
	QU18_CONST(1.0000000000000e+00), QU18_CONST(1.1547819846895e+00),
	QU18_CONST(1.3335214321633e+00), QU18_CONST(1.5399265260595e+00),
	QU18_CONST(1.7782794100389e+00), QU18_CONST(2.0535250264571e+00),
	QU18_CONST(2.3713737056617e+00), QU18_CONST(2.7384196342644e+00),
	QU18_CONST(3.1622776601684e+00), QU18_CONST(3.6517412725484e+00),
	QU18_CONST(4.2169650342858e+00), QU18_CONST(4.8696752516586e+00),
	QU18_CONST(5.6234132519035e+00), QU18_CONST(6.4938163157621e+00),
	QU18_CONST(7.4989420933246e+00), QU18_CONST(8.6596432336007e+00),
	QU18_CONST(1.0000000000000e+01), QU18_CONST(1.1547819846895e+01),
	QU18_CONST(1.3335214321633e+01), QU18_CONST(1.5399265260595e+01),
	QU18_CONST(1.7782794100389e+01), QU18_CONST(2.0535250264571e+01),
	QU18_CONST(2.3713737056617e+01), QU18_CONST(2.7384196342644e+01),
	QU18_CONST(3.1622776601684e+01), QU18_CONST(3.6517412725484e+01),
	QU18_CONST(4.2169650342858e+01), QU18_CONST(4.8696752516586e+01),
	QU18_CONST(5.6234132519035e+01), QU18_CONST(6.4938163157621e+01),
	QU18_CONST(7.4989420933246e+01), QU18_CONST(8.6596432336007e+01),
	QU18_CONST(1.0000000000000e+02), QU18_CONST(1.1547819846895e+02),
	QU18_CONST(1.3335214321633e+02), QU18_CONST(1.5399265260595e+02),
	QU18_CONST(1.7782794100389e+02), QU18_CONST(2.0535250264571e+02),
	QU18_CONST(2.3713737056617e+02), QU18_CONST(2.7384196342644e+02),
	QU18_CONST(3.1622776601684e+02), QU18_CONST(3.6517412725484e+02),
	QU18_CONST(4.2169650342858e+02), QU18_CONST(4.8696752516586e+02),
	QU18_CONST(5.6234132519035e+02), QU18_CONST(6.4938163157621e+02),
	QU18_CONST(7.4989420933246e+02), QU18_CONST(8.6596432336007e+02),
	QU18_CONST(1.0000000000000e+03), QU18_CONST(1.1547819846895e+03),
	QU18_CONST(1.3335214321633e+03), QU18_CONST(1.5399265260595e+03),
	QU18_CONST(1.7782794100389e+03), QU18_CONST(2.0535250264571e+03),
	QU18_CONST(2.3713737056617e+03), QU18_CONST(2.7384196342644e+03),
	QU18_CONST(3.1622776601684e+03), QU18_CONST(3.6517412725484e+03),
	QU18_CONST(4.2169650342858e+03), QU18_CONST(4.8696752516586e+03),
	QU18_CONST(5.6234132519035e+03), QU18_CONST(6.4938163157621e+03),
	QU18_CONST(7.4989420933246e+03), QU18_CONST(8.6596432336007e+03),
	QU18_CONST(1.0000000000000e+04), QU18_CONST(1.1547819846895e+04),
	QU18_CONST(1.3335214321633e+04), QU18_CONST(1.5399265260595e+04),
	QU18_CONST(1.7782794100389e+04), QU18_CONST(2.0535250264571e+04),
	QU18_CONST(2.3713737056617e+04), QU18_CONST(2.7384196342644e+04),
	QU18_CONST(3.1622776601684e+04), QU18_CONST(3.6517412725484e+04),
	QU18_CONST(4.2169650342858e+04), QU18_CONST(4.8696752516586e+04),
	QU18_CONST(5.6234132519035e+04), QU18_CONST(6.4938163157621e+04),
	QU18_CONST(7.4989420933246e+04), QU18_CONST(8.6596432336007e+04),
	QU18_CONST(1.0000000000000e+05), QU18_CONST(1.1547819846895e+05),
	QU18_CONST(1.3335214321633e+05), QU18_CONST(1.5399265260595e+05),
	QU18_CONST(1.7782794100389e+05), QU18_CONST(2.0535250264571e+05),
	QU18_CONST(2.3713737056617e+05),/* QU18_CONST(2.7384196342644e+05),
									QU18_CONST(3.1622776601684e+05), QU18_CONST(3.6517412725484e+05),
									QU18_CONST(4.2169650342858e+05), QU18_CONST(4.8696752516586e+05),
									QU18_CONST(5.6234132519035e+05), QU18_CONST(6.4938163157621e+05),
									QU18_CONST(7.4989420933246e+05), QU18_CONST(8.6596432336007e+05),*/
};

/**
* decode exponents coded with VLC codes
*/
int decode_exp_vlc(WMACodecContext *s, int ch)
{
	int last_exp, n, code;
	const uint16_t *ptr;

	FIXPU fixv, fixmax_scale;
	FIXPU *fixq, *fixq_end;
	const FIXPU *fixptab = fix_pow_tab + 60;

	ptr = s->exponent_bands[s->frame_len_bits - s->block_len_bits];
	fixq = s->fixexponents[ch];
	fixq_end = fixq + s->block_len;
	fixmax_scale = 0;
	if (s->version == 1) {
		//av_log(s->avctx, AV_LOG_INFO, "(s->version == 1) \n");
		last_exp = get_bits(&s->gb, 5) + 10;
		fixv = fixptab[last_exp];
		fixmax_scale = fixv;
		n = *ptr++;
		switch (n & 3) do {
		case 0: *fixq++ = fixv;
		case 3: *fixq++ = fixv;
		case 2: *fixq++ = fixv;
		case 1: *fixq++ = fixv;
		} while ((n -= 4) > 0);
	}else
		last_exp = 36;

	while (fixq < fixq_end) {
		code = get_vlc2(&s->gb, s->exp_vlc.table, EXPVLCBITS, EXPMAX);
		if (code < 0){
			return -1;
		}
		/* NOTE: this offset is the same as MPEG4 AAC ! */
		last_exp += code - 60;
		if ((unsigned)last_exp + 60 > FF_ARRAY_ELEMS(fix_pow_tab)) {
			return -1;
		}
		fixv = fixptab[last_exp];
		if (fixv > fixmax_scale)
			fixmax_scale = fixv;
		n = *ptr++;
		switch (n & 3) do {
		case 0: *fixq++ = fixv;
		case 3: *fixq++ = fixv;
		case 2: *fixq++ = fixv;
		case 1: *fixq++ = fixv;
		} while ((n -= 4) > 0);
	}
	s->fixmax_exponent[ch] = fixmax_scale;
	return 0;
}

static void vector_fmul_reverse_wma(FIXP *dst, const FIXP *src0, const FIXP *src1, int len){
	int i;
	src1 += len-1;
	for(i=0; i<len; i+=4)
	{
		dst[i+0] = (MULT31((src0[i+0]), (src1[-i-0])));
		dst[i+1] = (MULT31((src0[i+1]), (src1[-i-1])));
		dst[i+2] = (MULT31((src0[i+2]), (src1[-i-2])));
		dst[i+3] = (MULT31((src0[i+3]), (src1[-i-3])));
	}
}

static void vector_fmul_add_wma(FIXP *dst, const FIXP *src0, const FIXP *src1, const FIXP *src2, int len){
	int i;
	for(i=0; i<len; i+=4)
	{
		dst[i+0] = (MULT31((src0[i+0]), (src1[i+0])) + (src2[i+0]));
		dst[i+1] = (MULT31((src0[i+1]), (src1[i+1])) + (src2[i+1]));
		dst[i+2] = (MULT31((src0[i+2]), (src1[i+2])) + (src2[i+2]));
		dst[i+3] = (MULT31((src0[i+3]), (src1[i+3])) + (src2[i+3]));
	}
}


static void wma_window(WMACodecContext *s, FIXP *out)
{
	FIXP *in = s->output;
	int block_len, bsize, n;

	/* left part */
	if (s->block_len_bits <= s->prev_block_len_bits) {
		block_len = s->block_len;
		bsize = s->frame_len_bits - s->block_len_bits;

		vector_fmul_add_wma(out, in, s->windows[bsize], out, block_len);

	} else {
		block_len = 1 << s->prev_block_len_bits;
		n = (s->block_len - block_len) / 2;
		bsize = s->frame_len_bits - s->prev_block_len_bits;

		vector_fmul_add_wma(out+n, in+n, s->windows[bsize], out+n, block_len);

		memcpy(out+n+block_len, in+n+block_len, n*sizeof(FIXP));
	}

	out += s->block_len;
	in += s->block_len;

	/* right part */
	if (s->block_len_bits <= s->next_block_len_bits) {
		block_len = s->block_len;
		bsize = s->frame_len_bits - s->block_len_bits;

		vector_fmul_reverse_wma(out, in, s->windows[bsize], block_len);

	} else {
		block_len = 1 << s->next_block_len_bits;
		n = (s->block_len - block_len) / 2;
		bsize = s->frame_len_bits - s->next_block_len_bits;

		memcpy(out, in, n*sizeof(FIXP));
		vector_fmul_reverse_wma(out+n, in+n, s->windows[bsize], block_len);
		memset(out+n+block_len, 0, n*sizeof(FIXP));
	}
}



#undef _P
#define _P QU1_CONST
FIXPU pow10_1_20frac[] = {
	_P(1.00000000000),_P(1.12201845430),_P(1.25892541179),_P(1.41253754462),_P(1.58489319246),_P(1.77827941004),_P(1.99526231497),_P(1.11936056928),
	_P(1.25594321575),_P(1.40919146563),_P(1.58113883008),_P(1.77406694617),_P(1.99053585277),_P(1.11670898038),_P(1.25296808407),_P(1.40585331298),
	_P(1.57739336120),_P(1.76986446096),_P(1.98582058681),_P(1.11406367267),
};
int pow10_1_20exp[] = {0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3,};



int wma_decode_block(WMACodecContext *s)
{
	int n, v, a, ch, bsize;
	int coef_nb_bits, total_gain;
	int nb_coefs[MAX_CHANNELS];
	int mdct_norm_exp2;


	/* compute current block length */
	if (s->use_variable_block_len) {
		n = av_log2(s->nb_block_sizes - 1) + 1;

		if (s->reset_block_lengths) {
			s->reset_block_lengths = 0;
			v = get_bits(&s->gb, n);
			if (v >= s->nb_block_sizes){
				return WMA_DEC_ERR_PREV_BLOCK_LEN_BITS_OUTRANGE;
			}
			s->prev_block_len_bits = s->frame_len_bits - v;
			v = get_bits(&s->gb, n);
			if (v >= s->nb_block_sizes){
				return WMA_DEC_ERR_BLOCK_LEN_BITS_OUTRANGE;
			}
			s->block_len_bits = s->frame_len_bits - v;
		} else {
			/* update block lengths */
			s->prev_block_len_bits = s->block_len_bits;
			s->block_len_bits = s->next_block_len_bits;
		}
		v = get_bits(&s->gb, n);
		if (v >= s->nb_block_sizes){
			return WMA_DEC_ERR_NEXT_BLOCK_LEN_BITS_OUTRANGE;
		}
		s->next_block_len_bits = s->frame_len_bits - v;
	} else {
		/* fixed block len */
		s->next_block_len_bits = s->frame_len_bits;
		s->prev_block_len_bits = s->frame_len_bits;
		s->block_len_bits = s->frame_len_bits;
	}

	/* now check if the block length is coherent with the frame length */
	s->block_len = 1 << s->block_len_bits;
	if ((s->block_pos + s->block_len) > s->frame_len){
		return WMA_DEC_ERR_FRAMELEN_OVERFLOW;
	}

	if (s->nb_channels == 2) {
		s->ms_stereo = get_bits1(&s->gb);
	}
	v = 0;
	for(ch = 0; ch < s->nb_channels; ch++) {
		a = get_bits1(&s->gb);
		s->channel_coded[ch] = a;
		v |= a;
	}

	bsize = s->frame_len_bits - s->block_len_bits;

	/* if no channel coded, no need to go further */
	/* XXX: fix potential framing problems */
	if (!v)
		goto next;

	/* read total gain and extract corresponding number of bits for
	coef escape coding */
	total_gain = 1;
	for(;;) {
		a = get_bits(&s->gb, 7);
		total_gain += a;
		if (a != 127)
			break;
	}

	coef_nb_bits= ff_wma_total_gain_to_bits(total_gain);

	/* compute number of coefficients */
	n = s->coefs_end[bsize] - s->coefs_start;
	for(ch = 0; ch < s->nb_channels; ch++)
		nb_coefs[ch] = n;

	/* complex coding */
	if (s->use_noise_coding) {

		for(ch = 0; ch < s->nb_channels; ch++) {
			if (s->channel_coded[ch]) {
				int i, n, a;
				n = s->exponent_high_sizes[bsize];
				for(i=0;i<n;i++) {
					a = get_bits1(&s->gb);
					s->high_band_coded[ch][i] = a;
					/* if noise coding, the coefficients are not transmitted */
					if (a)
						nb_coefs[ch] -= s->exponent_high_bands[bsize][i];
				}
			}
		}
		for(ch = 0; ch < s->nb_channels; ch++) {
			if (s->channel_coded[ch]) {
				int i, n, val, code;

				n = s->exponent_high_sizes[bsize];
				val = (int)0x80000000;
				for(i=0;i<n;i++) {
					if (s->high_band_coded[ch][i]) {
						if (val == (int)0x80000000) {
							val = get_bits(&s->gb, 7) - 19;
						} else {
							code = get_vlc2(&s->gb, s->hgain_vlc.table, HGAINVLCBITS, HGAINMAX);
							if (code < 0){
								return WMA_DEC_ERR_HGAIN_VLC_INVALID;
							}
							val += code - 18;
						}
						s->high_band_values[ch][i] = val;
					}
				}
			}
		}
	}

	/* exponents can be reused in short blocks. */
	if ((s->block_len_bits == s->frame_len_bits) ||
		get_bits1(&s->gb)) {
			for(ch = 0; ch < s->nb_channels; ch++) {
				if (s->channel_coded[ch]) {
					if (s->use_exp_vlc) {
						if (decode_exp_vlc(s, ch) < 0)
							return WMA_DEC_ERR_DECODE_EXP_VLC_ERR;
					} else {
						decode_exp_lsp(s, ch);
					}
					s->exponents_bsize[ch] = bsize;
				}
			}
	}

	/* parse spectral coefficients : just RLE encoding */
	for(ch = 0; ch < s->nb_channels; ch++) {
		if (s->channel_coded[ch]) {
			int tindex;
			fixWMACoef* ptr = &s->coefs1[ch][0];

			/* special VLC tables are used for ms stereo because
			there is potentially less energy there */
			tindex = (ch == 1 && s->ms_stereo);
			memset(ptr, 0, s->block_len * sizeof(fixWMACoef));
			ff_wma_run_level_decode(s->avctx, &s->gb, &s->coef_vlc[tindex],
				s->level_table[tindex], s->run_table[tindex],
				0, ptr, 0, nb_coefs[ch],
				s->block_len, s->frame_len_bits, coef_nb_bits);
		}
		if (s->version == 1 && s->nb_channels >= 2) {
			align_get_bits(&s->gb);
		}
	}

	/* normalize */
	{
		mdct_norm_exp2 = -(s->block_len_bits-1)<<1;
		if (s->version == 1) {
			mdct_norm_exp2 += (s->block_len_bits-1);
		}
	}

	/* finally compute the MDCT coefficients */
	for(ch = 0; ch < s->nb_channels; ch++) {
		if (s->channel_coded[ch]) {
			fixWMACoef *coefs1;
			FIXP *coefs;//, *exponents;//, mult, mult1, noise;
			int i, j, n, n1, last_high_band, esize;
			FIXPU fixmult, fixmult1, tmpmult;
			uint64_t fixexp_power[HIGH_BAND_MAX_SIZE];
			FIXP fixnoise;
			int iexp, rexp, rexp1;
			FIXPU pow10val, *fixexponents;

			coefs1 = s->coefs1[ch];
			fixexponents = s->fixexponents[ch];
			esize = s->exponents_bsize[ch];
			iexp = total_gain/20;
			rexp = total_gain%20;
			for(pow10val=1; iexp>0; iexp--)
				pow10val *= 10;
			iexp = FASTCLZ(s->fixmax_exponent[ch]);
			if(s->fixmax_exponent[ch]==0)
				 return -1;
			fixmult = FIXU_DIV31(pow10_1_20frac[rexp], s->fixmax_exponent[ch]<<iexp);
			rexp = pow10_1_20exp[rexp]-(32-14)+iexp;

			iexp = FASTCLZ(fixmult);
			fixmult <<= iexp; 
			fixmult>>=1;
			rexp -= iexp-1;

			rexp += (mdct_norm_exp2>>1);
			if(mdct_norm_exp2&1)//version1
				fixmult = MULT31U(fixmult, QU1_CONST(1.4142135623730950488016887242097));
			if(pow10val > 1)
			{
				iexp = FASTCLZ(pow10val);
				rexp += (32-iexp);
				pow10val<<=iexp;
				fixmult = FIXU_MUL32(fixmult, pow10val);
			}
			iexp = FASTCLZ(fixmult);
			fixmult <<= iexp; 
			fixmult>>=1;
			rexp -= iexp-1;
			coefs = s->coefs[ch];
			if (s->use_noise_coding) {
				U64 tmp64_1, tmp64_2;
				int exp1, exp2;
				fixmult1 = fixmult;
				/* very low freqs : noise */
				for(i = 0;i < s->coefs_start; i++) {
					pow10val = (fixexponents[i<<bsize>>esize]);
					iexp = FASTCLZ(pow10val);
					fixnoise = FIXU_MUL32(fixmult1, pow10val<<iexp);
					iexp = rexp - iexp + (32-14);
					*coefs++ = (FIX_MUL((s->noise_table[s->noise_index]), fixnoise, 31+30-10+2-iexp));
					s->noise_index = (s->noise_index + 1) & (NOISE_TAB_SIZE - 1);
				}

				n1 = s->exponent_high_sizes[bsize];

				/* compute power of high bands */
				fixexponents = s->fixexponents[ch] +
					(s->high_band_start[bsize]<<bsize>>esize);
				last_high_band = 0; /* avoid warning */
				for(j=0;j<n1;j++) {
					n = s->exponent_high_bands[s->frame_len_bits -
						s->block_len_bits][j];
					if (s->high_band_coded[ch][j]) {
						FIXPU fixv;
						uint64_t fixe2;
						fixe2 = 0;
						for(i = 0;i < n; i++) {
							fixv = (fixexponents[i<<bsize>>esize]);
							fixe2 = UMADD64(fixe2, fixv, fixv);
						}
						fixexp_power[j] = fixe2 / n;
						last_high_band = j;
					}
					fixexponents += n<<bsize>>esize;
				}
				tmp64_2.w64 = fixexp_power[last_high_band];
				exp2 = fast_log2(tmp64_2.r.hi32)+1;
				if(exp2)
					exp2 += 32;
				else
					exp2 = fast_log2(tmp64_2.r.lo32)+1;
				exp2-=31;
				if(exp2 > 0)
				{
					tmp64_2.w64 >>= exp2;
				}
				else
				{
					tmp64_2.w64 <<= (-exp2);
				}
				exp2 += 31;
				exp2 -= 28;
				tmp64_2.r.lo32 = InvRNormalized(tmp64_2.r.lo32);

				/* main freqs and high freqs */
				fixexponents = s->fixexponents[ch] + (s->coefs_start<<bsize>>esize);
				for(j=-1;j<n1;j++) {
					if (j < 0) {
						n = s->high_band_start[bsize] -
							s->coefs_start;
					} else {
						n = s->exponent_high_bands[s->frame_len_bits -
							s->block_len_bits][j];
					}
					if (j >= 0 && s->high_band_coded[ch][j]) {
						/* use noise with specified power */
						tmp64_1.w64 = fixexp_power[j];
						exp1 = fast_log2(tmp64_1.r.hi32)+1;
						if(exp1)
							exp1 += 32;
						else
							exp1 = fast_log2(tmp64_1.r.lo32)+1;
						exp1-=29;
						if(exp1 > 0)
						{
							tmp64_1.w64 >>= exp1;
						}
						else
						{
							tmp64_1.w64 <<= (-exp1);
						}
						exp1 += 29;
						exp1 -= 28;
						exp1 -= exp2;
						if(exp1&1)
						{
							exp1++;
							tmp64_1.r.lo32 >>= 1;
						}
						exp1 >>= 1;
						tmp64_1.w64 = FIX64U_MUL(tmp64_1.r.lo32, tmp64_2.r.lo32);
						fixmult1 = isqrt64(tmp64_1.w64);

						/* XXX: use a table */
						iexp = s->high_band_values[ch][j]/20 + 2;
						rexp1 = s->high_band_values[ch][j]%20;
						if(rexp1 < 0)
						{
							iexp--;
							rexp1+=20;
						}
						for(pow10val=1; iexp>0; iexp--)
							pow10val *= 10;
						iexp = FASTCLZ(s->fixmax_exponent[ch]);
						tmpmult = FIXU_DIV31(pow10_1_20frac[rexp1], s->fixmax_exponent[ch]<<iexp);
						rexp1 = exp1 + pow10_1_20exp[rexp1]-(32-14)+iexp-s->noise_shift;
						rexp1 += (mdct_norm_exp2>>1);
						if(mdct_norm_exp2&1)
							tmpmult = MULT31U(tmpmult, QU1_CONST(1.4142135623730950488016887242097));
						if(pow10val > 1)
						{
							iexp = FASTCLZ(pow10val);
							rexp1 += (32-iexp);
							pow10val<<=iexp;
							tmpmult = FIXU_MUL32(tmpmult, pow10val);
						}
						fixmult1 = FIXU_MUL32(fixmult1, tmpmult);
						iexp = FASTCLZ(fixmult1)-1;
						fixmult1 <<= iexp;
						rexp1 -= iexp;

						for(i = 0;i < n; i++) {
							fixnoise = (s->noise_table[s->noise_index]);
							pow10val = (fixexponents[i<<bsize>>esize]);
							iexp = FASTCLZ(pow10val);
							tmpmult = FIXU_MUL32(fixmult1, pow10val<<iexp);
							iexp = rexp1 - iexp + (32-14)-2;

							*coefs++ = (FIX_MUL(fixnoise>>5, tmpmult, 31+28-10-iexp-5));

							s->noise_index = (s->noise_index + 1) & (NOISE_TAB_SIZE - 1);
						}
						fixexponents += n<<bsize>>esize;
					} 
					else {
						/* coded values + small noise */
						for(i = 0;i < n; i++) {
							fixnoise = ((s->noise_table[s->noise_index]));
							pow10val = (fixexponents[i<<bsize>>esize]);
							iexp = FASTCLZ(pow10val);
							fixmult1 = FIXU_MUL32(fixmult, pow10val<<iexp);
							iexp = rexp - iexp + (32-14);
							if(iexp <= 31+30-10+2-63)
								fixnoise = 0;
							else
								fixnoise = FIX_MUL(fixnoise, fixmult1, 31+30-10-iexp+2);
							*coefs++ = (FIX_MUL((*coefs1++), fixmult1, 0+30-10-iexp) + fixnoise);
							s->noise_index = (s->noise_index + 1) & (NOISE_TAB_SIZE - 1);
						}
						fixexponents += n<<bsize>>esize;
					}
				}

				/* very high freqs : noise */
				n = s->block_len - s->coefs_end[bsize];
				pow10val = (fixexponents[((-1<<bsize))>>esize]);
				iexp = FASTCLZ(pow10val);
				fixmult1 = FIXU_MUL32(fixmult, pow10val<<iexp);
				iexp = rexp - iexp + (32-14)-2;
				for(i = 0; i < n; i++) {
					if(iexp <= -12)
						*coefs++ = 0;
					else
						*coefs++ = (FIX_MUL((s->noise_table[s->noise_index]), fixmult1, 31+30-10-iexp));
					s->noise_index = (s->noise_index + 1) & (NOISE_TAB_SIZE - 1);
				}
			} 
			else {
				/* XXX: optimize more */
				for(i = 0;i < s->coefs_start; i++)
					*coefs++ = 0;
				n = nb_coefs[ch];
				for(i = 0;i < n; i++) {
					pow10val = (fixexponents[i<<bsize>>esize]);
					iexp = FASTCLZ(pow10val);
					fixnoise = FIXU_MUL32(fixmult, pow10val<<iexp);
					iexp = rexp - iexp + (32-14);
					*coefs++ = (FIX_MUL((coefs1[i]), fixnoise, 30-10-iexp));
				}
				n = s->block_len - s->coefs_end[bsize];
				for(i = 0;i < n; i++)
					*coefs++ = 0;
			}
		}
	}


	if (s->ms_stereo && s->channel_coded[1]) {
		/* nominal case for ms stereo: we do it before mdct */
		/* no need to optimize this case because it should almost
		never happen */
		int i;
		if (!s->channel_coded[0]) {
			memset(s->coefs[0], 0, sizeof(FIXP) * s->block_len);
			s->channel_coded[0] = 1;
		}

		for(i = 0; i < s->block_len; ++i)
		{
			FIXP fixa = (s->coefs[0][i]);
			FIXP fixb = (s->coefs[1][i]);
			s->coefs[0][i] = (fixa + fixb);
			s->coefs[1][i] = (fixa - fixb);
		}
	}

next:
	for(ch = 0; ch < s->nb_channels; ch++) {
		int n4, index;

		n4 = s->block_len / 2;
		if(s->channel_coded[ch]){
			fix_imdct_calc((s->frame_len_bits - bsize + 1), s->output, s->coefs[ch]);
		}else if(!(s->ms_stereo && ch==1))
			memset(s->output, 0, sizeof(s->output));

		/* multiply by the window and add in the frame */
		index = (s->frame_len / 2) + s->block_pos - n4;
		wma_window(s, &s->frame_out[ch][index]);	
	}

	/* update block number */
	s->block_num++;
	s->block_pos += s->block_len;
	if (s->block_pos >= s->frame_len)
		return 1;
	else
		return 0;
}


/* decode a frame of frame_len samples */
static int wma_decode_frame(WMACodecContext *s, int16_t *samples)
{
	int ret, i, n, ch, incr;
	int16_t *ptr;
	FIXP *iptr;

	/* read each block */
	s->block_num = 0;
	s->block_pos = 0;
	for(;;) {
		ret = wma_decode_block(s);//只有ret=1时才是正常的，返回值ret=0时表示一帧还没有解完,
		if (ret < 0)              //还需要继续
			return ret;
		if (ret)
			break;
	}

	/* convert frame to integer */
	n = s->frame_len;
	incr = s->nb_channels;
	for(ch = 0; ch < s->nb_channels; ch++)
	{
		ptr = samples + ch;
		iptr = s->frame_out[ch];

		for(i=0;i<n;i++){
			int t=(*iptr++);
			*ptr = cliptoshort(t, 10);
			 ptr += incr;
			if(s->nb_channels==1)
			{
			   *ptr = cliptoshort(t, 10);
			    ptr += incr;
			}
		}
		/* prepare for next block */
		memmove(&s->frame_out[ch][0], &s->frame_out[ch][s->frame_len],
			s->frame_len * sizeof(FIXP));
	}
	return WMA_DEC_ERR_NoErr;
}


int wma_decode_superframe(WmaAudioContext *avctx,
						  void *data, int *data_size,//output
						  uint8_t *buf, int buf_size)//input
{
	WMACodecContext *s = avctx->priv_data;
	int nb_frames, bit_offset, pos, len,i;
	uint8_t *q;
	int16_t *samples;
	int errcode=WMA_DEC_ERR_UNKNOW;
	int nsamps=0;

	if(buf_size==0){
		s->last_superframe_len = 0;
		return WMA_DEC_ERR_NoErr;
	}
	if (buf_size < s->block_align)
		return WMA_DEC_ERR_NoErr;
	buf_size = s->block_align;
	init_get_bits(&s->gb, buf, buf_size*8);
	samples =(int16_t*) data;


	if (s->use_bit_reservoir)
	{
		/* read super frame header */
		skip_bits(&s->gb, 4); /* super frame index */
		nb_frames = get_bits(&s->gb, 4) - 1;
		if((nb_frames+1) * s->nb_channels * s->frame_len * sizeof(int16_t) > *data_size)
			goto fail;
		bit_offset = get_bits(&s->gb, s->byte_offset_bits + 3);


		if (s->last_superframe_len > 0){

			/* add bit_offset bits to last frame */
			if ((s->last_superframe_len + ((bit_offset + 7) >> 3)) > MAX_CODED_SUPERFRAME_SIZE){
				errcode = WMA_DEC_ERR_LASTFRAME_LEN;
				goto fail;
			}
			q = s->last_superframe + s->last_superframe_len;
			len = bit_offset;
			while (len > 7) {
				*q++ = (get_bits)(&s->gb, 8);
				len -= 8;
			}
			if (len > 0) {
				*q++ = (get_bits)(&s->gb, len) << (8 - len);
			}

			/* XXX: bit_offset bits into last frame */
			init_get_bits(&s->gb, s->last_superframe, MAX_CODED_SUPERFRAME_SIZE*8);
			/* skip unused bits */
			if (s->last_bitoffset > 0)
				skip_bits(&s->gb, s->last_bitoffset);

			/* this frame is stored in the last superframe and in the current one */
			if ((errcode=wma_decode_frame(s, samples)) < 0)
				goto fail;	
			samples += (s->nb_channels==1?2:s->nb_channels) * s->frame_len;
			
		}


		/* read each frame starting from bit_offset */
		pos = bit_offset + 4 + 4 + s->byte_offset_bits + 3;
		init_get_bits(&s->gb, buf + (pos >> 3), (MAX_CODED_SUPERFRAME_SIZE - (pos >> 3))*8);
		len = pos & 7;
		if (len > 0)
			skip_bits(&s->gb, len);
		s->reset_block_lengths = 1;


		for(i=0;i<nb_frames;i++) {
			if (wma_decode_frame(s, samples) < 0)
				goto fail;     
			samples += (s->nb_channels==1?2:s->nb_channels) * s->frame_len;
		}

		/* we copy the end of the frame in the last frame buffer */
		pos = get_bits_count(&s->gb) + ((bit_offset + 4 + 4 + s->byte_offset_bits + 3) & ~7);
		s->last_bitoffset = pos & 7;
		pos >>= 3;
		len = buf_size - pos;

		if (len > MAX_CODED_SUPERFRAME_SIZE || len < 0) {
			errcode=WMA_DEC_ERR_EXCEED_MAX_CODED_SUPERFRAME_SIZE;
			goto fail;
		}
		s->last_superframe_len = len;
		memcpy(s->last_superframe, buf + pos, len);
	} else {

		if((s->nb_channels * s->frame_len * sizeof(int16_t)) > *data_size){
			errcode= WMA_DEC_ERR_LACK_OUTPUTMEM;
			goto fail;
		}
		if ((errcode=wma_decode_frame(s, samples)) < 0)
			goto fail;
		samples += (s->nb_channels==1?2:s->nb_channels) * s->frame_len;
	}

	*data_size = (int8_t *)samples - (int8_t *)data;
	return s->block_align;

fail:
	/* when error, we reset the bit reservoir */
	s->last_superframe_len = 0;
	return errcode;
}

void wma_decode_flush(WmaAudioContext *avctx)
{
	WMACodecContext *s = avctx->priv_data;

	s->last_bitoffset=
		s->last_superframe_len= 0;
}

