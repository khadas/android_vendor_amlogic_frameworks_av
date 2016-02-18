
#ifndef _WMA_FIXED_DEBUG_H_
#define _WMA_FIXED_DEBUG_H_


#include "common.h"
typedef int32_t FIXP;           /* Fixed point variable type */
typedef uint32_t FIXPU;         /* Fixed point fraction 0<=x<1 */

typedef union _U64 {
	uint64_t w64;
	struct {
		/* x86 = little endian */
		uint32_t lo32;
		uint32_t hi32;
	} r;
} U64;

typedef union _I64 {
	struct {
		uint32_t lo32;
		int32_t hi32;
	} r;
	int64_t w64;
} I64;

static INLINE int32_t MULT32(int32_t x, int32_t y) {
#ifdef __mips_dspr2
	return __builtin_mips_mult(x, y) >> 32;
#else
	return ((int64_t)(x) * (y)) >> 32;
#endif
}

static INLINE uint32_t MULT32U(uint32_t x, uint32_t y) {
#ifdef __mips_dspr2
	return __builtin_mips_multu(x, y)>>32;
#else
	return ((uint64_t)(x) * (y)) >> 32;
#endif
}

static INLINE int32_t MULT31(int32_t x, int32_t y) {
	return MULT32(x,y)<<1;
}

static INLINE int32_t MULT31U(uint32_t x, uint32_t y) {
	return MULT32U(x,y)<<1;
}

static INLINE int32_t MULT30(int32_t x, int32_t y) {
	return MULT32(x,y)<<2;
}

static INLINE int32_t MULT30U(uint32_t x, uint32_t y) {
	return MULT32U(x,y)<<2;
}

#ifdef __mips_dspr2
static INLINE  int32_t FIX_MUL(int a, int b, int c)
{
    return (__builtin_mips_mult(a, b)>>c);
}
#else
#define FIX_MUL(A,B,C) ((int32_t)(((int64_t)(A)*(int64_t)(B)) >> (C)))
#endif

#define FIX_MUL32 MULT32
#define FIX_MUL31(A,B) FIX_MUL(A,B,31)
#define FIX_MUL30(A,B) FIX_MUL(A,B,30)
#define FIX_MUL29(A,B) FIX_MUL(A,B,29)
#define FIX_MUL28(A,B) FIX_MUL(A,B,28)
#define FIX_MUL27(A,B) FIX_MUL(A,B,27)
#define FIX_MUL26(A,B) FIX_MUL(A,B,26)
#define FIX_MUL25(A,B) FIX_MUL(A,B,25)
#define FIX_MUL23(A,B) FIX_MUL(A,B,23)
#define FIX_MUL18(A,B) FIX_MUL(A,B,18)
#define FIX_MUL16(A,B) FIX_MUL(A,B,16)
#define FIX_MUL14(A,B) FIX_MUL(A,B,14)
#define FIX_MUL12(A,B) FIX_MUL(A,B,12)

#define FIXU_MUL(A,B,C) ((uint32_t)(((uint64_t)(A)*(uint64_t)(B)) >> (C)))
#define FIXU_MUL32 MULT32U
#define FIXU_MUL31(A,B) FIXU_MUL(A,B,31)
#define FIXU_MUL30(A,B) FIXU_MUL(A,B,30)


#ifdef __mips_dspr2
static INLINE uint64_t FIX64U_MUL(uint32_t x, uint32_t y) {
    return __builtin_mips_multu(x, y);
}
static INLINE int64_t FIX64_MUL(int32_t x, int32_t y) {
    return __builtin_mips_mult(x, y);
}
#else
static INLINE  uint64_t FIX64U_MUL(uint32_t x, uint32_t y) {
    return ((uint64_t)(x)*(uint64_t)(y));
}
static INLINE  int64_t FIX64_MUL(int32_t x, int32_t y) {
    return ((int64_t)(x)*(int64_t)(y));
}
#endif

#define FIX_DIV(A, B, C) (((int64_t)(A) << (C))/(B))
#define FIX_DIV32(A, B) FIX_DIV(A, B, 32) 
#define FIX_DIV31(A, B) FIX_DIV(A, B, 31) 
#define FIX_DIV30(A, B) FIX_DIV(A, B, 30) 
#define FIX_DIV29(A, B) FIX_DIV(A, B, 29) 
#define FIX_DIV28(A, B) FIX_DIV(A, B, 28) 
#define FIX_DIV23(A, B) FIX_DIV(A, B, 23) 
#define FIX_DIV16(A, B) FIX_DIV(A, B, 16) 
#define FIX_DIV14(A, B) FIX_DIV(A, B, 14) 

