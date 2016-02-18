#ifndef AVCODEC_H
#define AVCODEC_H

/**
 * @file avcodec.h
 * external api header.
 */


#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
//#include "rational.h"
#include <sys/types.h> /* size_t */
#if 0
#define FFMPEG_VERSION_INT     0x000408
#define FFMPEG_VERSION         "0.4.8"
#define LIBAVCODEC_BUILD       4701

#define LIBAVCODEC_VERSION_INT FFMPEG_VERSION_INT
#define LIBAVCODEC_VERSION     FFMPEG_VERSION

#define AV_STRINGIFY(s)	AV_TOSTRING(s)
#define AV_TOSTRING(s) #s
#define LIBAVCODEC_IDENT	"FFmpeg" LIBAVCODEC_VERSION "b" AV_STRINGIFY(LIBAVCODEC_BUILD)


/* CODEC_ID_MP3LAME is absolete */
#define CODEC_ID_MP3LAME CODEC_ID_MP3


/**
 * Pixel format. Notes: 
 *
 * PIX_FMT_RGBA32 is handled in an endian-specific manner. A RGBA
 * color is put together as:
 *  (A << 24) | (R << 16) | (G << 8) | B
 * This is stored as BGRA on little endian CPU architectures and ARGB on
 * big endian CPUs.
 *
 * When the pixel format is palettized RGB (PIX_FMT_PAL8), the palettized
 * image data is stored in AVFrame.data[0]. The palette is transported in
 * AVFrame.data[1] and, is 1024 bytes long (256 4-byte entries) and is
 * formatted the same as in PIX_FMT_RGBA32 described above (i.e., it is
 * also endian-specific). Note also that the individual RGB palette
 * components stored in AVFrame.data[1] should be in the range 0..255.
 * This is important as many custom PAL8 video codecs that were designed
 * to run on the IBM VGA graphics adapter use 6-bit palette components.
 */


/* currently unused, may be used if 24/32 bits samples ever supported */
//enum SampleFormat
//{
//    SAMPLE_FMT_S16 = 0,         ///< signed 16 bits 
//};
#endif

/* in bytes */
#define AVCODEC_MAX_AUDIO_FRAME_SIZE 131072

/**
 * Required number of additionally allocated bytes at the end of the input bitstream for decoding.
 * this is mainly needed because some optimized bitstream readers read 
 * 32 or 64 bit at once and could read over the end<br>
 * Note, if the first 23 bits of the additional bytes are not 0 then damaged
 * MPEG bitstreams could cause overread and segfault
 */
#define FF_INPUT_BUFFER_PADDING_SIZE 16

/* motion estimation type, EPZS by default */
enum Motion_Est_ID 
{
    ME_ZERO = 1,
    ME_FULL,
    ME_LOG,
    ME_PHODS,
    ME_EPZS,
    ME_X1
};

#if 0

/* only for ME compatiblity with old apps */
extern int motion_estimation_method;

#define FF_MAX_B_FRAMES 8

/* encoding support
   these flags can be passed in AVCodecContext.flags before initing 
   Note: note not everything is supported yet 
*/

#define CODEC_FLAG_QSCALE 0x0002  ///< use fixed qscale 
#define CODEC_FLAG_4MV    0x0004  ///< 4 MV per MB allowed / Advanced prediction for H263
#define CODEC_FLAG_QPEL   0x0010  ///< use qpel MC 
#define CODEC_FLAG_GMC    0x0020  ///< use GMC 
#define CODEC_FLAG_MV0    0x0040  ///< always try a MB with MV=<0,0> 
#define CODEC_FLAG_PART   0x0080  ///< use data partitioning 
/* parent program gurantees that the input for b-frame containing streams is not written to 
   for at least s->max_b_frames+1 frames, if this is not set than the input will be copied */
#define CODEC_FLAG_INPUT_PRESERVED 0x0100
#define CODEC_FLAG_PASS1 0x0200   ///< use internal 2pass ratecontrol in first  pass mode 
#define CODEC_FLAG_PASS2 0x0400   ///< use internal 2pass ratecontrol in second pass mode 
#define CODEC_FLAG_EXTERN_HUFF 0x1000 ///< use external huffman table (for mjpeg) 
#define CODEC_FLAG_GRAY  0x2000   ///< only decode/encode grayscale 
#define CODEC_FLAG_EMU_EDGE 0x4000///< dont draw edges 
#define CODEC_FLAG_PSNR           0x8000 ///< error[?] variables will be set during encoding 
#define CODEC_FLAG_TRUNCATED  0x00010000 /** input bitstream might be truncated at a random location instead 
                                            of only at frame boundaries */
