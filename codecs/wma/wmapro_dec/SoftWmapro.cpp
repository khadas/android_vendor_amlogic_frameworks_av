
#define LOG_TAG "SoftWmapro"
#include <utils/Log.h>

#include "SoftWmapro.h"

#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/MediaDefs.h>
#include <stdio.h>
#include <stdlib.h>
#include <OMX_Audio_EX_AML.h>
#ifdef WITH_AMLOGIC_MEDIA_EX_SUPPORT
#include <media/stagefright/AmMediaDefsExt.h>
#endif

namespace android {

template<class T>
static void InitOMXParams(T *params) {
    params->nSize = sizeof(T);
    params->nVersion.s.nVersionMajor = 1;
    params->nVersion.s.nVersionMinor = 0;
    params->nVersion.s.nRevision = 0;
    params->nVersion.s.nStep = 0;
}

SoftWmapro::SoftWmapro(
        const char *name,
        const OMX_CALLBACKTYPE *callbacks,
        OMX_PTR appData,
        OMX_COMPONENTTYPE **component)
    : SimpleSoftOMXComponent(name, callbacks, appData, component)
{
	ALOGI("%s %d \n",__FUNCTION__,__LINE__);
	mInputBufferCount=0;
	mOutputPortSettingsChange=NONE;
    initPorts();
	initDecoder();
	init_flag=0;
	mAnchorTimeUs=0;
	
}

SoftWmapro::~SoftWmapro() 
{
     wmapro_dec_free(wmactx);
}

void SoftWmapro::initPorts() {
	ALOGI("%s %d\n",__FUNCTION__,__LINE__);
    OMX_PARAM_PORTDEFINITIONTYPE def;
    InitOMXParams(&def);

    def.nPortIndex = 0;
    def.eDir = OMX_DirInput;
    def.nBufferCountMin = kNumBuffers;
    def.nBufferCountActual = def.nBufferCountMin;
    def.nBufferSize = 64*1024;
    def.bEnabled = OMX_TRUE;
    def.bPopulated = OMX_FALSE;
    def.eDomain = OMX_PortDomainAudio;
    def.bBuffersContiguous = OMX_FALSE;
    def.nBufferAlignment = 1;

    def.format.audio.cMIMEType =
        const_cast<char *>(MEDIA_MIMETYPE_AUDIO_WMAPRO);

    def.format.audio.pNativeRender = NULL;
    def.format.audio.bFlagErrorConcealment = OMX_FALSE;
    def.format.audio.eEncoding = OMX_AUDIO_CodingWMA;

    addPort(def);

    def.nPortIndex = 1;
    def.eDir = OMX_DirOutput;
    def.nBufferCountMin = kNumBuffers;
    def.nBufferCountActual = def.nBufferCountMin;
    def.nBufferSize = 128*1024*3;//kMaxNumSamplesPerBuffer * sizeof(int16_t);
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
void SoftWmapro::initDecoder() 
{
	wmactx=wmapro_dec_init();
	if(wmactx == NULL){
        ALOGE("init wma decoder error!! ret=%d \n", WMAPRO_ERR_Init);
    }
}

OMX_ERRORTYPE SoftWmapro::internalGetParameter(OMX_INDEXTYPE index,OMX_PTR params)
{

    switch (index) {

        case OMX_IndexParamAudioPcm:
        {
            OMX_AUDIO_PARAM_PCMMODETYPE *pcmParams =
                (OMX_AUDIO_PARAM_PCMMODETYPE *)params;
            if (pcmParams->nPortIndex != 1) {
                ALOGE("Err:OMX_ErrorUndefined %s %d %s\n",__FUNCTION__,__LINE__);
                return OMX_ErrorUndefined;
            }

            pcmParams->eNumData = OMX_NumericalDataSigned;
            pcmParams->eEndian = OMX_EndianBig;
            pcmParams->bInterleaved = OMX_TRUE;
            pcmParams->nBitPerSample = 16;
            pcmParams->ePCMMode = OMX_AUDIO_PCMModeLinear;
            pcmParams->eChannelMapping[0] = OMX_AUDIO_ChannelLF;
            pcmParams->eChannelMapping[1] = OMX_AUDIO_ChannelRF;

            if (!isConfigured()) {
                pcmParams->nChannels = 2;
                pcmParams->nSamplingRate = 44100;
            } else {
                pcmParams->nChannels = ((wfext.nChannels>2)? 2:wfext.nChannels);
                pcmParams->nSamplingRate = wfext.nSamplesPerSec;
            }
            return OMX_ErrorNone;
        }
        case OMX_IndexParamAudioWma:
        {
            ALOGI("OMX_IndexParamAudioWmaPro");
            return OMX_ErrorNone;
        }
        default:
            return SimpleSoftOMXComponent::internalGetParameter(index, params);
    }

}

int switch_id_tag(int codec_id)
{
    int codec_tag = codec_id;
    switch(codec_id){/*wFormatTag is 16 bits*/
        case (CODEC_ID_WMAV1 & 0xffff):
            codec_tag = 0x0160;
            break;
        case (CODEC_ID_WMAV2 & 0xffff):
            codec_tag = 0x0161;
            break;
        case (CODEC_ID_WMAPRO & 0xffff):
            codec_tag = 0x0162;
            break;
        default:
            ALOGE("codec id not implemented %x \n",codec_id);
    }

    return codec_tag;
}


OMX_ERRORTYPE SoftWmapro::internalSetParameter(OMX_INDEXTYPE index,const OMX_PTR params)
{
    switch (index) 
	{
        case OMX_IndexParamStandardComponentRole:
        {
            const OMX_PARAM_COMPONENTROLETYPE *roleParams =
                (const OMX_PARAM_COMPONENTROLETYPE *)params;
            if (strncmp((const char *)roleParams->cRole,
                        "audio_decoder.wmapro",
                        OMX_MAX_STRINGNAME_SIZE - 1)) {
                return OMX_ErrorUndefined;
            }

            return OMX_ErrorNone;
        }

        case OMX_IndexParamAudioWma:
        {
            const OMX_AUDIO_PARAM_ASFTYPE *AsfParams =
                (const OMX_AUDIO_PARAM_ASFTYPE *)params;

            if (AsfParams->nPortIndex != 0) {
                return OMX_ErrorUndefined;
            }
            wfext.nSamplesPerSec =AsfParams->nSamplesPerSec;
            wfext.nChannels      =AsfParams->nChannels;
            wfext.nBlockAlign    =AsfParams->nBlockAlign;
            wfext.wFormatTag     = switch_id_tag(AsfParams->wFormatTag);
            wfext.nAvgBytesPerSec=AsfParams->nAvgBitratePerSec>>3;
            wfext.extradata_size =AsfParams->extradata_size;
            wfext.extradata      =(char*)AsfParams->extradata;
            ALOGI("%s %d :samplerate =%d \n",__FUNCTION__,__LINE__,wfext.nSamplesPerSec);
            ALOGI("%s %d :channels   =%d \n",__FUNCTION__,__LINE__,wfext.nChannels);
            ALOGI("%s %d :bit_rate   =%d \n",__FUNCTION__,__LINE__,wfext.nAvgBytesPerSec);
            ALOGI("%s %d :codec_tag  =%d \n",__FUNCTION__,__LINE__,wfext.wFormatTag);
            ALOGI("%s %d :exdatsize  =%d \n",__FUNCTION__,__LINE__,wfext.extradata_size);
            ALOGI("%s %d :block_align=%d \n",__FUNCTION__,__LINE__,wfext.nBlockAlign);
            wmapro_dec_set_property(wmactx,WMAPRO_Set_WavFormat,&wfext);
            init_flag=1;
            return OMX_ErrorNone;
        }

        default:
            return SimpleSoftOMXComponent::internalSetParameter(index, params);
    }
}
bool SoftWmapro::isConfigured() const 
{
    return init_flag > 0;
}



void SoftWmapro::onQueueFilled(OMX_U32 portIndex) 
{
    if (mOutputPortSettingsChange != NONE) {
        return;
    }
    List<BufferInfo *> &inQueue = getPortQueue(0);
    List<BufferInfo *> &outQueue = getPortQueue(1);
    int used=0,pktbuf_size,nb_sample,ret,used_cnt=0;
    uint8_t *pktbuf=NULL;//less than 64k
	
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
		nb_sample=0;
		//-----------------------------------
		
		if (inHeader->nOffset == 0) {
            mAnchorTimeUs = inHeader->nTimeStamp;
            //mNumFramesOutput = 0;
        }
		
        pktbuf = inHeader->pBuffer + inHeader->nOffset;
        pktbuf_size = inHeader->nFilledLen;

		used=0;
		used_cnt=0;
		while(used_cnt<pktbuf_size){
			ret=wmapro_dec_decode_frame(wmactx, pktbuf+used_cnt, pktbuf_size-used_cnt, (short*)(outHeader->pBuffer+outHeader->nFilledLen), &used);

			if(ret != WMAPRO_ERR_NoErr){
				break;
				ALOGE("Err: wma decode error!! ret=%d, used=%d \n", ret, used);
			}else{
			    used_cnt+=used;
		    	wmapro_dec_get_property(wmactx,WMAPRO_Get_Samples, &nb_sample);	
            	if (nb_sample > 0){
					outHeader->nFilledLen += nb_sample* sizeof(int16_t);
                	outHeader->nOffset = 0;
                	outHeader->nFlags  = 0;
					outHeader->nTimeStamp = mAnchorTimeUs+ 
					                    (nb_sample * 1000000ll)/((wfext.nChannels>2)? 2:wfext.nChannels)/wfext.nSamplesPerSec;
            	}
        	}
		}
		 //-----------------------------
       
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

void SoftWmapro::onPortFlushCompleted(OMX_U32 portIndex) 
{

	ALOGI("%s %d\n",__FUNCTION__,__LINE__);
	wmapro_dec_reset(wmactx);
}

void SoftWmapro::onPortEnableCompleted(OMX_U32 portIndex, bool enabled) 
{
	ALOGI("%s %d\n",__FUNCTION__,__LINE__);
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
     ALOGI("%s %d\n",__FUNCTION__,__LINE__);
    return new android::SoftWmapro(name, callbacks, appData, component);
}
