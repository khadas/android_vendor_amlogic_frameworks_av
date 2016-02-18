
#ifndef _WMA_FIXED_DEBUG_H_
#define _WMA_FIXED_DEBUG_H_

#include "common.h"
//-------------------------------------------------
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

static INLINE int32_t MULT32(int32_t x, int32_t y) 
{

#ifdef ARC
  register int32_t ri1=x,ri2=y,out;
   __asm__ volatile(
   	"mullw 0,%1,%2\n"
   	"machlw %0,%1,%2\n"
   	:"=r"(out)
   	:"r"(ri1),"r"(ri2)
	);
	return out;
#else
  return (int32_t)(((int64_t)(x) * (y)) >> 32);
#endif

}

static INLINE uint32_t MULT32U(uint32_t x, uint32_t y) 
{
#ifdef ARC
  register uint32_t ri1=x,ri2=y,out;
   __asm__ volatile(
   	"mululw 0,%1,%2\n"
   	"machulw %0,%1,%2\n"
   	:"=r"(out)
   	:"r"(ri1),"r"(ri2)
	);
	return out;
#else
  return (uint32_t)(((uint64_t)(x) * (y)) >> 32);
#endif
}

#define   MULT31(x,y)    (MULT32(x,y) <<1)
#define   MULT31U(x,y)   (MULT32U(x,y)<<1)
#define   MULT30(x,y)    (MULT32(x,y) <<2)
#define   MULT30U(x,y)   (MULT32U(x,y)<<2)

#ifdef ARC
static INLINE int32_t FIX_MUL(int a, int b, int c)
{ 
   if(c<=32){
		register int ra=a,rb=b,rc=c;
  		register int rsl=32-c,rv,t;
    	__asm__ volatile                
	 	(                               
	     "mullw  0,%1,%2 \n"            
	     "machlw 0,%1,%2 \n"
	     "asl   %r4,%acc1,%3\n"
         "lsr   %r5,%acc2,%4\n"
         "or    %0,%r4, %r5  \n"
	     :"=r"(rv)
	     :"r"(ra),"r"(rb),"r"(rsl),"r"(rc)
	     :"r4","r5"
	 	);  
		return rv;
   	}else{
		register int ra=a,rb=b,rc=c;
  		register int rsl=c-32,rv,t;
    	__asm__ volatile                
	 	(                               
	     "mullw  0,%1,%2 \n"            
	     "machlw 0,%1,%2 \n"
	     "asr   %0,%acc1,%3\n"
	     :"=r"(rv)
	     :"r"(ra),"r"(rb),"r"(rsl)        
	 	);  
		return rv;
   	}
}


static INLINE  int32_t FIXU_MUL(int a, int b, int c)
{  
	if(c<=32){
		register int ra=a,rb=b,rc=c;
  		register int rsl=32-c,rv,t;
    	__asm__ volatile                
	 	(                               
	     "mululw  0,%1,%2 \n"            
	     "machulw 0,%1,%2 \n"
	     "asl   %r4,%acc1,%3\n"
         "lsr   %r5,%acc2,%4\n"
         "or    %0,%r4, %r5  \n"
	     :"=r"(rv)
	     :"r"(ra),"r"(rb),"r"(rsl),"r"(rc)
	     :"r4","r5"
	 	);  
		return rv;
   	}else{
		register int ra=a,rb=b,rc=c;
  		register int rsl=c-32,rv,t;
    	__asm__ volatile                
	 	(                               
	     "mululw  0,%1,%2 \n"            
	     "machulw 0,%1,%2 \n"
	     "asr   %0,%acc1,%3\n"
	     :"=r"(rv)
	     :"r"(ra),"r"(rb),"r"(rsl)        
	 	);  
		return rv;
   	}
}
#else
#define FIX_MUL(A,B,C) ((int32_t)(((int64_t)(A)*(int64_t)(B)) >> (C)))
#define FIXU_MUL(A,B,C) ((uint32_t)(((uint64_t)(A)*(uint64_t)(B)) >> (C)))
#endif

#define FIX_MUL32 MULT32
#define FIX_MUL31(A,B) FIX_MUL(A,B,31)
#define FIX_MUL30(A,B) FIX_MUL(A,B,30)

#define FIX_MUL29(A,B) FIX_MUL(A,B,29)
#define FIX_MUL28(A,B) FIX_MUL(A,B,28)
#define FIX_MUL26(A,B) FIX_MUL(A,B,26)
#define FIX_MUL25(A,B) FIX_MUL(A,B,25)
#define FIX_MUL23(A,B) FIX_MUL(A,B,23)
#define FIX_MUL18(A,B) FIX_MUL(A,B,18)
#define FIX_MUL16(A,B) FIX_MUL(A,B,16)
#define FIX_MUL14(A,B) FIX_MUL(A,B,14)
#define FIX_MUL12(A,B) FIX_MUL(A,B,12)
#define FIXU_MUL32 MULT32U
#define FIXU_MUL31(A,B) FIXU_MUL(A,B,31)
#define FIXU_MUL30(A,B) FIXU_MUL(A,B,30)