#define FIXU_DIV(A, B, C) (((uint64_t)(A) << (C))/(uint32_t)(B))
#define FIXU_DIV32(A, B) FIXU_DIV(A, B, 32) 
#define FIXU_DIV31(A, B) FIXU_DIV(A, B, 31) 
#define FIXU_DIV30(A, B) FIXU_DIV(A, B, 30) 

//Q0,小数位31位
#if 0
#define Q0_CONST(A) ((A==1.0) ? ((int32_t)0x7fffffff) : ((int32_t)((A)*(1LL<<31)))) 
#else
#define Q0_CONST(A) ((A==1.0) ? ((int32_t)0x7fffffff) : ((int32_t)((A)*((int64_t)1<<31))))
#endif

//#define Q0_FLOAT(A) (((float)(A))/((float)((1LL<<31))))
//Q1,小数位30位
#define Q1_CONST(A) ((int32_t)((A)*(1<<30)))
//#define Q1_FLOAT(A) (((float)(A))/((float)(1<<30)))
//Q2,小数位29位
#define Q2_CONST(A) ((int32_t)((A)*(1<<29)))
//#define Q2_FLOAT(A) (((float)(A))/((float)(1<<29)))
//Q3,小数位28位
#define Q3_CONST(A) ((int32_t)((A)*(1<<28)))
//#define Q3_FLOAT(A) (((float)(A))/((float)(1<<28)))
//Q4,小数位27位
#define Q4_CONST(A) ((int32_t)((A)*(1<<27)))
//#define Q4_FLOAT(A) (((float)(A))/((float)(1<<27)))
//Q5,小数位26位
#define Q5_CONST(A) ((int32_t)((A)*(1<<26)))
//#define Q5_FLOAT(A) (((float)(A))/((float)(1<<26)))
//Q6,小数位25位
#define Q6_CONST(A) ((int32_t)((A)*(1<<25)))
//#define Q6_FLOAT(A) (((float)(A))/((float)(1<<25)))
//Q7,小数位24位
#define Q7_CONST(A) ((int32_t)((A)*(1<<24)))
//#define Q7_FLOAT(A) (((float)(A))/((float)(1<<24)))
//Q8,小数位23位
#define Q8_CONST(A) ((int32_t)((A)*(1<<23)))
//#define Q8_FLOAT(A) (((float)(A))/((float)(1<<23)))
//Q9,小数位22位
#define Q9_CONST(A) ((int32_t)((A)*(1<<22)))
//#define Q9_FLOAT(A) (((float)(A))/((float)(1<<22)))
//Q10指整数部分为10位,同时还有1位符号位和32-1-10=21位小数位
#define Q10_CONST(A) ((int32_t)((A)*(1<<21)))
//#define Q10_FLOAT(A) (((float)(A))/((float)(1<<21)))
//Q12指整数部分为12位,同时还有1位符号位和32-1-12=19位小数位
#define Q12_CONST(A) ((int32_t)((A)*(1<<19)))
//#define Q12_FLOAT(A) (((float)(A))/((float)(1<<19)))
//Q13指整数部分为13位,同时还有1位符号位和32-1-13=18位小数位
#define Q13_CONST(A) ((int32_t)((A)*(1<<18)))
//#define Q13_FLOAT(A) (((float)(A))/((float)(1<<18)))
//Q14指整数部分为14位,同时还有1位符号位和32-1-14=17位小数位
#define Q14_CONST(A) ((int32_t)((A)*(1<<17)))
//#define Q14_FLOAT(A) (((float)(A))/((float)(1<<17)))
//Q15指整数部分为15位,同时还有1位符号位和32-1-15=16位小数位
#define Q15_CONST(A) ((int32_t)((A)*(1<<16)))
//#define Q15_FLOAT(A) (((float)(A))/((float)(1<<16)))
//Q16指整数部分为16位,同时还有1位符号位和32-1-16=15位小数位
#define Q16_CONST(A) ((int32_t)((A)*(1<<15)))
//#define Q16_FLOAT(A) (((float)(A))/((float)(1<<15)))
//Q17指整数部分为17位,同时还有1位符号位和32-1-17=14位小数位
#define Q17_CONST(A) ((int32_t)((A)*(1<<14)))
//#define Q17_FLOAT(A) (((float)(A))/((float)(1<<14)))
//Q18,小数位13位
#define Q18_CONST(A) ((int32_t)((A)*(1<<13)))
//#define Q18_FLOAT(A) (((float)(A))/((float)(1<<13)))
//Q19,小数位12位
#define Q19_CONST(A) (((int32_t)((A)*(1<<12))))
//#define Q19_FLOAT(A) (((float)(A))/((float)(1<<12)))
//Q20,小数位11位
#define Q20_CONST(A) ((int32_t)((A)*(1<<11)))//(((A) >= 0) ? ((REAL_T)((A)*(1<<11)+0.5)) : ((REAL_T)((A)*(1<<11)-0.5)))
//#define Q20_FLOAT(A) (((float)(A))/((float)(1<<11)))
//Q21,小数位10位
#define Q21_CONST(A) ((int32_t)((A)*(1<<10)))//(((A) >= 0) ? ((REAL_T)((A)*(1<<10)+0.5)) : ((REAL_T)((A)*(1<<10)-0.5)))
//#define Q21_FLOAT(A) (((float)(A))/((float)(1<<10)))
//Q22,小数位9位
#define Q22_CONST(A) ((int32_t)((A)*(1<<9)))
//#define Q22_FLOAT(A) (((float)(A))/((float)(1<<9)))
//Q23,小数位8位
#define Q23_CONST(A) ((int32_t)((A)*(1<<8)))
//#define Q23_FLOAT(A) (((float)(A))/((float)(1<<8)))
//Q24,小数位7位
#define Q24_CONST(A) ((int32_t)((A)*(1<<7)))
//#define Q24_FLOAT(A) (((float)(A))/((float)(1<<7)))
//Q25,小数位6位
#define Q25_CONST(A) ((int32_t)((A)*(1<<6)))
//#define Q25_FLOAT(A) (((float)(A))/((float)(1<<6)))
//Q26,小数位5位
#define Q26_CONST(A) ((int32_t)((A)*(1<<5)))
//#define Q26_FLOAT(A) (((float)(A))/((float)(1<<5)))

