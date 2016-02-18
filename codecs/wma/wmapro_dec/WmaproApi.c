

#include "wmapro_dec_api.h"
#include "wma_audio_codec.h"
#include <stdlib.h>
#include <string.h>

#define LOG_TAG "SoftWmapro"
#include <utils/Log.h>
extern int  decode_init(AudioContext *avctx);
extern int  decode_end(AudioContext *avctx);
extern int  decode_packet(AudioContext *avctx, void *data, int *data_size, uint8_t* buf, int buf_size);
extern void flush(AudioContext *avctx);

_malloc_lib  malloc_lib;
_realloc_lib realloc_lib;
_free_lib    free_lib;
_memcpy_lib  memcpy_lib;
_memset_lib  memset_lib;
_memmove_lib memmove_lib;
_qsort_lib   qsort_lib;

AudioContext* wmapro_dec_init()
{
    AudioContext *avctx;
	free_lib =free; 
	realloc_lib=realloc;
	malloc_lib=malloc;
	memcpy_lib=memcpy;
	memset_lib=memset;
	qsort_lib=qsort;
    avctx =malloc_lib(sizeof(AudioContext));
    if (avctx==NULL)
		return NULL;
    memset_lib(avctx,0,sizeof(AudioContext));
    return avctx;
}

int wmapro_dec_decode_frame(AudioContext *avctx, unsigned char *inbuf, int input_size, signed short *outbuf, int *used)
{
    int ret;
    avctx->output_size = avctx->max_output_size;
    ret = decode_packet(avctx, outbuf, &(avctx->output_size), inbuf, input_size);
    if(ret < 0 )//error
    {
        return ret;
    }
    *used = ret;
    return WMAPRO_ERR_NoErr;
}


int wmapro_dec_free(AudioContext *avctx)
{
    return decode_end(avctx);
    
}


int wmapro_dec_get_property(AudioContext *avctx, int property, int *value)
{
    switch(property)
    {
    case WMAPRO_Get_Samples:
		*value = avctx->output_size >> 1;//samples are always short type.
		break;
	case WMAPRO_Get_Version:
	    break;
    default:
		return WMAPRO_ERR_GetProperty;
    }
    return WMAPRO_ERR_NoErr;
}


int wmapro_dec_set_property(AudioContext*avctx,int property,void *value)
{
	waveformat_t *wfp = NULL;
	psysfuncb_t *psysfuncb=NULL;
	int ret=0;
	switch (property)
	{
	case WMAPRO_Set_WavFormat:
		
		wfp=(waveformat_t*)value;
		avctx->codec_id = wfp->wFormatTag;//区分是wma1还是wma2
		avctx->extradata = wfp->extradata;//是不是需要申请一个空间以保留
		avctx->extradata_size = wfp->extradata_size;
		avctx->bit_rate = wfp->nAvgBytesPerSec * 8;
		avctx->sample_rate = wfp->nSamplesPerSec;
		avctx->channels = wfp->nChannels;
		avctx->block_align = wfp->nBlockAlign;
		avctx->max_output_size=MAX_OUT_BUF_SIZE;
		ALOGI("TRACE:\n");
		ALOGI("%s %d :samplerate =%d \n",__FUNCTION__,__LINE__,avctx->sample_rate);
	    ALOGI("%s %d :channels   =%d \n",__FUNCTION__,__LINE__,avctx->channels);
	    ALOGI("%s %d :bit_rate   =%d \n",__FUNCTION__,__LINE__,avctx->bit_rate);
	    ALOGI("%s %d :codec_tag  =%d \n",__FUNCTION__,__LINE__,avctx->codec_id);
	    ALOGI("%s %d :exdatsize  =%d \n",__FUNCTION__,__LINE__,avctx->extradata_size);
        ALOGI("%s %d :block_align=%d \n",__FUNCTION__,__LINE__,avctx->block_align);
	   
		ret=decode_init(avctx);
		ALOGI("decode_init_ret=%d\n\n",ret);
		break;
	case WMAPRO_Set_Sysfun:
		psysfuncb = (psysfuncb_t*)value;
		free_lib =(_free_lib)(psysfuncb->psys_free); 
		realloc_lib=(_realloc_lib)(psysfuncb->psys_realloc);
		malloc_lib=(_malloc_lib)(psysfuncb->psys_malloc);
		memcpy_lib=(_memcpy_lib)(psysfuncb->psys_memcpy);
		memset_lib=(_memset_lib)(psysfuncb->psys_memset);
		qsort_lib=(_qsort_lib)(psysfuncb->psys_qsort);
		break;
	default:
		return WMAPRO_ERR_SetProperty;
	}
	return WMAPRO_ERR_NoErr;
}



void wmapro_dec_reset(AudioContext *avctx)
{
	 flush(avctx);
}