#ifdef ARC
static INLINE uint64_t FIX64U_MUL(uint32_t x, uint32_t y) 
{
	U64 u64; 
    register uint32_t ri1=x;
	register uint32_t ri2=y;
	register uint32_t hi;
    register uint32_t lo;
	__asm__ volatile(
    "mululw 0,%2,%3\n"
    "machulw %0,%2,%3\n"
    "mov %1,%acc2\n"
    :"=r"(hi),"=r"(lo)
    :"r"(ri1),"r"(ri2)
	);
	u64.r.hi32=hi;
	u64.r.lo32=lo;
    return u64.w64; 
}
static INLINE int64_t FIX64_MUL(int32_t x, int32_t y)
{
   I64 i64;
   I64 t64;
   register int32_t ri1=x;
   
   register int32_t ri2=y;
   register int32_t hi;
   register uint32_t lo;
   __asm__ volatile
   (
   	"mullw 0,%2,%3\n"
   	"machlw %0,%2,%3\n"
   	"mov %1,%acc2\n"
    :"=r"(hi),"=r"(lo)
    :"r"(ri1),"r"(ri2)
   );
   i64.r.hi32=hi;
   i64.r.lo32=lo;
   return i64.w64; 
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

#define FIXU_DIV(A, B, C) ((uint32_t)(((uint64_t)(A) << (C))/(uint32_t)(B)))
#define FIXU_DIV32(A, B) FIXU_DIV(A, B, 32) 
#define FIXU_DIV31(A, B) FIXU_DIV(A, B, 31) 
#define FIXU_DIV30(A, B) FIXU_DIV(A, B, 30) 

//Q0,小数位31位
//---------------------------
#if 0
#define Q0_CONST(A) ((A==1.0) ? ((int32_t)0x7fffffff) : ((int32_t)((A)*(1LL<<31)))) 
#else
#define Q0_CONST(A) ((A==1.0) ? ((int32_t)0x7fffffff) : ((int32_t)((A)*((int64_t)1<<31)))) 
#endif
//----------------------------
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
//----------------------------------
//code modified by myself:
#if 0
#define QU0_CONST(A) ((uint32_t)((A)*(1LL<<32)))
#else
#define QU0_CONST(A) ((uint32_t)((A)*(((uint64_t)1)<<32)))
#endif
//-----------------------------------

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
#endif



typedef int64_t Word64;

#if 0
inline int MULSHIFT32(int x, int y)
{
    int z;

    z = (Word64)x * (Word64)y >> 32;
    
    return z;
}
#else
#define MULSHIFT32 MULT32
#endif


static INLINE Word64 MADD64(Word64 sum64, int x, int y)
{
#ifdef ARC
	sum64 += FIX64_MUL(x,y);
#else
    sum64 += (int64_t)x * (int64_t)y; 
#endif
	return sum64;
}

static INLINE Word64 MSUB64(Word64 sum64, int x, int y)
{
#ifdef ARC
    sum64 -= FIX64_MUL(x,y);
#else
    sum64 -= (int64_t)x * (int64_t)y;
#endif
    return sum64;
}

static INLINE uint64_t UMADD64(uint64_t sum64, uint32_t x, uint32_t y)
{
#ifdef ARC
    sum64 += FIX64U_MUL(x ,y);
#else
    sum64 += (uint64_t)x * y;
#endif
	return sum64;
}

/**
 * Fixed point multiply by power of two.
 *
 * @param x                     fix point value
 * @param i                     integer power-of-two, -31..+31
 */
static INLINE short cliptoshort(int x, int frac)
{
    x>>=frac;
	if(x>32767) 
		x=32767;
	else if(x<-32768)
		x=-32768;
    return x;
}

static const int32_t clztab_8bit[256]={
	8, 
	7, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static INLINE int __builtin_clz(uint32_t x)
{
	
    if(x&0xff000000)
		return clztab_8bit[x>>24];
	else if(x&0x00ff0000)
		return 8+clztab_8bit[x>>16];
	else if(x&0x0000ff00)
		return 16+clztab_8bit[x>>8];
	else
		return 24+clztab_8bit[x];
}

#define FASTCLZ(x) __builtin_clz((x))
#define fast_log2(x) (31 - FASTCLZ(x))



static INLINE int FASTABS(int x) 
{
    int sign;
    sign = x >> (sizeof(int) * 8 - 1);
    x ^= sign;
    x -= sign;
    return x;
}


#define Q28_2    0x20000000    /* Q28: 2.0 */
#define Q28_15    0x30000000    /* Q28: 1.5 */

#define NUM_ITER_IRN        5

/**************************************************************************************
 * Function:    InvRNormalized
 *
 * Description: use Newton's method to solve for x = 1/r
 *
 * Inputs:      r = Q31, range = [0.5, 1) (normalize your inputs to this range)
 *
 * Outputs:     none
 *
 * Return:      x = Q29, range ~= [1.0, 2.0]
 *
 * Notes:       guaranteed to converge and not overflow for any r in [0.5, 1)
 * 
 *              xn+1  = xn - f(xn)/f'(xn)
 *              f(x)  = 1/r - x = 0 (find root)
 *                    = 1/x - r
 *              f'(x) = -1/x^2
 *
 *              so xn+1 = xn - (1/xn - r) / (-1/xn^2)
 *                      = xn * (2 - r*xn)
 *
 *              NUM_ITER_IRN = 2, maxDiff = 6.2500e-02 (precision of about 4 bits)
 *              NUM_ITER_IRN = 3, maxDiff = 3.9063e-03 (precision of about 8 bits)
 *              NUM_ITER_IRN = 4, maxDiff = 1.5288e-05 (precision of about 16 bits)
 *              NUM_ITER_IRN = 5, maxDiff = 3.0034e-08 (precision of about 24 bits)
 **************************************************************************************/
static INLINE int InvRNormalized(int r)
{
    int i, xn, t;

    /* r =   [0.5, 1.0) 
     * 1/r = (1.0, 2.0] 
     *   so use 1.5 as initial guess 
     */
    xn = Q28_15;

    /* xn = xn*(2.0 - r*xn) */
    for (i = NUM_ITER_IRN; i != 0; i--) {
        t = MULSHIFT32(r, xn);            /* Q31*Q29 = Q28 */
        t = Q28_2 - t;                    /* Q28 */
        xn = MULSHIFT32(xn, t) << 4;    /* Q29*Q28 << 4 = Q29 */
    }

    return xn;
}


#define iter1(N) \
                try = root + (1 << (N)); \
                if (n >= try << (N))   \
                {   n -= try << (N);   \
                    root |= 2 << (N); \
                }

/*
input 32bit unsigned int
output 32bit root multiply 2^15
*/
static INLINE uint32_t isqrt32F15(uint32_t num)
{
    uint64_t root = 0, try, n = num;
    n<<=28;
    
    
    iter1 (29);    iter1 (28);
    iter1 (27);    iter1 (26);    iter1 (25);    iter1 (24);
    iter1 (23);    iter1 (22);    iter1 (21);    iter1 (20);
    iter1 (19);    iter1 (18);    iter1 (17);    iter1 (16);
    iter1 (15);    iter1 (14);    iter1 (13);    iter1 (12);
    iter1 (11);    iter1 (10);    iter1 ( 9);    iter1 ( 8);
    iter1 ( 7);    iter1 ( 6);    iter1 ( 5);    iter1 ( 4);
    iter1 ( 3);    iter1 ( 2);    iter1 ( 1);    iter1 ( 0);
    return (uint32_t)root;
}


/*
input 64 bits unsigned int, cation: n must less than 0x10000000
output 32bit root multiply 2, so you need to divide 2 by yourself
*/
static INLINE uint32_t isqrt64(uint64_t n)
{
    uint64_t root = 0, try;
    
    iter1 (29);    iter1 (28);
    iter1 (27);    iter1 (26);    iter1 (25);    iter1 (24);
    iter1 (23);    iter1 (22);    iter1 (21);    iter1 (20);
    iter1 (19);    iter1 (18);    iter1 (17);    iter1 (16);
    iter1 (15);    iter1 (14);    iter1 (13);    iter1 (12);
    iter1 (11);    iter1 (10);    iter1 ( 9);    iter1 ( 8);
    iter1 ( 7);    iter1 ( 6);    iter1 ( 5);    iter1 ( 4);
    iter1 ( 3);    iter1 ( 2);    iter1 ( 1);    iter1 ( 0);
    return (uint32_t)root;
}

static INLINE int isqrt_long(int n, int fBitsIn, int *fBitsOut)
{
    int root = 0, try;
    int z;
    
    /* force even fBitsIn */
    z = fBitsIn & 0x01;
    n >>= z;
    fBitsIn -= z;
    /* for max precision, normalize to [0x20000000, 0x7fffffff] */
    z = (CLZ(n) - 1);
    z >>= 1;
    n <<= (2*z);
    
    iter1 (15);    iter1 (14);    iter1 (13);    iter1 (12);
    iter1 (11);    iter1 (10);    iter1 ( 9);    iter1 ( 8);
    iter1 ( 7);    iter1 ( 6);    iter1 ( 5);    iter1 ( 4);
    iter1 ( 3);    iter1 ( 2);    iter1 ( 1);    iter1 ( 0);
    n<<=14; root <<= 7;
    iter1 ( 7);    iter1 ( 6);    iter1 ( 5);    iter1 ( 4);
    iter1 ( 3);    iter1 ( 2);    iter1 ( 1);    iter1 ( 0);
    *fBitsOut = (fBitsIn >> 1)+z+8;
    return root;
}

void fix_imdct_calc(unsigned int nbits, int32_t *output, const int32_t *input);


#endif


