#ifndef __WMA_DEC_API_H__
#define __WMA_DEC_API_H__

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_OUT_BUF_SIZE (128*1024) //128k
#define PACKAGE "wmadec" /* Name of package */
#define VERSION "0.3.0" /* Version number of package */
//#define MAX_OUT_BUF_SIZE (8*1024) //8k

enum
{
    CODEC_ID_WMAV11=0x160,
    CODEC_ID_WMAV22=0x161,

 //from ffmpeg
    CODEC_ID_WMAV1=0x15007,
    CODEC_ID_WMAV2=0x15008,
    CODEC_ID_WMAPRO=0x15026,
};


enum wma_decError 
{
    WMA_DEC_ERR_NoErr = 0,
    WMA_DEC_ERR_UNKNOW=-100,
    WMA_DEC_ERR_NBFRAMES,
    WMA_DEC_ERR_LASTFRAME_LEN,
    WMA_DEC_ERR_EXCEED_MAX_CODED_SUPERFRAME_SIZE,
    WMA_DEC_ERR_LACK_OUTPUTMEM,
    WMA_DEC_ERR_PREV_BLOCK_LEN_BITS_OUTRANGE,
    WMA_DEC_ERR_BLOCK_LEN_BITS_OUTRANGE,
    WMA_DEC_ERR_NEXT_BLOCK_LEN_BITS_OUTRANGE,
    WMA_DEC_ERR_FRAMELEN_OVERFLOW,
    WMA_DEC_ERR_HGAIN_VLC_INVALID,
    WMA_DEC_ERR_DECODE_EXP_VLC_ERR,
    WMA_DEC_ERR_INIT,
    WMA_DEC_ERR_GetProperty = -1000,
    WMA_DEC_ERR_SetProperty,
    WMA_DEC_ERR_Init,
    WMA_DEC_ERR_Sync,
    WMA_DEC_ERR_,
};

enum wma_decProperty 
{
    WMA_DEC_Get_Samplerate = 0,
    WMA_DEC_Get_Samples,
    WMA_DEC_Get_FrameLength,
    WMA_DEC_Get_nbChannels,
    WMA_DEC_Get_VERSION,
    WMA_DEC_Get_Block_Align,
    WMA_DEC_Get_PktDecdone_Flag,
    WMA_DEC_Get_NbFramsInPkt,
    WMA_DEC_Get_ByteUsed_When_Err,
 
    WMA_DEC_Set_MemCallback = 100,
    WMA_DEC_Set_Wavfmt,
    WMA_DEC_Set_SysFunc,
};

typedef struct waveformatex_s {
	unsigned short wFormatTag;
	unsigned short nChannels;
	unsigned int   nSamplesPerSec;
	unsigned int   nAvgBytesPerSec;
	unsigned short nBlockAlign;
	unsigned short wBitsPerSample;
	unsigned short extradata_size;
	char *extradata;
}waveformatex_t;



typedef struct _WmaAudioContext 
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
	waveformatex_t *wfp;

	/*added by myself*/
	int new_pktheader_flag;
	int pkt_dec_done_flag;
	int nb_frames_in_packet;
	int nb_fra_decoded_cnt;
	//int nb_inbuf_used_bits;
	int nb_inbuf_used_bytes;
	int bit_offset_pktheader;
	int inbuf_size_original;
	char *inbuf_original;
	
} WmaAudioContext;


WmaAudioContext *wma_dec_init();
int wma_dec_decode_frame(WmaAudioContext *avctx, unsigned char *inbuf, int input_size, signed short *outbuf, int *used);
int wma_dec_free(WmaAudioContext *avctx);
int wma_dec_get_property(WmaAudioContext *avctx, int property, int *value);
int wma_dec_set_property(WmaAudioContext *avctx, int property, void *value);
void wma_dec_reset(WmaAudioContext *avctx);

#ifdef __cplusplus
}
#endif

#endif

