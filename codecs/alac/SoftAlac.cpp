/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "SoftAlac"
#include <utils/Log.h>

#include "SoftAlac.h"
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/foundation/ADebug.h>
#include <string.h>
namespace android {

template<class T>
static void InitOMXParams(T *params) {
    params->nSize = sizeof(T);
    params->nVersion.s.nVersionMajor = 1;
    params->nVersion.s.nVersionMinor = 0;
    params->nVersion.s.nRevision = 0;
    params->nVersion.s.nStep = 0;
}

SoftAlac::SoftAlac(
        const char *name,
        const OMX_CALLBACKTYPE *callbacks,
        OMX_PTR appData,
        OMX_COMPONENTTYPE **component)
    : SimpleSoftOMXComponent(name, callbacks, appData, component),
      mInputBufferCount(0),
      mAnchorTimeUs(0),
      mOutputPortSettingsChange(NONE),      
      mNumSamplesOutput(0)
{
 	init_flag=0;
    initPorts();
    initDecoder();
}

SoftAlac::~SoftAlac()
{
	alac_decode_close(avctx);
	if(avctx->priv_data!=NULL)
	    av_free(avctx->priv_data);
	if(avctx->extradata!=NULL)
		av_free(avctx->extradata);
	if(avctx!=NULL)
        av_free(avctx);
   
}

void SoftAlac::initPorts() {
    OMX_PARAM_PORTDEFINITIONTYPE def;
    InitOMXParams(&def);

    def.nPortIndex = 0;
    def.eDir = OMX_DirInput;
    def.nBufferCountMin = kNumBuffers;
    def.nBufferCountActual = def.nBufferCountMin;
    def.nBufferSize = 8192*2;//bug91792:need 11787 at least
    def.bEnabled = OMX_TRUE;
    def.bPopulated = OMX_FALSE;
    def.eDomain = OMX_PortDomainAudio;
    def.bBuffersContiguous = OMX_FALSE;
    def.nBufferAlignment = 1;

    def.format.audio.cMIMEType = const_cast<char*>(MEDIA_MIMETYPE_AUDIO_ALAC );
    def.format.audio.pNativeRender = NULL;
    def.format.audio.bFlagErrorConcealment = OMX_FALSE;
    def.format.audio.eEncoding = OMX_AUDIO_CodingAAC;

    addPort(def);

    def.nPortIndex = 1;
    def.eDir = OMX_DirOutput;
    def.nBufferCountMin = kNumBuffers;
    def.nBufferCountActual = def.nBufferCountMin;
    def.nBufferSize = 128*1024;//4096*8*sizeof(int)
    def.bEnabled = OMX_TRUE;
    def.bPopulated = OMX_FALSE;
    def.eDomain = OMX_PortDomainAudio;
    def.bBuffersContiguous = OMX_FALSE;
    def.nBufferAlignment = 2;

    def.format.audio.cMIMEType = const_cast<char *>("audio/raw");
    def.format.audio.pNativeRender = NULL;
    def.format.audio.bFlagErrorConcealment = OMX_FALSE;
    def.format.audio.eEncoding = OMX_AUDIO_CodingPCM;

    addPort(def);
}

void SoftAlac::initDecoder() {
	avctx=(AVCodecContext *)av_malloc(sizeof(AVCodecContext));
	if(avctx == NULL){
        ALOGE("malloc space for alac decoder failed!!\n");
		return;
    }
	memset(avctx,0,sizeof(AVCodecContext));
	
	avctx->priv_data= av_malloc(sizeof(ALACContext));
	if(avctx->priv_data==NULL)
	{
       ALOGE("malloc space for alac->avctx.priv_data failed!!\n");
	   return;
	}
	memset(avctx->priv_data,0,sizeof(ALACContext));
	    
}

OMX_ERRORTYPE SoftAlac::internalGetParameter(
        OMX_INDEXTYPE index, OMX_PTR params) {
    switch (index) {
        case OMX_IndexParamAudioPcm:
        {
            OMX_AUDIO_PARAM_PCMMODETYPE *pcmParams =
                (OMX_AUDIO_PARAM_PCMMODETYPE *)params;

            if (pcmParams->nPortIndex != 1) {
                return OMX_ErrorUndefined;
            }

            pcmParams->eNumData = OMX_NumericalDataSigned;
            pcmParams->eEndian = OMX_EndianBig;
            pcmParams->bInterleaved = OMX_TRUE;
            pcmParams->nBitPerSample = 16;// note:bug may emergy
            pcmParams->ePCMMode = OMX_AUDIO_PCMModeLinear;
            pcmParams->eChannelMapping[0] = OMX_AUDIO_ChannelLF;
            pcmParams->eChannelMapping[1] = OMX_AUDIO_ChannelRF;

            if (!isConfigured()) {
                pcmParams->nChannels = 1;
                pcmParams->nSamplingRate = 44100;
            } else {
                pcmParams->nChannels    = avctx->channels;
                pcmParams->nSamplingRate = avctx->sample_rate;
            }

            return OMX_ErrorNone;
        }

        default:
            return SimpleSoftOMXComponent::internalGetParameter(index, params);
    }
}

OMX_ERRORTYPE SoftAlac::internalSetParameter(
        OMX_INDEXTYPE index, const OMX_PTR params) {
    switch (index) {
        case OMX_IndexParamStandardComponentRole:
        {
            const OMX_PARAM_COMPONENTROLETYPE *roleParams =
                (const OMX_PARAM_COMPONENTROLETYPE *)params;
            if (strncmp((const char *)roleParams->cRole,
                        "audio_decoder.alac",
                        OMX_MAX_STRINGNAME_SIZE - 1)) {
                return OMX_ErrorUndefined;
            }
            return OMX_ErrorNone;
        }
        case OMX_IndexParamAudioAlac:
		{
            const OMX_AUDIO_PARAM_ALACTYPE *AlacParams =(const OMX_AUDIO_PARAM_ALACTYPE *)params;
            if (AlacParams->nPortIndex != 0) {
                return OMX_ErrorUndefined;
            }

			avctx->sample_rate   = AlacParams->nSamplesPerSec;
			avctx->channels      = AlacParams->nChannels;
			avctx->extradata_size= AlacParams->extradata_size;
			avctx->extradata     = (uint8_t*)av_malloc(avctx->extradata_size);
	        if(avctx->extradata==NULL)
	        {
		       ALOGE("malloc space for alac->avctx.extradata failed!!\n");
	           return OMX_ErrorInsufficientResources ;
	        }
			memcpy(avctx->extradata,AlacParams->extradata,avctx->extradata_size);
			alac_decode_init(avctx);
            init_flag=1;
			return OMX_ErrorNone;
        }
        default:
            return SimpleSoftOMXComponent::internalSetParameter(index, params);
    }
}

bool SoftAlac::isConfigured() const {
    return (init_flag > 0);
}

void SoftAlac::onQueueFilled(OMX_U32 portIndex)
{
    if (mOutputPortSettingsChange != NONE) {
        return;
    }
	List<BufferInfo *> &inQueue = getPortQueue(0);
	List<BufferInfo *> &outQueue = getPortQueue(1);
	int ret;
	AVPacket avpkt={0};
	//AVFrame outframe={0};
	int got_frame_ptr=0;

	while (!inQueue.empty() && !outQueue.empty()) {
		BufferInfo *inInfo = *inQueue.begin();
		OMX_BUFFERHEADERTYPE *inHeader = inInfo->mHeader;
	
		BufferInfo *outInfo = *outQueue.begin();
		OMX_BUFFERHEADERTYPE *outHeader = outInfo->mHeader;
			
		if (inHeader->nFlags & OMX_BUFFERFLAG_EOS) {
			inQueue.erase(inQueue.begin());
			inInfo->mOwnedByUs = false;
			notifyEmptyBufferDone(inHeader);
	
			outHeader->nFilledLen = 0;
			outHeader->nFlags = OMX_BUFFERFLAG_EOS;
	
			outQueue.erase(outQueue.begin());
			outInfo->mOwnedByUs = false;
			notifyFillBufferDone(outHeader);
			return;
		}
	
		if (inHeader->nOffset == 0) {
			mAnchorTimeUs = inHeader->nTimeStamp;
		}
			
		avpkt.data = inHeader->pBuffer + inHeader->nOffset;
		avpkt.size = inHeader->nFilledLen;
		
		//outframe.data[0]=outHeader->pBuffer;
		
		ALACContext *alac = (ALACContext*)(avctx->priv_data);
		ret=alac_decode_frame(avctx, outHeader->pBuffer,&got_frame_ptr, &avpkt);
		
		//memcpy(outHeader->pBuffer,
		//	   alac->frame.data[0],
		//	   alac->channels*alac->nb_samples*(alac->sample_size>>3));
		if(got_frame_ptr)
		     outHeader->nFilledLen = alac->channels*alac->nb_samples*(alac->sample_size>>3);
		else{
			 ALOGW("WARNING: got_frame_ptr=0-->set outHeader->nFilledLen=0!!\n");
			 outHeader->nFilledLen=0;
		}
		outHeader->nOffset = 0;
		outHeader->nFlags = 0;
		outHeader->nTimeStamp = mAnchorTimeUs+(alac->nb_samples * 1000000ll)/avctx->sample_rate;
     
		
		inInfo->mOwnedByUs = false;
		inQueue.erase(inQueue.begin());
		inInfo = NULL;
		notifyEmptyBufferDone(inHeader);
		inHeader = NULL;
	
		outInfo->mOwnedByUs = false;
		outQueue.erase(outQueue.begin());
		outInfo = NULL;
		notifyFillBufferDone(outHeader);
		outHeader = NULL;
		++mInputBufferCount;
	}
}


void SoftAlac::onPortFlushCompleted(OMX_U32 portIndex)
{
    if (portIndex == 0) 
	{
		
    }
}

void SoftAlac::onPortEnableCompleted(OMX_U32 portIndex, bool enabled) {
	//LOGI("TRACE:%s %d %s\n",__FUNCTION__,__LINE__,__FILE__);
    if (portIndex != 1) {
        return;
    }

    switch (mOutputPortSettingsChange) {
        case NONE:
            break;

        case AWAITING_DISABLED:
        {
            CHECK(!enabled);
            mOutputPortSettingsChange = AWAITING_ENABLED;
            break;
        }

        default:
        {
            CHECK_EQ((int)mOutputPortSettingsChange, (int)AWAITING_ENABLED);
            CHECK(enabled);
            mOutputPortSettingsChange = NONE;
            break;
        }
    }
}

}  // namespace android

android::SoftOMXComponent *createSoftOMXComponent(
        const char *name, const OMX_CALLBACKTYPE *callbacks,
        OMX_PTR appData, OMX_COMPONENTTYPE **component) {
    ALOGI("%s %d %s\n",__FUNCTION__,__LINE__,__FILE__);
    return new android::SoftAlac(name, callbacks, appData, component);
}
