#ifndef AVFORMAT_H
#define AVFORMAT_H

namespace android {


#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
typedef int64_t offset_t;
enum PixelFormatP
{
    PIX_FMT_YUV420P,   ///< Planar YUV 4:2:0 (1 Cr & Cb sample per 2x2 Y samples)
    PIX_FMT_YUV422,    
    PIX_FMT_RGB24,     ///< Packed pixel, 3 bytes per pixel, RGBRGB...
    PIX_FMT_BGR24,     ///< Packed pixel, 3 bytes per pixel, BGRBGR...
    PIX_FMT_YUV422P,   ///< Planar YUV 4:2:2 (1 Cr & Cb sample per 2x1 Y samples)
    PIX_FMT_YUV444P,   ///< Planar YUV 4:4:4 (1 Cr & Cb sample per 1x1 Y samples)
    PIX_FMT_RGBA32,    ///< Packed pixel, 4 bytes per pixel, BGRABGRA..., stored in cpu endianness
    PIX_FMT_YUV410P,   ///< Planar YUV 4:1:0 (1 Cr & Cb sample per 4x4 Y samples)
    PIX_FMT_YUV411P,   ///< Planar YUV 4:1:1 (1 Cr & Cb sample per 4x1 Y samples)
    PIX_FMT_RGB565,    ///< always stored in cpu endianness 
    PIX_FMT_RGB555,    ///< always stored in cpu endianness, most significant bit to 1 
    PIX_FMT_GRAY8,
    PIX_FMT_MONOWHITE, ///< 0 is white 
    PIX_FMT_MONOBLACK, ///< 0 is black 
    PIX_FMT_PAL8,      ///< 8 bit with RGBA palette 
    PIX_FMT_YUVJ420P,  ///< Planar YUV 4:2:0 full scale (jpeg)
    PIX_FMT_YUVJ422P,  ///< Planar YUV 4:2:2 full scale (jpeg)
    PIX_FMT_YUVJ444P,  ///< Planar YUV 4:4:4 full scale (jpeg)
    PIX_FMT_XVMC_MPEG2_MC,///< XVideo Motion Acceleration via common packet passing(xvmc_render.h)
    PIX_FMT_XVMC_MPEG2_IDCT,
    PIX_FMT_NB,
};

enum CodecType 
{
    CODEC_TYPE_UNKNOWN = -1,
    CODEC_TYPE_VIDEO,
    CODEC_TYPE_AUDIO,
    CODEC_TYPE_DATA,
};
#ifndef MKTAG
#define MKTAG(a,b,c,d) ((a) | ((b) << 8) | ((c) << 16) | ((unsigned)(d) << 24))
#endif
#ifndef MKBETAG
#define MKBETAG(a,b,c,d) ((d) | ((c) << 8) | ((b) << 16) | ((unsigned)(a) << 24))
#endif
enum CodecID
{
    CODEC_ID_NONE = 0,///AV_CODEC_ID_NONE,

    /* video codecs */
    CODEC_ID_MPEG1VIDEO,
    CODEC_ID_MPEG2VIDEO, ///< preferred ID for MPEG-1/2 video decoding
    CODEC_ID_MPEG2VIDEO_XVMC,
    CODEC_ID_H261,
    CODEC_ID_H263,
    CODEC_ID_RV10,
    CODEC_ID_RV20,
    CODEC_ID_MJPEG,
    CODEC_ID_MJPEGB,
    CODEC_ID_LJPEG,
    CODEC_ID_SP5X,
    CODEC_ID_JPEGLS,
    CODEC_ID_MPEG4,
    CODEC_ID_RAWVIDEO,
    CODEC_ID_MSMPEG4V1,
    CODEC_ID_MSMPEG4V2,
    CODEC_ID_MSMPEG4V3,
    CODEC_ID_WMV1,
    CODEC_ID_WMV2,
    CODEC_ID_H263P,
    CODEC_ID_H263I,
    CODEC_ID_FLV1,
    CODEC_ID_SVQ1,
    CODEC_ID_SVQ3,
    CODEC_ID_DVVIDEO,
    CODEC_ID_HUFFYUV,
    CODEC_ID_CYUV,
    CODEC_ID_H264,
    CODEC_ID_INDEO3,
    CODEC_ID_VP3,
    CODEC_ID_THEORA,
    CODEC_ID_ASV1,
    CODEC_ID_ASV2,
    CODEC_ID_FFV1,
    CODEC_ID_4XM,
    CODEC_ID_VCR1,
    CODEC_ID_CLJR,
    CODEC_ID_MDEC,
    CODEC_ID_ROQ,
    CODEC_ID_INTERPLAY_VIDEO,
    CODEC_ID_XAN_WC3,
    CODEC_ID_XAN_WC4,
    CODEC_ID_RPZA,
    CODEC_ID_CINEPAK,
    CODEC_ID_WS_VQA,
    CODEC_ID_MSRLE,
    CODEC_ID_MSVIDEO1,
    CODEC_ID_IDCIN,
    CODEC_ID_8BPS,
    CODEC_ID_SMC,
    CODEC_ID_FLIC,
    CODEC_ID_TRUEMOTION1,
    CODEC_ID_VMDVIDEO,
    CODEC_ID_MSZH,
    CODEC_ID_ZLIB,
    CODEC_ID_QTRLE,
    CODEC_ID_SNOW,
    CODEC_ID_TSCC,
    CODEC_ID_ULTI,
    CODEC_ID_QDRAW,
    CODEC_ID_VIXL,
    CODEC_ID_QPEG,
    CODEC_ID_PNG,
    CODEC_ID_PPM,
    CODEC_ID_PBM,
    CODEC_ID_PGM,
    CODEC_ID_PGMYUV,
    CODEC_ID_PAM,
    CODEC_ID_FFVHUFF,
    CODEC_ID_RV30,
    CODEC_ID_RV40,
    CODEC_ID_VC1,
    CODEC_ID_WMV3,
    CODEC_ID_LOCO,
    CODEC_ID_WNV1,
    CODEC_ID_AASC,
    CODEC_ID_INDEO2,
    CODEC_ID_FRAPS,
    CODEC_ID_TRUEMOTION2,
    CODEC_ID_BMP,
    CODEC_ID_CSCD,
    CODEC_ID_MMVIDEO,
    CODEC_ID_ZMBV,
    CODEC_ID_AVS,
    CODEC_ID_SMACKVIDEO,
    CODEC_ID_NUV,
    CODEC_ID_KMVC,
    CODEC_ID_FLASHSV,
    CODEC_ID_CAVS,
    CODEC_ID_JPEG2000,
    CODEC_ID_VMNC,
    CODEC_ID_VP5,
    CODEC_ID_VP6,
    CODEC_ID_VP6F,
    CODEC_ID_TARGA,
    CODEC_ID_DSICINVIDEO,
    CODEC_ID_TIERTEXSEQVIDEO,
    CODEC_ID_TIFF,
    CODEC_ID_GIF,
    CODEC_ID_DXA,
    CODEC_ID_DNXHD,
    CODEC_ID_THP,
    CODEC_ID_SGI,
    CODEC_ID_C93,
    CODEC_ID_BETHSOFTVID,
    CODEC_ID_PTX,
    CODEC_ID_TXD,
    CODEC_ID_VP6A,
    CODEC_ID_AMV,
    CODEC_ID_VB,
    CODEC_ID_PCX,
    CODEC_ID_SUNRAST,
    CODEC_ID_INDEO4,
    CODEC_ID_INDEO5,
    CODEC_ID_MIMIC,
    CODEC_ID_RL2,
    CODEC_ID_ESCAPE124,
    CODEC_ID_DIRAC,
    CODEC_ID_BFI,
    CODEC_ID_CMV,
    CODEC_ID_MOTIONPIXELS,
    CODEC_ID_TGV,
    CODEC_ID_TGQ,
    CODEC_ID_TQI,
    CODEC_ID_AURA,
    CODEC_ID_AURA2,
    CODEC_ID_V210X,
    CODEC_ID_TMV,
    CODEC_ID_V210,
    CODEC_ID_DPX,
    CODEC_ID_MAD,
    CODEC_ID_FRWU,
    CODEC_ID_FLASHSV2,
    CODEC_ID_CDGRAPHICS,
    CODEC_ID_R210,
    CODEC_ID_ANM,
    CODEC_ID_BINKVIDEO,
    CODEC_ID_IFF_ILBM,
    CODEC_ID_IFF_BYTERUN1,
    CODEC_ID_KGV1,
    CODEC_ID_YOP,
    CODEC_ID_VP8,
    CODEC_ID_PICTOR,
    CODEC_ID_ANSI,
    CODEC_ID_A64_MULTI,
    CODEC_ID_A64_MULTI5,
    CODEC_ID_R10K,
    CODEC_ID_MXPEG,
    CODEC_ID_LAGARITH,
    CODEC_ID_PRORES,
    CODEC_ID_JV,
    CODEC_ID_DFA,
    CODEC_ID_WMV3IMAGE,
    CODEC_ID_VC1IMAGE,
    CODEC_ID_UTVIDEO,
    CODEC_ID_BMV_VIDEO,
    CODEC_ID_VBLE,
    CODEC_ID_DXTORY,
    CODEC_ID_V410,
    CODEC_ID_XWD,
    CODEC_ID_CDXL,
    CODEC_ID_XBM,
    CODEC_ID_ZEROCODEC,
    CODEC_ID_MSS1,
    CODEC_ID_MSA1,
    CODEC_ID_TSCC2,
    CODEC_ID_MTS2,
    CODEC_ID_CLLC,
    CODEC_ID_Y41P       = MKBETAG('Y','4','1','P'),
    CODEC_ID_ESCAPE130  = MKBETAG('E','1','3','0'),
    CODEC_ID_EXR        = MKBETAG('0','E','X','R'),
    CODEC_ID_AVRP       = MKBETAG('A','V','R','P'),

    CODEC_ID_G2M        = MKBETAG( 0 ,'G','2','M'),
    CODEC_ID_AVUI       = MKBETAG('A','V','U','I'),
    CODEC_ID_AYUV       = MKBETAG('A','Y','U','V'),
    CODEC_ID_V308       = MKBETAG('V','3','0','8'),
    CODEC_ID_V408       = MKBETAG('V','4','0','8'),
    CODEC_ID_YUV4       = MKBETAG('Y','U','V','4'),
    CODEC_ID_SANM       = MKBETAG('S','A','N','M'),
    CODEC_ID_PAF_VIDEO  = MKBETAG('P','A','F','V'),