#define CODEC_FLAG_NORMALIZE_AQP  0x00020000 ///< normalize adaptive quantization 
#define CODEC_FLAG_INTERLACED_DCT 0x00040000 ///< use interlaced dct 
#define CODEC_FLAG_LOW_DELAY      0x00080000 ///< force low delay
#define CODEC_FLAG_ALT_SCAN       0x00100000 ///< use alternate scan 
#define CODEC_FLAG_TRELLIS_QUANT  0x00200000 ///< use trellis quantization 
#define CODEC_FLAG_GLOBAL_HEADER  0x00400000 ///< place global headers in extradata instead of every keyframe 
#define CODEC_FLAG_BITEXACT       0x00800000 ///< use only bitexact stuff (except (i)dct) 
/* Fx : Flag for h263+ extra options */
#define CODEC_FLAG_H263P_AIC      0x01000000 ///< H263 Advanced intra coding / MPEG4 AC prediction (remove this)
#define CODEC_FLAG_AC_PRED        0x01000000 ///< H263 Advanced intra coding / MPEG4 AC prediction
#define CODEC_FLAG_H263P_UMV      0x02000000 ///< Unlimited motion vector  
#define CODEC_FLAG_CBP_RD         0x04000000 ///< use rate distortion optimization for cbp
#define CODEC_FLAG_QP_RD          0x08000000 ///< use rate distortion optimization for qp selectioon
#define CODEC_FLAG_H263P_AIV      0x00000008 ///< H263 Alternative inter vlc
#define CODEC_FLAG_OBMC           0x00000001 ///< OBMC
#define CODEC_FLAG_LOOP_FILTER    0x00000800 ///< loop filter
#define CODEC_FLAG_H263P_SLICE_STRUCT 0x10000000
#define CODEC_FLAG_INTERLACED_ME  0x20000000 ///< interlaced motion estimation
#define CODEC_FLAG_SVCD_SCAN_OFFSET 0x40000000 ///< will reserve space for SVCD scan offset user data
#define CODEC_FLAG_CLOSED_GOP     0x80000000
/* Unsupported options :
 * 		Syntax Arithmetic coding (SAC)
 * 		Reference Picture Selection
 * 		Independant Segment Decoding */
/* /Fx */
/* codec capabilities */

#define CODEC_CAP_DRAW_HORIZ_BAND 0x0001 ///< decoder can use draw_horiz_band callback 
/**
 * Codec uses get_buffer() for allocating buffers.
 * direct rendering method 1
 */
#define CODEC_CAP_DR1             0x0002
/* if 'parse_only' field is true, then avcodec_parse_frame() can be
   used */
#define CODEC_CAP_PARSE_ONLY      0x0004
#define CODEC_CAP_TRUNCATED       0x0008

//the following defines might change, so dont expect compatibility if u use them
#define MB_TYPE_INTRA4x4   0x0001
#define MB_TYPE_INTRA16x16 0x0002 //FIXME h264 specific
#define MB_TYPE_INTRA_PCM  0x0004 //FIXME h264 specific
#define MB_TYPE_16x16      0x0008
#define MB_TYPE_16x8       0x0010
#define MB_TYPE_8x16       0x0020
#define MB_TYPE_8x8        0x0040
#define MB_TYPE_INTERLACED 0x0080
#define MB_TYPE_DIRECT2     0x0100 //FIXME
#define MB_TYPE_ACPRED     0x0200
#define MB_TYPE_GMC        0x0400
#define MB_TYPE_SKIP       0x0800
#define MB_TYPE_P0L0       0x1000
#define MB_TYPE_P1L0       0x2000
#define MB_TYPE_P0L1       0x4000
#define MB_TYPE_P1L1       0x8000
#define MB_TYPE_L0         (MB_TYPE_P0L0 | MB_TYPE_P1L0)
#define MB_TYPE_L1         (MB_TYPE_P0L1 | MB_TYPE_P1L1)
#define MB_TYPE_L0L1       (MB_TYPE_L0   | MB_TYPE_L1)
#define MB_TYPE_QUANT      0x00010000
#define MB_TYPE_CBP        0x00020000
//Note bits 24-31 are reserved for codec specific use (h264 ref0, mpeg1 0mv, ...)