//Q20,小数位20位,这里写得有点特别
#define Q20_CONST64(A) ((int64_t)((A)*(1<<20)))
//#define Q20_FLOAT64(A) (((float)(A))/((float)(1<<20)))

#define QU0_CONST(A) ((uint32_t)((A)*(1LL<<32)))
//#define QU0_FLOAT(A) (((float)(A))/((float)((1LL<<32))))
//QU1,小数位31位
#define QU1_CONST(A) ((uint32_t)((A)*((uint32_t)1<<31)))
//#define QU1_FLOAT(A) (((float)(A))/((float)((uint32_t)1<<31)))
//QU20,小数位12位
#define QU20_CONST(A) ((uint32_t)((A)*(1<<12)))
//#define QU20_FLOAT(A) (((float)(A))/((float)(1<<12)))
//QU19,小数位13位
#define QU19_CONST(A) ((uint32_t)((A)*(1<<13)))
//#define QU19_FLOAT(A) (((float)(A))/((float)(1<<13)))
//QU18,小数位14位
#define QU18_CONST(A) ((uint32_t)((A)*(1<<14)))
//#define QU18_FLOAT(A) (((float)(A))/((float)(1<<14)))
//QU15,小数位17位
#define QU15_CONST(A) ((uint32_t)((A)*(1<<17)))
//#define QU15_FLOAT(A) (((float)(A))/((float)(1<<17)))
//QU5,小数位27位
#define QU5_CONST(A) ((uint32_t)((A)*(1<<27)))
//#define QU5_FLOAT(A) (((float)(A))/((float)(1<<27)))
//QU4,小数位28位
#define QU4_CONST(A) ((uint32_t)((A)*(1<<28)))
//#define QU4_FLOAT(A) (((float)(A))/((float)(1<<28)))
//QU2,小数位30位
#define QU2_CONST(A) ((uint32_t)((A)*(1<<30)))
//#define QU2_FLOAT(A) (((float)(A))/((float)(1<<30)))



