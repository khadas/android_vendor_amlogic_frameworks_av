#ifndef __WMAAPI_H__
#define __WMAAPI_H__


#ifdef __cplusplus
extern "C" {
#endif

#define MAX_OUT_BUF_SIZE (128*1024) //128k
#define PACKAGE "wmapro" /* Name of package */
#define VERSION "0.1.0" /* Version number of package */

enum
{
    CODEC_ID_WMAV11=0x160,
    CODEC_ID_WMAV22=0x161,

 //from ffmpeg
    CODEC_ID_WMAV1=0x15007,
    CODEC_ID_WMAV2=0x15008,
    CODEC_ID_WMAPRO=0x15026,
};

enum LyxWmaError {
    WMAPRO_ERR_NoErr = 0,
    WMAPRO_ERR_GetProperty = -1000,
    WMAPRO_ERR_SetProperty,
    WMAPRO_ERR_Init,
    WMAPRO_ERR_Sync,
    WMAPRO_ERR_InvalidData,
    WMAPRO_ERR_InvalidData1,
    WMAPRO_ERR_,
};

enum LyxWmaProperty {
    WMAPRO_Get_Samplerate = 0,
    WMAPRO_Get_Samples,
    WMAPRO_Get_FrameLength,
    WMAPRO_Get_Version,
    
    WMAPRO_Set_MemCallback = 100,
    WMAPRO_Set_WavFormat,
    WMAPRO_Set_Sysfun,
};

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

/* waveformatex fields specified in Microsoft documentation:
   http://msdn2.microsoft.com/en-us/library/ms713497.aspx */
typedef struct waveformat{
	unsigned short wFormatTag;
	unsigned short nChannels;
	unsigned int nSamplesPerSec;
	unsigned int nAvgBytesPerSec;
	unsigned short nBlockAlign;
	unsigned short wBitsPerSample;
	unsigned short extradata_size;
	char *extradata;
}waveformat_t;

typedef struct sysfuncb_s
{
	 void *psys_malloc;
	 void *psys_realloc;
	 void *psys_free;
	 void *psys_memcpy;
	 void *psys_memset;
	 void *psys_memmove;
	 void *psys_qsort;
}psysfuncb_t;

typedef struct tagAudioContext 
{
    /* Codec Context */
    int block_align;
    int nb_packets;
    int frame_number;
    int sample_rate;
    int channels;
    int bit_rate;
    int flags;
    int sample_fmt;
    unsigned int channel_layout;

    /*codec extradata*/
    int extradata_size;
    unsigned char *extradata;    

    int output_size;
    int max_output_size;
    int codec_id;
    void *priv_data;
} AudioContext;


/*
 * 该函数用来完成对wmapro解码器的初始化
 * avctx 指向LyxAudioContext结构体, 先要清0, 接着要初始化好里面的codec_id, sample_rate, channels,
 * bit_rate, block_align, extradata_size和extradata, 这些信息都可以从demux中取得的.
 * 
 * 返回值:  WMAPRO_ERR_NoErr    成功
 *          其它                失败
 */
AudioContext * wmapro_dec_init();


/*
 * 该函数是解码主函数
 * avctx 指向LyxAudioContext结构体
 * 
 * inbuf 指向输入缓冲区
 * input_size 表示inbuf的大小,单位是字节, 大小至少为block_align,否则不会解码该帧,
 *            一般从demux取得的asf中的一个packet里都含有整数倍block_align的数据
 * outbuf 指向输出缓冲区, 要预先分配好, 大小根据不同的文件会有差异,最大不超过128k字节
 * used 返回使用了inbuf的大小, 只要正常解码都会返回与block_align一样的值, 
 *      如果不是这个值且返回正确,说明input_size太小,不够解码
 * 返回值:  WMAPRO_ERR_NoErr    正确完成,没有出错
 *          负数                出错
 */
int wmapro_dec_decode_frame(AudioContext *avctx, unsigned char *inbuf, int input_size, signed short *outbuf, int *used);


/*
 * 该函数用于释放解码函数中申请的内存
 * avctx 指向LyxAudioContext结构体
 */
int wmapro_dec_free(AudioContext *avctx);


/*
 * 该函数用来取得一些属性值
 * avctx 指向LyxAudioContext结构体
 * property WMAPRO_Get_Samples      取得生成了多少个样本, 单位sizeof(short),用于在decode后取得outbuf的大小
 *
 * 返回值:  WMAPRO_ERR_NoErr       正确返回
 *          WMAPRO_ERR_GetProperty 出错
 */
int wmapro_dec_get_property(AudioContext *avctx, int property, int *value);




int wmapro_dec_set_property(AudioContext *avctx,int property,void *value);


/*
 * 该函数用来在重新定位后对解码器进行复位
 * avctx 指向LyxAudioContext结构体
 */
void wmapro_dec_reset(AudioContext *avctx);

#ifdef __cplusplus
}
#endif


#endif