/**
 * Pan Scan area.
 * this specifies the area which should be displayed. Note there may be multiple such areas for one frame
 */


#define FF_QSCALE_TYPE_MPEG1	0
#define FF_QSCALE_TYPE_MPEG2	1

#define FF_BUFFER_TYPE_INTERNAL 1
#define FF_BUFFER_TYPE_USER     2 ///< Direct rendering buffers (image is (de)allocated by user)
#define FF_BUFFER_TYPE_SHARED   4 ///< buffer from somewher else, dont dealloc image (data/base)
#define FF_BUFFER_TYPE_COPY     8 ///< just a (modified) copy of some other buffer, dont dealloc anything
#endif


#define FF_I_TYPE 1 // Intra
#define FF_P_TYPE 2 // Predicted
#define FF_B_TYPE 3 // Bi-dir predicted
#define FF_S_TYPE 4 // S(GMC)-VOP MPEG4
#define FF_SI_TYPE 5
#define FF_SP_TYPE 6
#if 0
#define FF_BUFFER_HINTS_VALID    0x01 // Buffer hints value is meaningful (if 0 ignore)
#define FF_BUFFER_HINTS_READABLE 0x02 // Codec will read from buffer
#define FF_BUFFER_HINTS_PRESERVE 0x04 // User must not alter buffer content
#define FF_BUFFER_HINTS_REUSABLE 0x08 // Codec will reuse the buffer (update)



#define DEFAULT_FRAME_RATE_BASE 1001000



#endif

/**
 * AVOption.
 */
typedef struct AVOption 
{
    /** options' name */
    const char *name; /* if name is NULL, it indicates a link to next */
    /** short English text help or const struct AVOption* subpointer */
    const char *help; //	const struct AVOption* sub;
    /** offset to context structure where the parsed value should be stored */
    int offset;
    /** options' type */
    int type;
#define FF_OPT_TYPE_BOOL 1      ///< boolean - true,1,on  (or simply presence)
#define FF_OPT_TYPE_DOUBLE 2    ///< double
#define FF_OPT_TYPE_INT 3       ///< integer
#define FF_OPT_TYPE_STRING 4    ///< string (finished with \0)
#define FF_OPT_TYPE_MASK 0x1f	///< mask for types - upper bits are various flags
//#define FF_OPT_TYPE_EXPERT 0x20 // flag for expert option
#define FF_OPT_TYPE_FLAG (FF_OPT_TYPE_BOOL | 0x40)
#define FF_OPT_TYPE_RCOVERRIDE (FF_OPT_TYPE_STRING | 0x80)
    /** min value  (min == max   ->  no limits) */
    double min;
    /** maximum value for double/int */
    double max;
    /** default boo [0,1]l/double/int value */
    double defval;
    /**
     * default string value (with optional semicolon delimited extra option-list
     * i.e.   option1;option2;option3
     * defval might select other then first argument as default
     */
    const char *defstr;
#define FF_OPT_MAX_DEPTH 10
} AVOption;
#if 0
/**
 * Parse option(s) and sets fields in passed structure
 * @param strct	structure where the parsed results will be written
 * @param list  list with AVOptions
 * @param opts	string with options for parsing
 */
int avoption_parse(void* strct, const AVOption* list, const char* opts);


/**
 * four components are given, that's all.
 * the last component is alpha
 */

/**
 * AVPaletteControl
 * This structure defines a method for communicating palette changes
 * between and demuxer and a decoder.
 */
#define AVPALETTE_SIZE 1024
#define AVPALETTE_COUNT 256
typedef struct AVPaletteControl 
{

    /* demuxer sets this to 1 to indicate the palette has changed;
     * decoder resets to 0 */
    int palette_changed;

    /* 4-byte ARGB palette entries, stored in native byte order; note that
     * the individual palette components should be on a 8-bit scale; if
     * the palette data comes from a IBM VGA native format, the component
     * data is probably 6 bits in size and needs to be scaled */
    unsigned int palette[AVPALETTE_COUNT];

} AVPaletteControl;