#ifdef __GNUC__
#define CLZ(x) __builtin_clz(x)
#define CTZ(x) __builtin_ctz(x)
#else
static uint32_t ALWAYS_INLINE popcnt( uint32_t x )
{
    x -= ((x >> 1) & 0x55555555);
    x = (((x >> 2) & 0x33333333) + (x & 0x33333333));
    x = (((x >> 4) + x) & 0x0f0f0f0f);
    x += (x >> 8);
    x += (x >> 16);
    return x & 0x0000003f;
}
static uint32_t ALWAYS_INLINE CLZ( uint32_t x )
{
    x |= (x >> 1);
    x |= (x >> 2);
    x |= (x >> 4);
    x |= (x >> 8);
    x |= (x >> 16);
    return 32 - popcnt(x);
}
static uint32_t ALWAYS_INLINE CTZ( uint32_t x )
{
    return popcnt((x & -x) - 1);
}

INLINE int CLZ_real(int x)
{//could use gcc builtin function instead
	int numZeros;
	
	if (!x)
		return 32;
	
	/* count leading zeros with binary search (function should be 17 ARM instructions total) */
	numZeros = 1;
	if (!((unsigned int)x >> 16))	{ numZeros += 16; x <<= 16; }
	if (!((unsigned int)x >> 24))	{ numZeros +=  8; x <<=  8; }
	if (!((unsigned int)x >> 28))	{ numZeros +=  4; x <<=  4; }
	if (!((unsigned int)x >> 30))	{ numZeros +=  2; x <<=  2; }
	
	numZeros -= ((unsigned int)x >> 31);
	
	return numZeros;
}

#endif

typedef int64_t Word64;

#if 0
INLINE int MULSHIFT32(int x, int y)
{
    int z;
	
    z = (Word64)x * (Word64)y >> 32;
    
	return z;
}
#else
#define MULSHIFT32 MULT32
#endif


static INLINE int64_t fix64mul32(int64_t a64, int b, int shift)
{
	I64 *a=(I64*)&a64;
	I64 m,n;
	
	m.w64 = FIX64_MUL(a->r.hi32, b);
	n.w64 = FIX64U_MUL(a->r.lo32, b);
	
	return ((int64_t)m.r.hi32<<(64-shift)) + (((int64_t)m.r.lo32+(int64_t)n.r.hi32)<<(32-shift)) + (/*(int64_t)*/n.r.lo32>>shift);
}



static INLINE Word64 MADD64(Word64 sum64, int x, int y)
{
#ifdef __mips_dspr2
	return __builtin_mips_madd(sum64, x, y);
#else
	sum64 += (int64_t)x * (int64_t)y;
	return sum64;
#endif
}

static INLINE Word64 MSUB64(Word64 sum64, int x, int y)
{
#ifdef __mips_dspr2
	return __builtin_mips_msub(sum64, x, y);
#else
	sum64 -= (int64_t)x * (int64_t)y;
	return sum64;
#endif
}


static INLINE uint32_t ones32(uint32_t x)
{
    x -= ((x >> 1) & 0x55555555);
    x = (((x >> 2) & 0x33333333) + (x & 0x33333333));
    x = (((x >> 4) + x) & 0x0f0f0f0f);
    x += (x >> 8);
    x += (x >> 16);
	
    return (x & 0x0000003f);
}

//--------------------------
//code added by myself:
#ifdef _WIN32
static INLINE int __builtin_clz(uint32_t x)
{
    int ret = 32;
	while(x)
	{
		x = x >> 1;
		ret -= 1;
	}
	return ret;
}
#endif
//------------------------------

static INLINE int FASTCLZ(uint32_t x)
{
#if defined(__mips__)
    return __builtin_clz((x));
#else
    return x ? __builtin_clz((x)) : 32;
#endif
}

static INLINE int fast_log2(uint32_t x)
{
#if 1 
    return(31 - FASTCLZ(x));
#elif 1
    x |= (x >> 1);
    x |= (x >> 2);
    x |= (x >> 4);
    x |= (x >> 8);
    x |= (x >> 16);
	
    //if((ones32(x) - 1) != (31 - __builtin_clz((x)|1))) printf("%x =%d, %d \n", x, (ones32(x) - 1), (31 - __builtin_clz((x)|1)));
    return (ones32(x) - 1);
#else
    uint32_t count = 0;
	
    while (x >>= 1)
        count++;
	
    return count;
#endif
}

static INLINE int FASTABS(int x) 
{
#ifdef __mips_dsp
    return  __builtin_mips_absq_s_w(x);
#else
	int sign;
	
	sign = x >> (sizeof(int) * 8 - 1);
	x ^= sign;
	x -= sign;
	
	return x;
#endif
}

#endif

