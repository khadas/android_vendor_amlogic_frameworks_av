
#define LOG_TAG "SoftWmastd"
#include <utils/Log.h>

#include "wma_dec_api.h"
#include "wma.h"
#include <string.h>
#ifndef NULL
#define NULL 0
#endif
//-------------------------------------
#include "common.h"
#include <stdlib.h>

//---------------------------------------
WmaAudioContext* wma_dec_init()
{
    WmaAudioContext *avctx;
    avctx =malloc(sizeof(WmaAudioContext));
    if (avctx==NULL)
		return NULL;
    memset(avctx,0,sizeof(WmaAudioContext));
    return avctx;
}

void wma_dec_reset(WmaAudioContext *avctx)
{
   int ch;
    WMACodecContext *s = avctx->priv_data;
    avctx->new_pktheader_flag=1;
	wma_decode_flush(avctx);
	for(ch = 0; ch < s->nb_channels; ch++)
	{
	   memset(&s->frame_out[ch][0], 0, BLOCK_MAX_SIZE * 2 * sizeof(FIXP));
	}
}


int wma_dec_decode_frame(WmaAudioContext *avctx, unsigned char *inbuf, int input_size, signed short *outbuf, int *used)
{
    int ret;   
	avctx->output_size = avctx->max_output_size;
    ret = wma_decode_superframe(avctx, outbuf, &(avctx->output_size), inbuf, input_size);//avctx->outputsize¼ÇÂ¼Êä³öµÄoutput samples;	
    if(ret < 0 )
    {//error
        return ret;
    }
    *used = ret;
    return WMA_DEC_ERR_NoErr;
}


int wma_dec_free(WmaAudioContext *avctx)
{   
    int ret=WMA_DEC_ERR_NoErr;
    ret=ff_wma_end(avctx);
	if(avctx->priv_data!=NULL)
		free(avctx->priv_data);
    free(avctx);
    return ret;
}


int wma_dec_get_property(WmaAudioContext *avctx, int property, int *value)
{
	
    switch(property)
    {
	case WMA_DEC_Get_Samples:
		*value = avctx->output_size >> 1;//samples are always short type.
		break;
	case WMA_DEC_Get_nbChannels:
		*value = avctx->channels;
		break;
	case WMA_DEC_Get_Samplerate:
		*value = avctx->sample_rate;
		break;
	case WMA_DEC_Get_VERSION:
		break;
	case WMA_DEC_Get_Block_Align:
		*value = avctx->block_align;
		break;  
	case WMA_DEC_Get_PktDecdone_Flag:
		*value = avctx->pkt_dec_done_flag;
		break;
	case WMA_DEC_Get_NbFramsInPkt:
		*value = avctx->nb_frames_in_packet;
		break;
	case WMA_DEC_Get_ByteUsed_When_Err:
		// *value = avctx->nb_inbuf_used_bits>>3;
		break;
	default:
		return WMA_DEC_ERR_GetProperty;
    }
    return WMA_DEC_ERR_NoErr;
}

int wma_dec_set_property(WmaAudioContext *avctx, int property, void *value)
{  
    waveformatex_t *wfpt;
    waveformatex_t *wfp_av;
    int i,ret;
    switch (property)
	{
    case WMA_DEC_Set_Wavfmt:
        ff_wma_end(avctx);
        wfpt=(waveformatex_t*)value;
		avctx->wfp=malloc(sizeof(waveformatex_t));
		wfp_av = avctx->wfp;
        wfp_av->wFormatTag = wfpt->wFormatTag;
        wfp_av->nChannels = wfpt->nChannels;
        wfp_av->nSamplesPerSec = wfpt->nSamplesPerSec;
        wfp_av->nAvgBytesPerSec = wfpt->nAvgBytesPerSec;
        wfp_av->extradata_size= wfpt->extradata_size;
        wfp_av->wBitsPerSample = wfpt->wBitsPerSample;
		wfp_av->nBlockAlign = wfpt->nBlockAlign;
        wfp_av->extradata=malloc(wfpt->extradata_size);
        for (i=0; i<wfpt->extradata_size; i++){
			wfp_av->extradata[i]=wfpt->extradata[i];
        }
        avctx->codec_id = wfp_av->wFormatTag;
        avctx->extradata=wfp_av->extradata;
        avctx->extradata_size= wfp_av->extradata_size;
        avctx->bit_rate = wfp_av->nAvgBytesPerSec << 3;
        avctx->sample_rate = wfp_av->nSamplesPerSec;
        avctx->channels =wfp_av->nChannels;
        avctx->block_align = wfp_av->nBlockAlign;
        ret=wma_decode_init(avctx);
		if(ret<0)
		 ALOGE("wma_decode_init failed\n");
        avctx->max_output_size=MAX_OUT_BUF_SIZE;
		break;
	case WMA_DEC_Set_SysFunc:
		break;
	default:
		return WMA_DEC_ERR_SetProperty;
	}
	return WMA_DEC_ERR_NoErr;
}