#undef PCM_CODEC



/* external high level API */



/* returns LIBAVCODEC_VERSION_INT constant */
unsigned avcodec_version(void);
/* returns LIBAVCODEC_BUILD constant */
unsigned avcodec_build(void);
void avcodec_init(void);









/* misc usefull functions */

/**
 * returns a single letter to describe the picture type
 */
char av_get_pict_type_char(int pict_type);

/**
 * reduce a fraction.
 * this is usefull for framerate calculations
 * @param max the maximum allowed for dst_nom & dst_den
 * @return 1 if exact, 0 otherwise
 */
int av_reduce(int *dst_nom, int *dst_den, int64_t nom, int64_t den, int64_t max);

/**
 * rescale a 64bit integer.
 * a simple a*b/c isnt possible as it can overflow
 */
int64_t av_rescale(int64_t a, int b, int c);


/**
 * Interface for 0.5.0 version
 *
 * do not even think about it's usage for this moment
 */


/**
 * Commands
 * order can't be changed - once it was defined
 */
typedef enum 
{
    // general commands
    AVC_OPEN_BY_NAME = 0xACA000,
    AVC_OPEN_BY_CODEC_ID,
    AVC_OPEN_BY_FOURCC,
    AVC_CLOSE,

    AVC_FLUSH,
    // pin - struct { uint8_t* src, uint_t src_size }
    // pout - struct { AVPicture* img, consumed_bytes,
    AVC_DECODE,
    // pin - struct { AVPicture* img, uint8_t* dest, uint_t dest_size }
    // pout - uint_t used_from_dest_size
    AVC_ENCODE, 

    // query/get video commands
    AVC_GET_VERSION = 0xACB000,
    AVC_GET_WIDTH,
    AVC_GET_HEIGHT,
    AVC_GET_DELAY,
    AVC_GET_QUANT_TABLE,
    // ...

    // query/get audio commands
    AVC_GET_FRAME_SIZE = 0xABC000,

    // maybe define some simple structure which
    // might be passed to the user - but they can't
    // contain any codec specific parts and these
    // calls are usualy necessary only few times

    // set video commands
    AVC_SET_WIDTH = 0xACD000,
    AVC_SET_HEIGHT,

    // set video encoding commands
    AVC_SET_FRAME_RATE = 0xACD800,
    AVC_SET_QUALITY,
    AVC_SET_HURRY_UP,

    // set audio commands
    AVC_SET_SAMPLE_RATE = 0xACE000,
    AVC_SET_CHANNELS,

} avc_cmd_t;

/**
 * \param handle  allocated private structure by libavcodec
 *                for initialization pass NULL - will be returned pout
 *                user is supposed to know nothing about its structure
 * \param cmd     type of operation to be performed
 * \param pint    input parameter
 * \param pout    output parameter
 *
 * \returns  command status - eventually for query command it might return
 * integer resulting value
 */
int avcodec(void* handle, avc_cmd_t cmd, void* pin, void* pout);






/* memory */
//void *av_malloc(unsigned int size);
void *av_mallocz(unsigned int size);
void *av_realloc(void *ptr, unsigned int size);
void av_free(void *ptr);
char *av_strdup(const char *s);
void __av_freep(void **ptr);

void *av_fast_realloc(void *ptr, unsigned int *size, unsigned int min_size);
/* for static data only */
/* call av_free_static to release all staticaly allocated tables */
void av_free_static(void);
void *__av_mallocz_static(void** location, unsigned int size);
#define av_mallocz_static(p, s) __av_mallocz_static((void **)(p), s)


/* av_log API */

#include <stdarg.h>

#define AV_LOG_ERROR 0
#define AV_LOG_INFO 1
#define AV_LOG_DEBUG 2



#undef  AV_LOG_TRAP_PRINTF
#ifdef AV_LOG_TRAP_PRINTF
#define printf DO NOT USE
#define fprintf DO NOT USE
#undef stderr
#define stderr DO NOT USE
#endif
#endif
#ifdef __cplusplus
}
#endif

#endif /* AVCODEC_H */