    /* various PCM "codecs" */
    CODEC_ID_FIRST_AUDIO = 0x10000,     ///< A dummy id pointing at the start of audio codecs
    CODEC_ID_PCM_S16LE = 0x10000,
    CODEC_ID_PCM_S16BE,
    CODEC_ID_PCM_U16LE,
    CODEC_ID_PCM_U16BE,
    CODEC_ID_PCM_S8,
    CODEC_ID_PCM_U8,
    CODEC_ID_PCM_MULAW,
    CODEC_ID_PCM_ALAW,
    CODEC_ID_PCM_S32LE,
    CODEC_ID_PCM_S32BE,
    CODEC_ID_PCM_U32LE,
    CODEC_ID_PCM_U32BE,
    CODEC_ID_PCM_S24LE,
    CODEC_ID_PCM_S24BE,
    CODEC_ID_PCM_U24LE,
    CODEC_ID_PCM_U24BE,
    CODEC_ID_PCM_S24DAUD,
    CODEC_ID_PCM_ZORK,
    CODEC_ID_PCM_S16LE_PLANAR,
    CODEC_ID_PCM_DVD,
    CODEC_ID_PCM_F32BE,
    CODEC_ID_PCM_F32LE,
    CODEC_ID_PCM_F64BE,
    CODEC_ID_PCM_F64LE,
    CODEC_ID_PCM_BLURAY,
    CODEC_ID_PCM_LXF,
    CODEC_ID_S302M,
    CODEC_ID_PCM_S8_PLANAR,

    /* various ADPCM codecs */
    CODEC_ID_ADPCM_IMA_QT = 0x11000,
    CODEC_ID_ADPCM_IMA_WAV,
    CODEC_ID_ADPCM_IMA_DK3,
    CODEC_ID_ADPCM_IMA_DK4,
    CODEC_ID_ADPCM_IMA_WS,
    CODEC_ID_ADPCM_IMA_SMJPEG,
    CODEC_ID_ADPCM_MS,
    CODEC_ID_ADPCM_4XM,
    CODEC_ID_ADPCM_XA,
    CODEC_ID_ADPCM_ADX,
    CODEC_ID_ADPCM_EA,
    CODEC_ID_ADPCM_G726,
    CODEC_ID_ADPCM_CT,
    CODEC_ID_ADPCM_SWF,
    CODEC_ID_ADPCM_YAMAHA,
    CODEC_ID_ADPCM_SBPRO_4,
    CODEC_ID_ADPCM_SBPRO_3,
    CODEC_ID_ADPCM_SBPRO_2,
    CODEC_ID_ADPCM_THP,
    CODEC_ID_ADPCM_IMA_AMV,
    CODEC_ID_ADPCM_EA_R1,
    CODEC_ID_ADPCM_EA_R3,
    CODEC_ID_ADPCM_EA_R2,
    CODEC_ID_ADPCM_IMA_EA_SEAD,
    CODEC_ID_ADPCM_IMA_EA_EACS,
    CODEC_ID_ADPCM_EA_XAS,
    CODEC_ID_ADPCM_EA_MAXIS_XA,
    CODEC_ID_ADPCM_IMA_ISS,
    CODEC_ID_ADPCM_G722,
    CODEC_ID_ADPCM_IMA_APC,
    CODEC_ID_VIMA       = MKBETAG('V','I','M','A'),

    /* AMR */
    CODEC_ID_AMR_NB = 0x12000,
    CODEC_ID_AMR_WB,

    /* RealAudio codecs*/
    CODEC_ID_RA_144 = 0x13000,
    CODEC_ID_RA_288,

    /* various DPCM codecs */
    CODEC_ID_ROQ_DPCM = 0x14000,
    CODEC_ID_INTERPLAY_DPCM,
    CODEC_ID_XAN_DPCM,
    CODEC_ID_SOL_DPCM,

    /* audio codecs */
    CODEC_ID_MP2 = 0x15000,
    CODEC_ID_MP3, ///< preferred ID for decoding MPEG audio layer 1, 2 or 3
    CODEC_ID_AAC,
    CODEC_ID_AC3,
    CODEC_ID_DTS,
    CODEC_ID_VORBIS,
    CODEC_ID_DVAUDIO,
    CODEC_ID_WMAV1,
    CODEC_ID_WMAV2,
    CODEC_ID_MACE3,
    CODEC_ID_MACE6,
    CODEC_ID_VMDAUDIO,
    CODEC_ID_FLAC,
    CODEC_ID_MP3ADU,
    CODEC_ID_MP3ON4,
    CODEC_ID_SHORTEN,
    CODEC_ID_ALAC,
    CODEC_ID_WESTWOOD_SND1,
    CODEC_ID_GSM, ///< as in Berlin toast format
    CODEC_ID_QDM2,
    CODEC_ID_COOK,
    CODEC_ID_TRUESPEECH,
    CODEC_ID_TTA,
    CODEC_ID_SMACKAUDIO,
    CODEC_ID_QCELP,
    CODEC_ID_WAVPACK,
    CODEC_ID_DSICINAUDIO,
    CODEC_ID_IMC,
    CODEC_ID_MUSEPACK7,
    CODEC_ID_MLP,
    CODEC_ID_GSM_MS, /* as found in WAV */
    CODEC_ID_ATRAC3,
    CODEC_ID_VOXWARE,
    CODEC_ID_APE,
    CODEC_ID_NELLYMOSER,
    CODEC_ID_MUSEPACK8,
    CODEC_ID_SPEEX,
    CODEC_ID_WMAVOICE,
    CODEC_ID_WMAPRO,
    CODEC_ID_WMALOSSLESS,
    CODEC_ID_ATRAC3P,
    CODEC_ID_EAC3,
    CODEC_ID_SIPR,
    CODEC_ID_MP1,
    CODEC_ID_TWINVQ,
    CODEC_ID_TRUEHD,
    CODEC_ID_MP4ALS,
    CODEC_ID_ATRAC1,
    CODEC_ID_BINKAUDIO_RDFT,
    CODEC_ID_BINKAUDIO_DCT,
    CODEC_ID_AAC_LATM,
    CODEC_ID_QDMC,
    CODEC_ID_CELT,
    CODEC_ID_G723_1,
    CODEC_ID_G729,
    CODEC_ID_8SVX_EXP,
    CODEC_ID_8SVX_FIB,
    CODEC_ID_BMV_AUDIO,
    CODEC_ID_RALF,
    CODEC_ID_IAC,
    CODEC_ID_ILBC,
    CODEC_ID_FFWAVESYNTH = MKBETAG('F','F','W','S'),
    CODEC_ID_8SVX_RAW    = MKBETAG('8','S','V','X'),
    CODEC_ID_SONIC       = MKBETAG('S','O','N','C'),
    CODEC_ID_SONIC_LS    = MKBETAG('S','O','N','L'),
    CODEC_ID_PAF_AUDIO   = MKBETAG('P','A','F','A'),
    CODEC_ID_OPUS        = MKBETAG('O','P','U','S'),

    /* subtitle codecs */
    CODEC_ID_FIRST_SUBTITLE = 0x17000,          ///< A dummy ID pointing at the start of subtitle codecs.
    CODEC_ID_DVD_SUBTITLE = 0x17000,
    CODEC_ID_DVB_SUBTITLE,
    CODEC_ID_TEXT,  ///< raw UTF-8 text
    CODEC_ID_XSUB,
    CODEC_ID_SSA,
    CODEC_ID_MOV_TEXT,
    CODEC_ID_HDMV_PGS_SUBTITLE,
    CODEC_ID_DVB_TELETEXT,
    CODEC_ID_SRT,
    CODEC_ID_MICRODVD   = MKBETAG('m','D','V','D'),
    CODEC_ID_EIA_608    = MKBETAG('c','6','0','8'),
    CODEC_ID_JACOSUB    = MKBETAG('J','S','U','B'),
    CODEC_ID_SAMI       = MKBETAG('S','A','M','I'),
    CODEC_ID_REALTEXT   = MKBETAG('R','T','X','T'),
    CODEC_ID_SUBVIEWER  = MKBETAG('S','u','b','V'),

    /* other specific kind of codecs (generally used for attachments) */
    CODEC_ID_FIRST_UNKNOWN = 0x18000,           ///< A dummy ID pointing at the start of various fake codecs.
    CODEC_ID_TTF = 0x18000,
    CODEC_ID_BINTEXT    = MKBETAG('B','T','X','T'),
    CODEC_ID_XBIN       = MKBETAG('X','B','I','N'),
    CODEC_ID_IDF        = MKBETAG( 0 ,'I','D','F'),
    CODEC_ID_OTF        = MKBETAG( 0 ,'O','T','F'),

    CODEC_ID_PROBE = 0x19000, ///< codec_id is not known (like CODEC_ID_NONE) but lavf should attempt to identify it

    CODEC_ID_MPEG2TS = 0x20000, /**< _FAKE_ codec to indicate a raw MPEG-2 TS
                                * stream (only used by libavformat) */
    CODEC_ID_MPEG4SYSTEMS = 0x20001, /**< _FAKE_ codec to indicate a MPEG-4 Systems
                                * stream (only used by libavformat) */
    CODEC_ID_FFMETADATA = 0x21000,   ///< Dummy codec for streams containing only metadata information.

};

typedef struct CodecTag
{
    int id;
    unsigned int tag;
    unsigned int invalid_asf : 1;
} CodecTag;


typedef struct AVPanScan
{
    /**
     * id.
     * - encoding: set by user.
     * - decoding: set by lavc
     */
    int id;

    /**
     * width and height in 1/16 pel
     * - encoding: set by user.
     * - decoding: set by lavc
     */
    int width;
    int height;

    /**
     * position of the top left corner in 1/16 pel for up to 3 fields/frames.
     * - encoding: set by user.
     * - decoding: set by lavc
     */
    int16_t position[3][2];
}AVPanScan;

typedef struct RcOverride
{
    int start_frame;
    int end_frame;
    int qscale; // if this is 0 then quality_factor will be used instead
    float quality_factor;
} RcOverride;


#define FF_COMMON_FRAME \
    /**\
     * pointer to the picture planes.\
     * this might be different from the first allocated byte\
     * - encoding: \
     * - decoding: \
     */\
    uint8_t *data[4];\
    int linesize[4];\
    /**\
     * pointer to the first allocated byte of the picture. can be used in get_buffer/release_buffer\
     * this isnt used by lavc unless the default get/release_buffer() is used\
     * - encoding: \
     * - decoding: \
     */\
    uint8_t *base[4];\
    /**\
     * 1 -> keyframe, 0-> not\
     * - encoding: set by lavc\
     * - decoding: set by lavc\
     */\
    int key_frame;\
\
    /**\
     * picture type of the frame, see ?_TYPE below.\
     * - encoding: set by lavc for coded_picture (and set by user for input)\
     * - decoding: set by lavc\
     */\
    int pict_type;\
\
    /**\
     * presentation timestamp in micro seconds (time when frame should be shown to user)\
     * if 0 then the frame_rate will be used as reference\
     * - encoding: MUST be set by user\
     * - decoding: set by lavc\
     */\
    int64_t pts;\
\
    /**\
     * picture number in bitstream order.\
     * - encoding: set by\
     * - decoding: set by lavc\
     */\
    int coded_picture_number;\
    /**\
     * picture number in display order.\
     * - encoding: set by\
     * - decoding: set by lavc\
     */\
    int display_picture_number;\
\
    /**\
     * quality (between 1 (good) and FF_LAMBDA_MAX (bad)) \
     * - encoding: set by lavc for coded_picture (and set by user for input)\
     * - decoding: set by lavc\
     */\
    int quality; \
\
    /**\
     * buffer age (1->was last buffer and dint change, 2->..., ...).\
     * set to INT_MAX if the buffer has not been used yet \
     * - encoding: unused\
     * - decoding: MUST be set by get_buffer()\
     */\
    int age;\
\
    /**\
     * is this picture used as reference\
     * - encoding: unused\
     * - decoding: set by lavc (before get_buffer() call))\
     */\
    int reference;\
\
    /**\
     * QP table\
     * - encoding: unused\
     * - decoding: set by lavc\
     */\
    int8_t *qscale_table;\
    /**\
     * QP store stride\
     * - encoding: unused\
     * - decoding: set by lavc\
     */\
    int qstride;\
\
    /**\
     * mbskip_table[mb]>=1 if MB didnt change\
     * stride= mb_width = (width+15)>>4\
     * - encoding: unused\
     * - decoding: set by lavc\
     */\
    uint8_t *mbskip_table;\
\
    /**\
     * Motion vector table\
     * - encoding: unused\
     * - decoding: set by lavc\
     */\
    int16_t (*motion_val[2])[2];\
\
    /**\
     * Macroblock type table\
     * mb_type_base + mb_width + 2\
     * - encoding: unused\
     * - decoding: set by lavc\
     */\
    uint32_t *mb_type;\
\
    /**\
     * Macroblock size: (0->16x16, 1->8x8, 2-> 4x4, 3-> 2x2)\
     * - encoding: unused\
     * - decoding: set by lavc\
     */\
    uint8_t motion_subsample_log2;\
\
    /**\
     * for some private data of the user\
     * - encoding: unused\
     * - decoding: set by user\
     */\
    void *opaque;\
\
    /**\
     * error\
     * - encoding: set by lavc if flags&CODEC_FLAG_PSNR\
     * - decoding: unused\
     */\
    uint64_t error[4];\
\
    /**\
     * type of the buffer (to keep track of who has to dealloc data[*])\
     * - encoding: set by the one who allocs it\
     * - decoding: set by the one who allocs it\
     * Note: user allocated (direct rendering) & internal buffers can not coexist currently\
     */\
    int type;\
    \
    /**\
     * when decoding, this signal how much the picture must be delayed.\
     * extra_delay = repeat_pict / (2*fps)\
     * - encoding: unused\
     * - decoding: set by lavc\
     */\
    int repeat_pict;\
    \
    /**\
     * \
     */\
    int qscale_type;\
    \
    /**\
     * The content of the picture is interlaced.\
     * - encoding: set by user\
     * - decoding: set by lavc (default 0)\
     */\
    int interlaced_frame;\
    \
    /**\
     * if the content is interlaced, is top field displayed first.\
     * - encoding: set by user\
     * - decoding: set by lavc\
     */\
    int top_field_first;\
    \
    /**\
     * Pan scan.\
     * - encoding: set by user\
     * - decoding: set by lavc\
     */\
    AVPanScan *pan_scan;\
    \
    /**\
     * tell user application that palette has changed from previous frame.\
     * - encoding: ??? (no palette-enabled encoder yet)\
     * - decoding: set by lavc (default 0)\
     */\
    int palette_has_changed;\
    \
    /**\
     * Codec suggestion on buffer type if != 0\
     * - encoding: unused\
     * - decoding: set by lavc (before get_buffer() call))\
     */\
    int buffer_hints;\


/**
 * Audio Video Frame.
 */
typedef struct AVFrame 
{
    FF_COMMON_FRAME

} AVFrame;

/**
 * main external api structure.
 */
typedef struct AVCodecContext
{
    /**
     * the average bitrate.
     * - encoding: set by user. unused for constant quantizer encoding
     * - decoding: set by lavc. 0 or some bitrate if this info is available in the stream 
     */
    int bit_rate;

    /**
     * number of bits the bitstream is allowed to diverge from the reference.
     *           the reference can be CBR (for CBR pass1) or VBR (for pass2)
     * - encoding: set by user. unused for constant quantizer encoding
     * - decoding: unused
     */
    int bit_rate_tolerance; 

    /**
     * CODEC_FLAG_*.
     * - encoding: set by user.
     * - decoding: set by user.
     */
    int flags;

    /**
     * some codecs needs additionnal format info. It is stored here
     * - encoding: set by user. 
     * - decoding: set by lavc. (FIXME is this ok?)
     */
    int sub_id;

    /**
     * motion estimation algorithm used for video coding.
     * - encoding: MUST be set by user.
     * - decoding: unused
     */
    int me_method;

    /**
     * some codecs need / can use extra-data like huffman tables.
     * mjpeg: huffman tables
     * rv10: additional flags
     * mpeg4: global headers (they can be in the bitstream or here)
     * - encoding: set/allocated/freed by lavc.
     * - decoding: set/allocated/freed by user.
     */
    void *extradata;
    int extradata_size;
    
    /* video only */
    /**
     * frames per sec multiplied by frame_rate_base.
     * for variable fps this is the precission, so if the timestamps 
     * can be specified in msec precssion then this is 1000*frame_rate_base
     * - encoding: MUST be set by user
     * - decoding: set by lavc. 0 or the frame_rate if available
     */
    int frame_rate;
    
    /**
     * width / height.
     * - encoding: MUST be set by user. 
     * - decoding: set by user if known, codec should override / dynamically change if needed
     */
    int width, height;
    
#define FF_ASPECT_SQUARE 1
#define FF_ASPECT_4_3_625 2
#define FF_ASPECT_4_3_525 3
#define FF_ASPECT_16_9_625 4
#define FF_ASPECT_16_9_525 5
#define FF_ASPECT_EXTENDED 15

    /**
     * the number of pictures in a group of pitures, or 0 for intra_only.
     * - encoding: set by user.
     * - decoding: unused
     */
    int gop_size;

    /**
     * pixel format, see PIX_FMT_xxx.
     * - encoding: FIXME: used by ffmpeg to decide whether an pix_fmt
     *                    conversion is in order. This only works for
     *                    codecs with one supported pix_fmt, we should
     *                    do something for a generic case as well.
     * - decoding: set by lavc.
     */
    enum PixelFormatP pix_fmt;
 
    /**
     * Frame rate emulation. If not zero lower layer (i.e. format handler) 
     * has to read frames at native frame rate.
     * - encoding: set by user.
     * - decoding: unused.
     */
    int rate_emu;
       
    /**
     * if non NULL, 'draw_horiz_band' is called by the libavcodec
     * decoder to draw an horizontal band. It improve cache usage. Not
     * all codecs can do that. You must check the codec capabilities
     * before
     * - encoding: unused
     * - decoding: set by user.
     * @param height the height of the slice
     * @param y the y position of the slice
     * @param type 1->top field, 2->bottom field, 3->frame
     * @param offset offset into the AVFrame.data from which the slice should be read
     */
    void (*draw_horiz_band)(struct AVCodecContext *s,
                            const AVFrame *src, int offset[4],
                            int y, int type, int height);

    /* audio only */
    int sample_rate; ///< samples per sec 
    int channels;
    int sample_fmt;  ///< sample format, currenly unused 

    /* the following data should not be initialized */
    int frame_size;     ///< in samples, initialized when calling 'init' 
    int frame_number;   ///< audio or video frame number 
    int real_pict_num;  ///< returns the real picture number of previous encoded frame 
    
    /**
     * number of frames the decoded output will be delayed relative to 
     * the encoded input.
     * - encoding: set by lavc.
     * - decoding: unused
     */
    int delay;
    
    /* - encoding parameters */
    float qcompress;  ///< amount of qscale change between easy & hard scenes (0.0-1.0)
    float qblur;      ///< amount of qscale smoothing over time (0.0-1.0) 
    
    /**
     * minimum quantizer.
     * - encoding: set by user.
     * - decoding: unused
     */
    int qmin;

    /**
     * maximum quantizer.
     * - encoding: set by user.
     * - decoding: unused
     */
    int qmax;

    /**
     * maximum quantizer difference etween frames.
     * - encoding: set by user.
     * - decoding: unused
     */
    int max_qdiff;

    /**
     * maximum number of b frames between non b frames.
     * note: the output will be delayed by max_b_frames+1 relative to the input
     * - encoding: set by user.
     * - decoding: unused
     */
    int max_b_frames;

    /**
     * qscale factor between ip and b frames.
     * - encoding: set by user.
     * - decoding: unused
     */
    float b_quant_factor;
    
    /** obsolete FIXME remove */
    int rc_strategy;
    int b_frame_strategy;

    /**
     * hurry up amount.
     * - encoding: unused
     * - decoding: set by user. 1-> skip b frames, 2-> skip idct/dequant too, 5-> skip everything except header
     */
    int hurry_up;
    
    struct AVCodec *codec;
    
    void *priv_data;

    /* unused, FIXME remove*/
    int rtp_mode;
    
    int rtp_payload_size;   /* The size of the RTP payload, the coder will  */
                            /* do it's best to deliver a chunk with size    */
                            /* below rtp_payload_size, the chunk will start */
                            /* with a start code on some codecs like H.263  */
                            /* This doesn't take account of any particular  */
                            /* headers inside the transmited RTP payload    */

    
    /* The RTP callcack: This function is called  */
    /* every time the encoder as a packet to send */
    /* Depends on the encoder if the data starts  */
    /* with a Start Code (it should) H.263 does   */
    void (*rtp_callback)(void *data, int size, int packet_number); 

    /* statistics, used for 2-pass encoding */
    int mv_bits;
    int header_bits;
    int i_tex_bits;
    int p_tex_bits;
    int i_count;
    int p_count;
    int skip_count;
    int misc_bits;
    
    /**
     * number of bits used for the previously encoded frame.
     * - encoding: set by lavc
     * - decoding: unused
     */
    int frame_bits;

    /**
     * private data of the user, can be used to carry app specific stuff.
     * - encoding: set by user
     * - decoding: set by user
     */
    void *opaque;

    char codec_name[32];
    enum CodecType codec_type; /* see CODEC_TYPE_xxx */
    enum CodecID codec_id; /* see CODEC_ID_xxx */
    
    /**
     * fourcc (LSB first, so "ABCD" -> ('D'<<24) + ('C'<<16) + ('B'<<8) + 'A').
     * this is used to workaround some encoder bugs
     * - encoding: set by user, if not then the default based on codec_id will be used
     * - decoding: set by user, will be converted to upper case by lavc during init
     */
    unsigned int codec_tag;
    
    /**
     * workaround bugs in encoders which sometimes cannot be detected automatically.
     * - encoding: unused
     * - decoding: set by user
     */
    int workaround_bugs;
#define FF_BUG_AUTODETECT       1  ///< autodetection
#define FF_BUG_OLD_MSMPEG4      2
#define FF_BUG_XVID_ILACE       4
#define FF_BUG_UMP4             8
#define FF_BUG_NO_PADDING       16
#define FF_BUG_AC_VLC           0  ///< will be removed, libavcodec can now handle these non compliant files by default
#define FF_BUG_QPEL_CHROMA      64
#define FF_BUG_STD_QPEL         128
#define FF_BUG_QPEL_CHROMA2     256
#define FF_BUG_DIRECT_BLOCKSIZE 512
#define FF_BUG_EDGE             1024
//#define FF_BUG_FAKE_SCALABILITY 16 //autodetection should work 100%
        
    /**
     * luma single coeff elimination threshold.
     * - encoding: set by user
     * - decoding: unused
     */
    int luma_elim_threshold;
    
    /**
     * chroma single coeff elimination threshold.
     * - encoding: set by user
     * - decoding: unused
     */
    int chroma_elim_threshold;
    
    /**
     * strictly follow the std (MPEG4, ...).
     * - encoding: set by user
     * - decoding: unused
     */
    int strict_std_compliance;
    
    /**
     * qscale offset between ip and b frames.
     * if > 0 then the last p frame quantizer will be used (q= lastp_q*factor+offset)
     * if < 0 then normal ratecontrol will be done (q= -normal_q*factor+offset)
     * - encoding: set by user.
     * - decoding: unused
     */
    float b_quant_offset;
    
    /**
     * error resilience higher values will detect more errors but may missdetect
     * some more or less valid parts as errors.
     * - encoding: unused
     * - decoding: set by user
     */
    int error_resilience;
#define FF_ER_CAREFULL        1
#define FF_ER_COMPLIANT       2
#define FF_ER_AGGRESSIVE      3
#define FF_ER_VERY_AGGRESSIVE 4
    
    /**
     * called at the beginning of each frame to get a buffer for it.
     * if pic.reference is set then the frame will be read later by lavc
     * width and height should be rounded up to the next multiple of 16
     * - encoding: unused
     * - decoding: set by lavc, user can override
     */
    int (*get_buffer)(struct AVCodecContext *c, AVFrame *pic);
    
    /**
     * called to release buffers which where allocated with get_buffer.
     * a released buffer can be reused in get_buffer()
     * pic.data[*] must be set to NULL
     * - encoding: unused
     * - decoding: set by lavc, user can override
     */
    void (*release_buffer)(struct AVCodecContext *c, AVFrame *pic);

    /**
     * is 1 if the decoded stream contains b frames, 0 otherwise.
     * - encoding: unused
     * - decoding: set by lavc
     */
    int has_b_frames;
    
    int block_align; ///< used by some WAV based audio codecs
    
    int parse_only; /* - decoding only: if true, only parsing is done
                       (function avcodec_parse_frame()). The frame
                       data is returned. Only MPEG codecs support this now. */
    
    /**
     * 0-> h263 quant 1-> mpeg quant.
     * - encoding: set by user.
     * - decoding: unused
     */
    int mpeg_quant;
    
    /**
     * pass1 encoding statistics output buffer.
     * - encoding: set by lavc
     * - decoding: unused
     */
    char *stats_out;
    
    /**
     * pass2 encoding statistics input buffer.
     * concatenated stuff from stats_out of pass1 should be placed here
     * - encoding: allocated/set/freed by user
     * - decoding: unused
     */
    char *stats_in;
    
    /**
     * ratecontrol qmin qmax limiting method.
     * 0-> clipping, 1-> use a nice continous function to limit qscale wthin qmin/qmax
     * - encoding: set by user.
     * - decoding: unused
     */
    float rc_qsquish;

    float rc_qmod_amp;
    int rc_qmod_freq;
    
    /**
     * ratecontrol override, see RcOverride.
     * - encoding: allocated/set/freed by user.
     * - decoding: unused
     */
    RcOverride *rc_override;
    int rc_override_count;
    
    /**
     * rate control equation.
     * - encoding: set by user
     * - decoding: unused
     */
    char *rc_eq;
    
    /**
     * maximum bitrate.
     * - encoding: set by user.
     * - decoding: unused
     */
    int rc_max_rate;
    
    /**
     * minimum bitrate.
     * - encoding: set by user.
     * - decoding: unused
     */
    int rc_min_rate;
    
    /**
     * decoder bitstream buffer size.
     * - encoding: set by user.
     * - decoding: unused
     */
    int rc_buffer_size;
    float rc_buffer_aggressivity;

    /**
     * qscale factor between p and i frames.
     * if > 0 then the last p frame quantizer will be used (q= lastp_q*factor+offset)
     * if < 0 then normal ratecontrol will be done (q= -normal_q*factor+offset)
     * - encoding: set by user.
     * - decoding: unused
     */
    float i_quant_factor;
    
    /**
     * qscale offset between p and i frames.
     * - encoding: set by user.
     * - decoding: unused
     */
    float i_quant_offset;
    
    /**
     * initial complexity for pass1 ratecontrol.
     * - encoding: set by user.
     * - decoding: unused
     */
    float rc_initial_cplx;

    /**
     * dct algorithm, see FF_DCT_* below.
     * - encoding: set by user
     * - decoding: unused
     */
    int dct_algo;
#define FF_DCT_AUTO    0
#define FF_DCT_FASTINT 1
#define FF_DCT_INT     2
#define FF_DCT_MMX     3
#define FF_DCT_MLIB    4
#define FF_DCT_ALTIVEC 5
#define FF_DCT_FAAN    6
    
    /**
     * luminance masking (0-> disabled).
     * - encoding: set by user
     * - decoding: unused
     */
    float lumi_masking;
    
    /**
     * temporary complexity masking (0-> disabled).
     * - encoding: set by user
     * - decoding: unused
     */
    float temporal_cplx_masking;
    
    /**
     * spatial complexity masking (0-> disabled).
     * - encoding: set by user
     * - decoding: unused
     */
    float spatial_cplx_masking;
    
    /**
     * p block masking (0-> disabled).
     * - encoding: set by user
     * - decoding: unused
     */
    float p_masking;

    /**
     * darkness masking (0-> disabled).
     * - encoding: set by user
     * - decoding: unused
     */
    float dark_masking;
    
    
    /* for binary compatibility */
    int unused;
    
    /**
     * idct algorithm, see FF_IDCT_* below.
     * - encoding: set by user
     * - decoding: set by user
     */
    int idct_algo;
#define FF_IDCT_AUTO         0
#define FF_IDCT_INT          1
#define FF_IDCT_SIMPLE       2
#define FF_IDCT_SIMPLEMMX    3
#define FF_IDCT_LIBMPEG2MMX  4
#define FF_IDCT_PS2          5
#define FF_IDCT_MLIB         6
#define FF_IDCT_ARM          7
#define FF_IDCT_ALTIVEC      8
#define FF_IDCT_SH4          9
#define FF_IDCT_SIMPLEARM    10

    /**
     * slice count.
     * - encoding: set by lavc
     * - decoding: set by user (or 0)
     */
    int slice_count;
    /**
     * slice offsets in the frame in bytes.
     * - encoding: set/allocated by lavc
     * - decoding: set/allocated by user (or NULL)
     */
    int *slice_offset;

    /**
     * error concealment flags.
     * - encoding: unused
     * - decoding: set by user
     */
    int error_concealment;
#define FF_EC_GUESS_MVS   1
#define FF_EC_DEBLOCK     2

    /**
     * dsp_mask could be add used to disable unwanted CPU features
     * CPU features (i.e. MMX, SSE. ...)
     *
     * with FORCE flag you may instead enable given CPU features
     * (Dangerous: usable in case of misdetection, improper usage however will
     * result into program crash)
     */
    unsigned dsp_mask;
#define FF_MM_FORCE	0x80000000 /* force usage of selected flags (OR) */
    /* lower 16 bits - CPU features */

    /**
     * bits per sample/pixel from the demuxer (needed for huffyuv).
     * - encoding: set by lavc
     * - decoding: set by user
     */
     int bits_per_sample;
    
    /**
     * prediction method (needed for huffyuv).
     * - encoding: set by user
     * - decoding: unused
     */
     int prediction_method;
#define FF_PRED_LEFT   0
#define FF_PRED_PLANE  1
#define FF_PRED_MEDIAN 2
    
    /**
     * sample aspect ratio (0 if unknown).
     * numerator and denominator must be relative prime and smaller then 256 for some video standards
     * - encoding: set by user.
     * - decoding: set by lavc.
     */
    //AVRational sample_aspect_ratio;

    /**
     * the picture in the bitstream.
     * - encoding: set by lavc
     * - decoding: set by lavc
     */
    AVFrame *coded_frame;

    /**
     * debug.
     * - encoding: set by user.
     * - decoding: set by user.
     */
    int debug;
#define FF_DEBUG_PICT_INFO 1
#define FF_DEBUG_RC        2
#define FF_DEBUG_BITSTREAM 4
#define FF_DEBUG_MB_TYPE   8
#define FF_DEBUG_QP        16
#define FF_DEBUG_MV        32
//#define FF_DEBUG_VIS_MV    0x00000040
#define FF_DEBUG_SKIP      0x00000080
#define FF_DEBUG_STARTCODE 0x00000100
#define FF_DEBUG_PTS       0x00000200
#define FF_DEBUG_ER        0x00000400
#define FF_DEBUG_MMCO      0x00000800
#define FF_DEBUG_BUGS      0x00001000
#define FF_DEBUG_VIS_QP    0x00002000
#define FF_DEBUG_VIS_MB_TYPE 0x00004000
    
    /**
     * debug.
     * - encoding: set by user.
     * - decoding: set by user.
     */
    int debug_mv;
#define FF_DEBUG_VIS_MV_P_FOR  0x00000001 //visualize forward predicted MVs of P frames
#define FF_DEBUG_VIS_MV_B_FOR  0x00000002 //visualize forward predicted MVs of B frames
#define FF_DEBUG_VIS_MV_B_BACK 0x00000004 //visualize backward predicted MVs of B frames

    /**
     * error.
     * - encoding: set by lavc if flags&CODEC_FLAG_PSNR
     * - decoding: unused
     */
    uint64_t error[4];
    
    /**
     * minimum MB quantizer.
     * - encoding: set by user.
     * - decoding: unused
     */
    int mb_qmin;

    /**
     * maximum MB quantizer.
     * - encoding: set by user.
     * - decoding: unused
     */
    int mb_qmax;
    
    /**
     * motion estimation compare function.
     * - encoding: set by user.
     * - decoding: unused
     */
    int me_cmp;
    /**
     * subpixel motion estimation compare function.
     * - encoding: set by user.
     * - decoding: unused
     */
    int me_sub_cmp;
    /**
     * macroblock compare function (not supported yet).
     * - encoding: set by user.
     * - decoding: unused
     */
    int mb_cmp;
    /**
     * interlaced dct compare function
     * - encoding: set by user.
     * - decoding: unused
     */
    int ildct_cmp;
#define FF_CMP_SAD  0
#define FF_CMP_SSE  1
#define FF_CMP_SATD 2
#define FF_CMP_DCT  3
#define FF_CMP_PSNR 4
#define FF_CMP_BIT  5
#define FF_CMP_RD   6
#define FF_CMP_ZERO 7
#define FF_CMP_VSAD 8
#define FF_CMP_VSSE 9
#define FF_CMP_CHROMA 256
    
    /**
     * ME diamond size & shape.
     * - encoding: set by user.
     * - decoding: unused
     */
    int dia_size;

    /**
     * amount of previous MV predictors (2a+1 x 2a+1 square).
     * - encoding: set by user.
     * - decoding: unused
     */
    int last_predictor_count;

    /**
     * pre pass for motion estimation.
     * - encoding: set by user.
     * - decoding: unused
     */
    int pre_me;

    /**
     * motion estimation pre pass compare function.
     * - encoding: set by user.
     * - decoding: unused
     */
    int me_pre_cmp;

    /**
     * ME pre pass diamond size & shape.
     * - encoding: set by user.
     * - decoding: unused
     */
    int pre_dia_size;

    /**
     * subpel ME quality.
     * - encoding: set by user.
     * - decoding: unused
     */
    int me_subpel_quality;

    /**
     * callback to negotiate the pixelFormat.
     * @param fmt is the list of formats which are supported by the codec,
     * its terminated by -1 as 0 is a valid format, the formats are ordered by quality
     * the first is allways the native one
     * @return the choosen format
     * - encoding: unused
     * - decoding: set by user, if not set then the native format will always be choosen
     */
    enum PixelFormatP (*get_format)(struct AVCodecContext *s, enum PixelFormatP * fmt);

    /**
     * DTG active format information (additionnal aspect ratio
     * information only used in DVB MPEG2 transport streams). 0 if
     * not set.
     * 
     * - encoding: unused.
     * - decoding: set by decoder 
     */
    int dtg_active_format;
#define FF_DTG_AFD_SAME         8
#define FF_DTG_AFD_4_3          9
#define FF_DTG_AFD_16_9         10
#define FF_DTG_AFD_14_9         11
#define FF_DTG_AFD_4_3_SP_14_9  13
#define FF_DTG_AFD_16_9_SP_14_9 14
#define FF_DTG_AFD_SP_4_3       15

    /**
     * Maximum motion estimation search range in subpel units.
     * if 0 then no limit
     * 
     * - encoding: set by user.
     * - decoding: unused.
     */
    int me_range;

    /**
     * frame_rate_base.
     * for variable fps this is 1
     * - encoding: set by user.
     * - decoding: set by lavc.
     * @todo move this after frame_rate
     */

    int frame_rate_base;
    /**
     * intra quantizer bias.
     * - encoding: set by user.
     * - decoding: unused
     */
    int intra_quant_bias;
#define FF_DEFAULT_QUANT_BIAS 999999
    
    /**
     * inter quantizer bias.
     * - encoding: set by user.
     * - decoding: unused
     */
    int inter_quant_bias;

    /**
     * color table ID.
     * - encoding: unused.
     * - decoding: which clrtable should be used for 8bit RGB images
     *             table have to be stored somewhere FIXME
     */
    int color_table_id;
    
    /**
     * internal_buffer count. 
     * Dont touch, used by lavc default_get_buffer()
     */
    int internal_buffer_count;
    
    /**
     * internal_buffers. 
     * Dont touch, used by lavc default_get_buffer()
     */
    void *internal_buffer;

#define FF_LAMBDA_SHIFT 7
#define FF_LAMBDA_SCALE (1<<FF_LAMBDA_SHIFT)
#define FF_QP2LAMBDA 118 ///< factor to convert from H.263 QP to lambda
#define FF_LAMBDA_MAX (256*128-1)

#define FF_QUALITY_SCALE FF_LAMBDA_SCALE //FIXME maybe remove
    /**
     * global quality for codecs which cannot change it per frame.
     * this should be proportional to MPEG1/2/4 qscale.
     * - encoding: set by user.
     * - decoding: unused
     */
    int global_quality;
    
#define FF_CODER_TYPE_VLC   0
#define FF_CODER_TYPE_AC    1
    /**
     * coder type
     * - encoding: set by user.
     * - decoding: unused
     */
    int coder_type;

    /**
     * context model
     * - encoding: set by user.
     * - decoding: unused
     */
    int context_model;
    
    /**
     * slice flags
     * - encoding: unused
     * - decoding: set by user.
     */
    int slice_flags;
#define SLICE_FLAG_CODED_ORDER    0x0001 ///< draw_horiz_band() is called in coded order instead of display
#define SLICE_FLAG_ALLOW_FIELD    0x0002 ///< allow draw_horiz_band() with field slices (MPEG2 field pics)
#define SLICE_FLAG_ALLOW_PLANE    0x0004 ///< allow draw_horiz_band() with 1 component at a time (SVQ1)

    /**
     * XVideo Motion Acceleration
     * - encoding: forbidden
     * - decoding: set by decoder
     */
    int xvmc_acceleration;
    
    /**
     * macroblock decision mode
     * - encoding: set by user.
     * - decoding: unused
     */
    int mb_decision;
#define FF_MB_DECISION_SIMPLE 0        ///< uses mb_cmp
#define FF_MB_DECISION_BITS   1        ///< chooses the one which needs the fewest bits
#define FF_MB_DECISION_RD     2        ///< rate distoration

    /**
     * custom intra quantization matrix
     * - encoding: set by user, can be NULL
     * - decoding: set by lavc
     */
    uint16_t *intra_matrix;

    /**
     * custom inter quantization matrix
     * - encoding: set by user, can be NULL
     * - decoding: set by lavc
     */
    uint16_t *inter_matrix;
    
    /**
     * fourcc from the AVI stream header (LSB first, so "ABCD" -> ('D'<<24) + ('C'<<16) + ('B'<<8) + 'A').
     * this is used to workaround some encoder bugs
     * - encoding: unused
     * - decoding: set by user, will be converted to upper case by lavc during init
     */
    unsigned int stream_codec_tag;

    /**
     * scene change detection threshold.
     * 0 is default, larger means fewer detected scene changes
     * - encoding: set by user.
     * - decoding: unused
     */
    int scenechange_threshold;

    /**
     * minimum lagrange multipler
     * - encoding: set by user.
     * - decoding: unused
     */
    int lmin;

    /**
     * maximum lagrange multipler
     * - encoding: set by user.
     * - decoding: unused
     */
    int lmax;

    /**
     * Palette control structure
     * - encoding: ??? (no palette-enabled encoder yet)
     * - decoding: set by user.
     */
    struct AVPaletteControl *palctrl;

    /**
     * noise reduction strength
     * - encoding: set by user.
     * - decoding: unused
     */
    int noise_reduction;
    
    /**
     * called at the beginning of a frame to get cr buffer for it.
     * buffer type (size, hints) must be the same. lavc won't check it.
     * lavc will pass previous buffer in pic, function should return
     * same buffer or new buffer with old frame "painted" into it.
     * if pic.data[0] == NULL must behave like get_buffer().
     * - encoding: unused
     * - decoding: set by lavc, user can override
     */
    int (*reget_buffer)(struct AVCodecContext *c, AVFrame *pic);

    /**
     * number of bits which should be loaded into the rc buffer before decoding starts
     * - encoding: set by user.
     * - decoding: unused
     */
    int rc_initial_buffer_occupancy;

    /**
     *
     * - encoding: set by user.
     * - decoding: unused
     */
    int inter_threshold;

    /**
     * CODEC_FLAG2_*.
     * - encoding: set by user.
     * - decoding: set by user.
     */
    int flags2;

    /**
     * simulates errors in the bitstream to test error concealment.
     * - encoding: set by user.
     * - decoding: unused.
     */
    int error_rate;
    
    /**
     * MP3 antialias algorithm, see FF_AA_* below.
     * - encoding: unused
     * - decoding: set by user
     */
    int antialias_algo;
#define FF_AA_AUTO    0
#define FF_AA_FASTINT 1 //not implemented yet
#define FF_AA_INT     2
#define FF_AA_FLOAT   3
    /**
     * Quantizer noise shaping.
     * - encoding: set by user
     * - decoding: unused
     */
    int quantizer_noise_shaping;
} AVCodecContext;

/* frame parsing */
typedef struct AVCodecParserContext 
{
    void *priv_data;
    struct AVCodecParser *parser;
    int64_t frame_offset; /* offset of the current frame */
    int64_t cur_offset; /* current offset 
                           (incremented by each av_parser_parse()) */
    int64_t last_frame_offset; /* offset of the last frame */
    /* video info */
    int pict_type; /* XXX: put it back in AVCodecContext */
    int repeat_pict; /* XXX: put it back in AVCodecContext */
    int64_t pts;     /* pts of the current frame */
    int64_t dts;     /* dts of the current frame */

    /* private data */
    int64_t last_pts;
    int64_t last_dts;

#define AV_PARSER_PTS_NB 4
    int cur_frame_start_index;
    int64_t cur_frame_offset[AV_PARSER_PTS_NB];
    int64_t cur_frame_pts[AV_PARSER_PTS_NB];
    int64_t cur_frame_dts[AV_PARSER_PTS_NB];
} AVCodecParserContext;

typedef struct 
{
    unsigned char *buffer;
    int buffer_size;
    unsigned char *buf_ptr, *buf_end;
    void *opaque;
    int (*read_packet)(void *opaque, uint8_t *buf, int buf_size);
    void (*write_packet)(void *opaque, uint8_t *buf, int buf_size);
    int (*seek)(void *opaque, offset_t offset, int whence);
    offset_t pos; /* position in the file of the current buffer */
    int must_flush; /* true if the next seek should flush */
    int eof_reached; /* true if eof reached */
    int write_flag;  /* true if open for writing */
    int is_streamed;
    int max_packet_size;
} ByteIOContext;

typedef struct AVPicture 
{
    uint8_t *data[4];
    int linesize[4];       ///< number of bytes per line
} AVPicture;


//-----------------------------------------------------------------
/*
#ifndef  HAVE_AV_CONFIG_H
#define HAVE_AV_CONFIG_H
#endif
*/

#define LIBAVFORMAT_BUILD       4611
#define LIBAVFORMAT_VERSION_INT FFMPEG_VERSION_INT
#define LIBAVFORMAT_VERSION     FFMPEG_VERSION
#define LIBAVFORMAT_IDENT	"FFmpeg" FFMPEG_VERSION "b" AV_STRINGIFY(LIBAVFORMAT_BUILD)

#include <time.h>
#include <stdio.h>  /* FILE */
#include "avcodec.h"

//#include "avio.h"
#if defined(WIN32) && !defined(__MINGW32__) && !defined(__CYGWIN__)
#    define CONFIG_WIN32
#endif

#ifdef CONFIG_WIN32
/* windows */

#ifndef __MINGW32__
#define int64_t_C(c)     (c ## i64)
#define uint64_t_C(c)    (c ## i64)

#ifdef HAVE_AV_CONFIG_H
#define inline __inline
#endif

#    else
#        define int64_t_C(c)     (c ## LL)
#        define uint64_t_C(c)    (c ## ULL)
#    endif /* __MINGW32__ */

#    ifdef HAVE_AV_CONFIG_H
#        ifdef _DEBUG
#            define DEBUG
#        endif

#        define snprintf _snprintf
#        define vsnprintf _vsnprintf
#    endif

/* CONFIG_WIN32 end */
#elif defined (CONFIG_OS2)
/* OS/2 EMX */

#ifndef int64_t_C
#define int64_t_C(c)     (c ## LL)
#define uint64_t_C(c)    (c ## ULL)
#endif

#ifdef HAVE_AV_CONFIG_H

#ifdef USE_FASTMEMCPY
#include "fastmemcpy.h"
#endif

#include <float.h>

#endif /* HAVE_AV_CONFIG_H */

/* CONFIG_OS2 end */
#else

/* unix */

#ifndef int64_t_C
#define int64_t_C(c)     (c ## LL)
#define uint64_t_C(c)    (c ## ULL)
#endif

#ifdef HAVE_AV_CONFIG_H

#        ifdef USE_FASTMEMCPY
#            include "fastmemcpy.h"
#        endif
#    endif /* HAVE_AV_CONFIG_H */

#endif /* !CONFIG_WIN32 && !CONFIG_OS2 */

/* packet functions */

#ifndef MAXINT64
#define MAXINT64 int64_t_C(0x7fffffffffffffff)
#endif

#ifndef MININT64
#define MININT64 int64_t_C(0x8000000000000000)
#endif

#define AV_NOPTS_VALUE MININT64
#define AV_TIME_BASE 1000000

#define DURATION_MAX_READ_SIZE 250000
/* absolute maximum size we read until we abort */
#define MAX_READ_SIZE        5000000
/* maximum duration until we stop analysing the stream */
#define MAX_STREAM_DURATION  ((int)(AV_TIME_BASE * 1.0))
#define PROBE_BUF_SIZE 2048
#define IO_BUFFER_SIZE 32768
#define	url_write_packet NULL



typedef struct AVPacket 
{
    int64_t pts; /* presentation time stamp in AV_TIME_BASE units (or
                    pts_den units in muxers or demuxers) */
    int64_t dts; /* decompression time stamp in AV_TIME_BASE units (or
                    pts_den units in muxers or demuxers) */
    uint8_t *data;
    int   size;
    int   stream_index;
    int   flags;
    int   duration; /* presentation duration (0 if not available) */
    void  (*destruct)(struct AVPacket *);
    void  *priv;
} AVPacket; 
#define PKT_FLAG_KEY   0x0001

/* initialize optional fields of a packet */
//static inline void av_init_packet(AVPacket *pkt) //jacky 2006/10/18
#if 0
int av_new_packet(AVPacket *pkt, int size);
int av_dup_packet(AVPacket *pkt);

/**
 * Free a packet
 *
 * @param pkt packet to free
 */
//static inline void av_free_packet(AVPacket *pkt)
#endif

/*************************************************/
/* fractional numbers for exact pts handling */

/* the exact value of the fractional number is: 'val + num / den'. num
   is assumed to be such as 0 <= num < den */
typedef struct AVFrac 
{
    int64_t val, num, den; 
} AVFrac;

void av_frac_init(AVFrac *f, int64_t val, int64_t num, int64_t den);
void av_frac_add(AVFrac *f, int64_t incr);
void av_frac_set(AVFrac *f, int64_t val);

/*************************************************/
/* input/output formats */

struct AVFormatContext;

/* this structure contains the data a format has to probe a file */
typedef struct AVProbeData
{
    const char *filename;
    unsigned char *buf;
    int buf_size;
} AVProbeData;

#define AVPROBE_SCORE_MAX 100

typedef struct AVFormatParameters 
{
    int frame_rate;
    int frame_rate_base;
    int sample_rate;
    int channels;
    int width;
    int height;
    enum PixelFormatP pix_fmt;
    struct AVImageFormat *image_format;
    int channel; /* used to select dv channel */
    const char *device; /* video4linux, audio or DV device */
    const char *standard; /* tv standard, NTSC, PAL, SECAM */
    int mpeg2ts_raw:1;  /* force raw MPEG2 transport stream output, if possible */
    int mpeg2ts_compute_pcr:1; /* compute exact PCR for each transport
                                  stream packet (only meaningful if
                                  mpeg2ts_raw is TRUE */
    int initial_pause:1;       /* do not begin to play the stream
                                  immediately (RTSP only) */
} AVFormatParameters;

#define AVFMT_NOFILE        0x0001 /* no file should be opened */
#define AVFMT_NEEDNUMBER    0x0002 /* needs '%d' in filename */ 
#define AVFMT_SHOW_IDS      0x0008 /* show format stream IDs numbers */
#define AVFMT_RAWPICTURE    0x0020 /* format wants AVPicture structure for
                                      raw picture data */

typedef struct AVOutputFormat
{
    const char *name;
    const char *long_name;
    const char *mime_type;
    const char *extensions; /* comma separated extensions */
    /* size of private data so that it can be allocated in the wrapper */
    int priv_data_size;
    /* output support */
    enum CodecID audio_codec; /* default audio codec */
    enum CodecID video_codec; /* default video codec */
    int (*write_header)(struct AVFormatContext *);
    int (*write_packet)(struct AVFormatContext *, 
                        int stream_index,
                        const uint8_t *buf, int size, int64_t pts);
    int (*write_trailer)(struct AVFormatContext *);
    /* can use flags: AVFMT_NOFILE, AVFMT_NEEDNUMBER */
    int flags;
    /* currently only used to set pixel format if not YUV420P */
    int (*set_parameters)(struct AVFormatContext *, AVFormatParameters *);
    /* private fields */
    struct AVOutputFormat *next;
} AVOutputFormat;

typedef struct AVInputFormat 
{
    const char *name;
    const char *long_name;
    /* size of private data so that it can be allocated in the wrapper */
    int priv_data_size;
    /* tell if a given file has a chance of being parsing by this format */
    int (*read_probe)(AVProbeData *);
    /* read the format header and initialize the AVFormatContext
       structure. Return 0 if OK. 'ap' if non NULL contains
       additionnal paramters. Only used in raw format right
       now. 'av_new_stream' should be called to create new streams.  */
    int (*read_header)(struct AVFormatContext *,
                       AVFormatParameters *ap);
    /* read one packet and put it in 'pkt'. pts and flags are also
       set. 'av_new_stream' can be called only if the flag
       AVFMTCTX_NOHEADER is used. */
    int (*read_packet)(struct AVFormatContext *, AVPacket *pkt);
    /* close the stream. The AVFormatContext and AVStreams are not
       freed by this function */
    int (*read_close)(struct AVFormatContext *);
    /* seek at or before a given timestamp (given in AV_TIME_BASE
       units) relative to the frames in stream component stream_index */
    int (*read_seek)(struct AVFormatContext *, 
                     int stream_index, int64_t timestamp);
    /* can use flags: AVFMT_NOFILE, AVFMT_NEEDNUMBER */
    int flags;
    /* if extensions are defined, then no probe is done. You should
       usually not use extension format guessing because it is not
       reliable enough */
    const char *extensions;
    /* general purpose read only value that the format can use */
    int value;

    /* start/resume playing - only meaningful if using a network based format
       (RTSP) */
    int (*read_play)(struct AVFormatContext *);

    /* pause playing - only meaningful if using a network based format
       (RTSP) */
    int (*read_pause)(struct AVFormatContext *);

    /* private fields */
    struct AVInputFormat *next;
} AVInputFormat;

typedef struct AVIndexEntry 
{
    int64_t pos;
    int64_t timestamp;
#define AVINDEX_KEYFRAME 0x0001
/* the following 2 flags indicate that the next/prev keyframe is known, and scaning for it isnt needed */
    int flags;
    int min_distance;         /* min distance between this and the previous keyframe, used to avoid unneeded searching */
} AVIndexEntry;

typedef struct AVStream 
{
    int index;    /* stream index in AVFormatContext */
    int id;       /* format specific stream id */
    AVCodecContext codec; /* codec context */
    int r_frame_rate;     /* real frame rate of the stream */
    int r_frame_rate_base;/* real frame rate base of the stream */
    void *priv_data;
    /* internal data used in av_find_stream_info() */
    int64_t codec_info_duration;     
    int codec_info_nb_frames;
    /* encoding: PTS generation when outputing stream */
    AVFrac pts;
    /* ffmpeg.c private use */
    int stream_copy; /* if TRUE, just copy stream */
    /* quality, as it has been removed from AVCodecContext and put in AVVideoFrame
     * MN:dunno if thats the right place, for it */
    float quality; 
    /* decoding: position of the first frame of the component, in
       AV_TIME_BASE fractional seconds. */
    int64_t start_time; 
    /* decoding: duration of the stream, in AV_TIME_BASE fractional
       seconds. */
    int64_t duration;

    /* av_read_frame() support */
    int need_parsing;
    struct AVCodecParserContext *parser;

    int64_t cur_dts;
    int last_IP_duration;
    /* av_seek_frame() support */
    AVIndexEntry *index_entries; /* only used if the format does not
                                    support seeking natively */
    int nb_index_entries;
    int index_entries_allocated_size;
} AVStream;

typedef struct InternalBuffer{
    int last_pic_num;
    uint8_t *base[4];
    uint8_t *data[4];
    int linesize[4];
}InternalBuffer;

#define AVFMTCTX_NOHEADER      0x0001 /* signal that no header is present
                                         (streams are added dynamically) */

#define MAX_STREAMS 20

/* format I/O context */
typedef struct AVFormatContext
{
    /* can only be iformat or oformat, not both at the same time */
    struct AVInputFormat *iformat;
    struct AVOutputFormat *oformat;
    void *priv_data;
    ByteIOContext pb;
    int nb_streams;
    AVStream *streams[MAX_STREAMS];
    char filename[1024]; /* input or output filename */
    /* stream info */
    char title[512];
    char author[512];
    char copyright[512];
    char comment[512];
    char album[512];
    int year;  /* ID3 year, 0 if none */
    int track; /* track number, 0 if none */
    char genre[32]; /* ID3 genre */
    
    int ctx_flags; /* format specific flags, see AVFMTCTX_xx */
    /* private data for pts handling (do not modify directly) */
    int pts_wrap_bits; /* number of bits in pts (used for wrapping control) */
    int pts_num, pts_den; /* value to convert to seconds */
    /* This buffer is only needed when packets were already buffered but
       not decoded, for example to get the codec parameters in mpeg
       streams */
    struct AVPacketList *packet_buffer;

    /* decoding: position of the first frame of the component, in
       AV_TIME_BASE fractional seconds. NEVER set this value directly:
       it is deduced from the AVStream values.  */
    int64_t start_time; 
    /* decoding: duration of the stream, in AV_TIME_BASE fractional
       seconds. NEVER set this value directly: it is deduced from the
       AVStream values.  */
    int64_t duration;
    /* decoding: total file size. 0 if unknown */
    int64_t file_size;
    /* decoding: total stream bitrate in bit/s, 0 if not
       available. Never set it directly if the file_size and the
       duration are known as ffmpeg can compute it automatically. */
    int bit_rate;

    /* av_read_frame() support */
    AVStream *cur_st;
    const uint8_t *cur_ptr;
    int cur_len;
    AVPacket cur_pkt;

    /* the following are used for pts/dts unit conversion */
    int64_t last_pkt_stream_pts;
    int64_t last_pkt_stream_dts;
    int64_t last_pkt_pts;
    int64_t last_pkt_dts;
    int last_pkt_pts_frac;
    int last_pkt_dts_frac;

    /* av_seek_frame() support */
    int64_t data_offset; /* offset of the first packet */
    int index_built;
} AVFormatContext;

typedef struct AVPacketList 
{
    AVPacket pkt;
    struct AVPacketList *next;
} AVPacketList;

//extern AVInputFormat *first_iformat;
//extern AVOutputFormat *first_oformat;

/* still image support */
struct AVInputImageContext;
typedef struct AVInputImageContext AVInputImageContext;

typedef struct AVImageInfo 
{
    enum PixelFormatP pix_fmt; /* requested pixel format */
    int width; /* requested width */
    int height; /* requested height */
    int interleaved; /* image is interleaved (e.g. interleaved GIF) */
    AVPicture pict; /* returned allocated image */
} AVImageInfo;

/* AVImageFormat.flags field constants */
#define AVIMAGE_INTERLEAVED 0x0001 /* image format support interleaved output */

typedef struct AVImageFormat 
{
    const char *name;
    const char *extensions;
    /* tell if a given file has a chance of being parsing by this format */
    int (*img_probe)(AVProbeData *);
    /* read a whole image. 'alloc_cb' is called when the image size is
       known so that the caller can allocate the image. If 'allo_cb'
       returns non zero, then the parsing is aborted. Return '0' if
       OK. */
    int (*img_read)(ByteIOContext *, 
                    int (*alloc_cb)(void *, AVImageInfo *info), void *);
    /* write the image */
    int supported_pixel_formats; /* mask of supported formats for output */
    int (*img_write)(ByteIOContext *, AVImageInfo *);
    int flags;
    struct AVImageFormat *next;
} AVImageFormat;

#define AVERROR_UNKNOWN     (-1)  /* unknown error */
#define AVERROR_IO          (-2)  /* i/o error */
#define AVERROR_NUMEXPECTED (-3)  /* number syntax expected in filename */
#define AVERROR_INVALIDDATA (-4)  /* invalid data found */
#define AVERROR_NOMEM       (-5)  /* not enough memory */
#define AVERROR_NOFMT       (-6)  /* unknown format */
#define AVERROR_NOTSUPP     (-7)  /* operation not supported */

/* XXX: use automatic init with either ELF sections or C file parser */
/* modules */
#if 0
/* mpeg.c */
extern AVInputFormat mpegps_demux;

/* asf.c */
int asf_init(void);
/* rtsp.c */
extern AVInputFormat redir_demux;
int redir_open(AVFormatContext **ic_ptr, ByteIOContext *f);

/* utils.c */
void av_register_input_format(AVInputFormat *format);
AVOutputFormat *guess_stream_format(const char *short_name, 
                                    const char *filename, const char *mime_type);
//AVOutputFormat *guess_format(const char *short_name, 
//                             const char *filename, const char *mime_type);


void av_register_all(void);



/* media file input */
AVInputFormat *av_find_input_format(const char *short_name);
//AVInputFormat *av_probe_input_format(AVProbeData *pd, int is_opened);
int av_open_input_stream(AVFormatContext **ic_ptr, 
                         ByteIOContext *pb, const char *filename, 
                         AVInputFormat *fmt, AVFormatParameters *ap);

int av_open_input_file(AVFormatContext **ic_ptr, const char *filename, 
                       AVInputFormat *fmt,
                       int buf_size,
                       AVFormatParameters *ap);


 
int av_find_stream_info(AVFormatContext *ic);
int av_read_packet(AVFormatContext *s, AVPacket *pkt);
int av_read_frame(AVFormatContext *s, AVPacket *pkt);
int av_seek_frame(AVFormatContext *s, int stream_index, int64_t timestamp);
int av_read_play(AVFormatContext *s);
int av_read_pause(AVFormatContext *s);
void av_close_input_file(AVFormatContext *s);
AVStream *av_new_stream(AVFormatContext *s, int id);
void av_set_pts_info(AVFormatContext *s, int pts_wrap_bits,
                     int pts_num, int pts_den);

int av_find_default_stream_index(AVFormatContext *s);
int av_index_search_timestamp(AVStream *st, int timestamp);
int av_add_index_entry(AVStream *st,
                       int64_t pos, int64_t timestamp, int distance, int flags);

/* media file output */
int av_set_parameters(AVFormatContext *s, AVFormatParameters *ap);




void dump_format(AVFormatContext *ic,
                 int index, 
                 const char *url,
                 int is_output);

int parse_frame_rate(int *frame_rate, int *frame_rate_base, const char *arg);
int64_t parse_date(const char *datestr, int duration);

int64_t av_gettime(void);

/* ffm specific for ffserver */


int get_frame_filename(char *buf, int buf_size,
                       const char *path, int number);
int filename_number_test(const char *filename);

/* grab specific */

int audio_init(void);
#endif

/**
 * AVCodec.
 */
typedef struct AVCodec
{
    const char *name;
    enum CodecType type;
    int id;
    int priv_data_size;
    int (*init)(AVCodecContext *);
    int (*encode)(AVCodecContext *, uint8_t *buf, int buf_size, void *data);
    int (*close)(AVCodecContext *);
    int (*decode)(AVCodecContext *, void *outdata, int *outdata_size,
                  uint8_t *buf, int buf_size);
    int capabilities;
    const AVOption *options;
    struct AVCodec *next;
    void (*flush)(AVCodecContext *);
} AVCodec;


#if 0
extern AVCodec wmav1_decoder;
extern AVCodec wmav2_decoder;
extern AVCodec wmapro_decoder;
extern AVCodec *first_avcodec;
void register_avcodec(AVCodec *format);
AVCodec *avcodec_find_decoder(enum CodecID id);
AVCodec *avcodec_find_decoder_by_name(const char *name);
int avcodec_open(AVCodecContext *avctx, AVCodec *codec);
void avcodec_string(char *buf, int buf_size, AVCodecContext *enc, int encode);
void avcodec_get_context_defaults(AVCodecContext *s);
AVCodecContext *avcodec_alloc_context(void);
AVFrame *avcodec_alloc_frame(void);


void avcodec_default_release_buffer(AVCodecContext *s, AVFrame *pic);
void avcodec_default_free_buffers(AVCodecContext *s);
/**
 * opens / inits the AVCodecContext.
 * not thread save!
 */

int avcodec_decode_audio(AVCodecContext *avctx, int16_t *samples, 
                         int *frame_size_ptr,
                         uint8_t *buf, int buf_size);

int avcodec_parse_frame(AVCodecContext *avctx, uint8_t **pdata, 
                        int *data_size_ptr,
                        uint8_t *buf, int buf_size);

int avcodec_close(AVCodecContext *avctx);

void avcodec_register_all(void);

void avcodec_flush_buffers(AVCodecContext *avctx);
#endif
typedef struct AVCodecParser 
{
    int codec_ids[3]; /* several codec IDs are permitted */
    int priv_data_size;
    int (*parser_init)(AVCodecParserContext *s);
    int (*parser_parse)(AVCodecParserContext *s, 
                        AVCodecContext *avctx,
                        uint8_t **poutbuf, int *poutbuf_size, 
                        const uint8_t *buf, int buf_size);
    void (*parser_close)(AVCodecParserContext *s);
    struct AVCodecParser *next;
} AVCodecParser;
#if 0
extern AVCodecParser *av_first_parser;

//void av_register_codec_parser(AVCodecParser *parser);
AVCodecParserContext *av_parser_init(int codec_id);
int av_parser_parse(AVCodecParserContext *s, 
                    AVCodecContext *avctx,
                    uint8_t **poutbuf, int *poutbuf_size, 
                    const uint8_t *buf, int buf_size,
                    int64_t pts, int64_t dts);
void av_parser_close(AVCodecParserContext *s);
extern void av_log(AVCodecContext*, int level, const char *fmt, ...);// __attribute__ ((__format__ (__printf__, 3, 4)));//jacky 2006/10/18
extern void av_vlog(AVCodecContext*, int level, const char *fmt, va_list);
extern int av_log_get_level(void);
extern void av_log_set_level(int);
extern void av_log_set_callback(void (*)(AVCodecContext*, int, const char*, va_list));
#endif
//#define DEBUG
/* dct code */
typedef short DCTELEM;
/**
 * DSPContext.
 */
typedef struct DSPContext 
{
    /* pixel ops : interface with DCT */
    void (*get_pixels)(DCTELEM *block/*align 16*/, const uint8_t *pixels/*align 8*/, int line_size);
    void (*diff_pixels)(DCTELEM *block/*align 16*/, const uint8_t *s1/*align 8*/, const uint8_t *s2/*align 8*/, int stride);
    void (*put_pixels_clamped)(const DCTELEM *block/*align 16*/, uint8_t *pixels/*align 8*/, int line_size);
    void (*add_pixels_clamped)(const DCTELEM *block/*align 16*/, uint8_t *pixels/*align 8*/, int line_size);
    /**
     * translational global motion compensation.
     */
    void (*gmc1)(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int srcStride, int h, int x16, int y16, int rounder);
    /**
     * global motion compensation.
     */
    void (*gmc )(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int stride, int h, int ox, int oy,
		    int dxx, int dxy, int dyx, int dyy, int shift, int r, int width, int height);
    void (*clear_blocks)(DCTELEM *blocks/*align 16*/);
    int (*pix_sum)(uint8_t * pix, int line_size);
    int (*pix_norm1)(uint8_t * pix, int line_size);
// 16x16 8x8 4x4 2x2 16x8 8x4 4x2 8x16 4x8 2x4
    
  
  
  

    /* (I)DCT */
    void (*fdct)(DCTELEM *block/* align 16*/);
    void (*fdct248)(DCTELEM *block/* align 16*/);
    
    /* IDCT really*/
    void (*idct)(DCTELEM *block/* align 16*/);
    
    /**
     * block -> idct -> clip to unsigned 8 bit -> dest.
     * (-1392, 0, 0, ...) -> idct -> (-174, -174, ...) -> put -> (0, 0, ...)
     * @param line_size size in bytes of a horizotal line of dest
     */
    void (*idct_put)(uint8_t *dest/*align 8*/, int line_size, DCTELEM *block/*align 16*/);
    
    /**
     * block -> idct -> add dest -> clip to unsigned 8 bit -> dest.
     * @param line_size size in bytes of a horizotal line of dest
     */
    void (*idct_add)(uint8_t *dest/*align 8*/, int line_size, DCTELEM *block/*align 16*/);
    
    /**
     * idct input permutation.
     * several optimized IDCTs need a permutated input (relative to the normal order of the reference
     * IDCT)
     * this permutation must be performed before the idct_put/add, note, normally this can be merged
     * with the zigzag/alternate scan<br>
     * an example to avoid confusion:
     * - (->decode coeffs -> zigzag reorder -> dequant -> reference idct ->...)
     * - (x -> referece dct -> reference idct -> x)
     * - (x -> referece dct -> simple_mmx_perm = idct_permutation -> simple_idct_mmx -> x)
     * - (->decode coeffs -> zigzag reorder -> simple_mmx_perm -> dequant -> simple_idct_mmx ->...)
     */
    uint8_t idct_permutation[64];
    int idct_permutation_type;
#define FF_NO_IDCT_PERM 1
#define FF_LIBMPEG2_IDCT_PERM 2
#define FF_SIMPLE_IDCT_PERM 3
#define FF_TRANSPOSE_IDCT_PERM 4

    int (*try_8x8basis)(int16_t rem[64], int16_t weight[64], int16_t basis[64], int scale);
    void (*add_8x8basis)(int16_t rem[64], int16_t basis[64], int scale);
#define BASIS_SHIFT 16
#define RECON_SHIFT 6

} DSPContext;
#if 0
void dsputil_static_init(void);
void dsputil_init(DSPContext* p, AVCodecContext *avctx);

/* motion estimation */
// h is limited to {width/2, width, 2*width} but never larger than 16 and never smaller then 2
// allthough currently h<4 is not used as functions with width <8 are not used and neither implemented
typedef int (*me_cmp_func)(void /*MpegEncContext*/ *s, uint8_t *blk1/*align width (8 or 16)*/, uint8_t *blk2/*align 1*/, int line_size, int h)/* __attribute__ ((const))*/;

/**
 * permute block according to permuatation.
 * @param last last non zero element in scantable order
 */
void ff_block_permute(DCTELEM *block, uint8_t *permutation, const uint8_t *scantable, int last);

void ff_set_cmp(DSPContext* c, me_cmp_func *cmp, int type);

int asf_probe(AVProbeData *pd);
int asf_read_header(AVFormatContext *s, AVFormatParameters *ap);

int asf_read_packet(AVFormatContext *s, AVPacket *pkt);
int asf_read_close(AVFormatContext *s);
int asf_read_seek(AVFormatContext *s, int stream_index, int64_t pts);

#endif
#define MPA_FRAME_SIZE 1152
#define PACKET_SIZE 3200
#define PACKET_HEADER_SIZE 12
#define FRAME_HEADER_SIZE 17

typedef struct 
{
    int num;
    int seq;
    /* use for reading */
    AVPacket pkt;
    int frag_offset;
    int timestamp;
    int64_t duration;

    int ds_span;		/* descrambling  */
    int ds_packet_size;
    int ds_chunk_size;
    int ds_data_size;
    int ds_silence_data;
    
    int packet_pos;

} ASFStream;

typedef struct 
{
    uint32_t v1;
    uint16_t v2;
    uint16_t v3;
    uint8_t v4[8];
} GUID;

typedef struct 
{
    GUID guid;			// generated by client computer
    uint64_t file_size;		// in bytes
                                // invalid if broadcasting
    uint64_t create_time;	// time of creation, in 100-nanosecond units since 1.1.1601
                                // invalid if broadcasting
    uint64_t packets_count;	// how many packets are there in the file
                                // invalid if broadcasting
    uint64_t play_time;		// play time, in 100-nanosecond units
                                // invalid if broadcasting
    uint64_t send_time;		// time to send file, in 100-nanosecond units
                                // invalid if broadcasting (could be ignored)
    uint32_t preroll;		// timestamp of the first packet, in milliseconds
    				// if nonzero - substract from time
    uint32_t ignore;            // preroll is 64bit - but let's just ignore it
    uint32_t flags;		// 0x01 - broadcast
    				// 0x02 - seekable
                                // rest is reserved should be 0
    uint32_t min_pktsize;	// size of a data packet
                                // invalid if broadcasting
    uint32_t max_pktsize;	// shall be the same as for min_pktsize
                                // invalid if broadcasting
    uint32_t max_bitrate;	// bandwith of stream in bps
    				// should be the sum of bitrates of the
                                // individual media streams
} ASFMainHeader;


typedef struct
{
    int seqno;
    int packet_size;
    int is_streamed;
    int asfid2avid[128];        /* conversion table from asf ID 2 AVStream ID */
    ASFStream streams[128];	/* it's max number and it's not that big */
    /* non streamed additonnal info */
    int64_t nb_packets;
    int64_t duration; /* in 100ns units */
    /* packet filling */
    int packet_size_left;
    int packet_timestamp_start;
    int packet_timestamp_end;
    int packet_nb_frames;
    uint8_t packet_buf[PACKET_SIZE];
    ByteIOContext pb;
    /* only for reading */
    uint64_t data_offset; /* begining of the first data packet */

    ASFMainHeader hdr;

    int packet_flags;
    int packet_property;
    int packet_timestamp;
    int packet_segsizetype;
    int packet_segments;
    int packet_seq;
    int packet_replic_size;
    int packet_key_frame;
    int packet_padsize;
    int packet_frag_offset;
    int packet_frag_size;
    int packet_frag_timestamp;
    int packet_multi_size;
    int packet_obj_size;
    int packet_time_delta;
    int packet_time_start;
    int packet_pos;

    int stream_index;
    ASFStream* asf_st; /* currently decoded stream */
} ASFContext;

typedef struct URLContext 
{
    struct URLProtocol *prot;
    int flags;        
    int is_streamed;  /* true if streamed (no seek possible), default = false */
    int max_packet_size;  /* if non zero, the stream is packetized with this max packet size */
    void *priv_data;
	//FILE *fid;
    char filename[1]; /* specified filename */
}URLContext;

typedef struct URLProtocol
{
    const char *name;
    int (*url_open)(URLContext *h, const char *filename, int flags);
    int (*url_read)(URLContext *h, unsigned char *buf, int size);
    int (*url_write)(URLContext *h, unsigned char *buf, int size);
    offset_t (*url_seek)(URLContext *h, offset_t pos, int whence);
    int (*url_close)(URLContext *h);
    struct URLProtocol *next;
} URLProtocol;


#define URL_EOF (-1)
#define URL_RDONLY 0
#define URL_WRONLY 1
#define URL_RDWR   2

#if 0
int file_open(URLContext *h, const char *filename, int flags);
int file_read(URLContext *h, unsigned char *buf, int size);
int file_write(URLContext *h, unsigned char *buf, int size);
offset_t file_seek(URLContext *h, offset_t pos, int whence);
int file_close(URLContext *h);

/* unbuffered I/O */



typedef int URLInterruptCB(void);

int url_open(URLContext **h, const char *filename, int flags);
int url_read(URLContext *h, unsigned char *buf, int size);
int url_write(URLContext *h, unsigned char *buf, int size);
offset_t url_seek(URLContext *h, offset_t pos, int whence);
int url_close(URLContext *h);
//int url_exist(const char *filename);
offset_t url_filesize(URLContext *h);
//int url_get_max_packet_size(URLContext *h);
//void url_get_filename(URLContext *h, char *buf, int buf_size);

/* the callback is called in blocking functions to test regulary if
   asynchronous interruption is needed. -EINTR is returned in this
   case by the interrupted function. 'NULL' means no interrupt
   callback is given. */

extern URLProtocol *first_protocol;


int register_protocol(URLProtocol *protocol);


/*
int init_put_byte(ByteIOContext *s,
                  unsigned char *buffer,
                  int buffer_size,
                  int write_flag,
                  void *opaque,
                  int (*read_packet)(void *opaque, uint8_t *buf, int buf_size),
                  void (*write_packet)(void *opaque, uint8_t *buf, int buf_size),
                  int (*seek)(void *opaque, offset_t offset, int whence));
*/
void put_byte(ByteIOContext *s, int b);
void put_buffer(ByteIOContext *s, const unsigned char *buf, int size);
void put_le64(ByteIOContext *s, uint64_t val);
void put_be64(ByteIOContext *s, uint64_t val);
void put_le32(ByteIOContext *s, unsigned int val);
void put_be32(ByteIOContext *s, unsigned int val);
void put_le16(ByteIOContext *s, unsigned int val);
void put_be16(ByteIOContext *s, unsigned int val);
void put_tag(ByteIOContext *s, const char *tag);

//void put_be64_double(ByteIOContext *s, double val);
void put_strz(ByteIOContext *s, const char *buf);

offset_t url_fseek(ByteIOContext *s, offset_t offset, int whence);
void url_fskip(ByteIOContext *s, offset_t offset);
offset_t url_ftell(ByteIOContext *s);
int url_feof(ByteIOContext *s);


int url_fgetc(ByteIOContext *s);
#ifdef __GNUC__
int url_fprintf(ByteIOContext *s, const char *fmt, ...) __attribute__ ((__format__ (__printf__, 2, 3)));
#else
int url_fprintf(ByteIOContext *s, const char *fmt, ...);
#endif
char *url_fgets(ByteIOContext *s, char *buf, int buf_size);

void put_flush_packet(ByteIOContext *s);

int get_buffer(ByteIOContext *s, unsigned char *buf, int size);
int get_byte(ByteIOContext *s);
unsigned int get_le32(ByteIOContext *s);
uint64_t get_le64(ByteIOContext *s);
unsigned int get_le16(ByteIOContext *s);

//////by ren//////double get_be64_double(ByteIOContext *s);
char *get_strz(ByteIOContext *s, char *buf, int maxlen);
unsigned int get_be16(ByteIOContext *s);
unsigned int get_be32(ByteIOContext *s);
uint64_t get_be64(ByteIOContext *s);

//static inline int url_is_streamed(ByteIOContext *s)

static __inline int url_is_streamed(ByteIOContext *s) //jacky 2006/10/18
{
    return s->is_streamed;
}

//int url_fdopen(ByteIOContext *s, URLContext *h);
//int url_setbufsize(ByteIOContext *s, int buf_size);
int url_fopen(ByteIOContext *s, const char *filename, int flags);
int url_fclose(ByteIOContext *s);
URLContext *url_fileno(ByteIOContext *s);
int url_fget_max_packet_size(ByteIOContext *s);

int url_open_buf(ByteIOContext *s, uint8_t *buf, int buf_size, int flags);
int url_close_buf(ByteIOContext *s);

int url_open_dyn_buf(ByteIOContext *s);
int url_open_dyn_packet_buf(ByteIOContext *s, int max_packet_size);
int url_close_dyn_buf(ByteIOContext *s, uint8_t **pbuffer);

/* file.c */
extern URLProtocol file_protocol;
extern URLProtocol pipe_protocol;


#ifdef HAVE_AV_CONFIG_H


int strstart(const char *str, const char *val, const char **ptr);

void pstrcpy(char *buf, int buf_size, const char *str);
char *pstrcat(char *buf, int buf_size, const char *s);




struct in_addr;


int match_ext(const char *filename, const char *extensions);

#endif /* HAVE_AV_CONFIG_H */
/**
 * AVCodec.
 */

#endif

#ifdef __cplusplus
}
#endif
}
#endif /* AVFORMAT_H */
