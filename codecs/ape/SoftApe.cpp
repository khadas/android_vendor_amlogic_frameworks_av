
#define LOG_TAG "SoftApe"
#include <utils/Log.h>

#include "SoftApe.h"
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/MediaDefs.h>

namespace android {

template<class T>
static void InitOMXParams(T *params) {
    params->nSize = sizeof(T);
    params->nVersion.s.nVersionMajor = 1;
    params->nVersion.s.nVersionMinor = 0;
    params->nVersion.s.nRevision = 0;
    params->nVersion.s.nStep = 0;
}

SoftApe::SoftApe(
        const char *name,
        const OMX_CALLBACKTYPE *callbacks,
        OMX_PTR appData,
        OMX_COMPONENTTYPE **component)
    : SimpleSoftOMXComponent(name, callbacks, appData, component)
{
	ALOGI("%s %d\n",__FUNCTION__,__LINE__);
	mInputBufferCount=0;
    init_flag=0;
	mOutputPortSettingsChange=NONE;
    initPorts();
	mAnchorTimeUs=0;
	memset(&ApeCtx,0,sizeof(APEContext));
    extradata=NULL;
    extradata_size=0;
    channels=0;
    bits_per_coded_sample=0;
    
}

SoftApe::~SoftApe() 
{
   ape_decode_close(&ApeCtx);
}

#define OUT_BUF_LEN ((8192*2)*2)

void SoftApe::initPorts() {
	ALOGI("%s %d\n",__FUNCTION__,__LINE__);
    OMX_PARAM_PORTDEFINITIONTYPE def;
    InitOMXParams(&def);
    
    def.nPortIndex = 0;
    def.eDir = OMX_DirInput;
    def.nBufferCountMin = kNumBuffers;
    def.nBufferCountActual = def.nBufferCountMin;
    def.nBufferSize = (1024*4)*1024;//experimental value-->insane profile may need more!
    def.bEnabled = OMX_TRUE;
    def.bPopulated = OMX_FALSE;
    def.eDomain = OMX_PortDomainAudio;
    def.bBuffersContiguous = OMX_FALSE;
    def.nBufferAlignment = 1;
    def.format.audio.cMIMEType =const_cast<char *>(MEDIA_MIMETYPE_AUDIO_APE);
    def.format.audio.pNativeRender = NULL;
    def.format.audio.bFlagErrorConcealment = OMX_FALSE;
    def.format.audio.eEncoding = OMX_AUDIO_CodingAAC;

    addPort(def);

    def.nPortIndex = 1;
    def.eDir = OMX_DirOutput;
    def.nBufferCountMin = kNumBuffers;
    def.nBufferCountActual = def.nBufferCountMin;
    def.nBufferSize = OUT_BUF_LEN;
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

OMX_ERRORTYPE SoftApe::internalGetParameter(OMX_INDEXTYPE index,OMX_PTR params)
{
	
    switch (index) {

        case OMX_IndexParamAudioPcm:
        {
			
            OMX_AUDIO_PARAM_PCMMODETYPE *pcmParams =(OMX_AUDIO_PARAM_PCMMODETYPE *)params;

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
                pcmParams->nSamplingRate = 48000;
            } else {
                pcmParams->nChannels = channels;//mVi->channels;
                pcmParams->nSamplingRate = sample_rate;
            }
            return OMX_ErrorNone;
        }
        default:
            return SimpleSoftOMXComponent::internalGetParameter(index, params);
    }

}



OMX_ERRORTYPE SoftApe::internalSetParameter(OMX_INDEXTYPE index,const OMX_PTR params)
{
	
    switch (index) 
	{
        case OMX_IndexParamStandardComponentRole:
        {
            const OMX_PARAM_COMPONENTROLETYPE *roleParams =(const OMX_PARAM_COMPONENTROLETYPE *)params;

            if (strncmp((const char *)roleParams->cRole,
                        "audio_decoder.ape",
                        OMX_MAX_STRINGNAME_SIZE - 1)) {
                return OMX_ErrorUndefined;
            }

            return OMX_ErrorNone;
        }

        case OMX_IndexParamAudioApe:
        {
            const OMX_AUDIO_PARAM_APETYPE *ApeParams =(const OMX_AUDIO_PARAM_APETYPE *)params;

            if (ApeParams->nPortIndex != 0) {
                return OMX_ErrorUndefined;
            }
            sample_rate    =ApeParams->nSamplesPerSec;
			channels       =ApeParams->nChannels;
	        bits_per_coded_sample    =ApeParams->wBitsPerSample;
			extradata_size =ApeParams->extradata_size;
			extradata      =ApeParams->extradata;

			ALOGI("%s %d :samplerate =%d \n",__FUNCTION__,__LINE__,sample_rate);
	        ALOGI("%s %d :channels   =%d \n",__FUNCTION__,__LINE__,channels);
	        ALOGI("%s %d :bit_widtd  =%d \n",__FUNCTION__,__LINE__,bits_per_coded_sample);
	        ALOGI("%s %d :exdatsize  =%d \n",__FUNCTION__,__LINE__,extradata_size);
	        ape_decode_init(&ApeCtx,extradata,extradata_size,channels,bits_per_coded_sample);
			init_flag=1;
            return OMX_ErrorNone;
        }

        default:
            return SimpleSoftOMXComponent::internalSetParameter(index, params);
    }
}
bool SoftApe::isConfigured() const 
{
    return init_flag > 0;
}

static void dump_pcm_bin(char *path,char *buf,int size)
{
	FILE *fp=fopen(path,"ab+");
	 if(fp!= NULL){
		   fwrite(buf,1,size,fp);
		   fclose(fp);
	}
}

void SoftApe::onQueueFilled(OMX_U32 portIndex) 
{
    if (mOutputPortSettingsChange != NONE) {
        return;
    }
    List<BufferInfo *> &inQueue = getPortQueue(0);
    List<BufferInfo *> &outQueue = getPortQueue(1);
	int used,pktbuf_size,nb_sample,ret;
	uint8_t *pktbuf=NULL;//less than 64k
	int outbuflen=OUT_BUF_LEN;
    int used_cnt=0;
    
    while (!inQueue.empty() && !outQueue.empty()) {
        BufferInfo *inInfo = *inQueue.begin();
        OMX_BUFFERHEADERTYPE *inHeader = inInfo->mHeader;

        BufferInfo *outInfo = *outQueue.begin();
        OMX_BUFFERHEADERTYPE *outHeader = outInfo->mHeader;
        if ((inHeader->nFlags & OMX_BUFFERFLAG_EOS) && !ApeCtx.samples) {
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


        pktbuf = inHeader->pBuffer + inHeader->nOffset;
        pktbuf_size = inHeader->nFilledLen;
        
        used_cnt=ape_decode_frame(&ApeCtx,pktbuf,pktbuf_size,(short*)(outHeader->pBuffer),&outbuflen);
        if (inHeader->nOffset == 0 && used_cnt>0) {
                
                mAnchorTimeUs = inHeader->nTimeStamp;
                
        }
		
        if(outbuflen>0)
        {
            outHeader->nFilledLen += outbuflen;
            outHeader->nOffset = 0;
            outHeader->nFlags = 0;
            mAnchorTimeUs +=(outbuflen * 1000000ll)/sizeof(int16_t)/channels/sample_rate;
	        outHeader->nTimeStamp = mAnchorTimeUs;
        }
        
        inHeader->nFilledLen-=used_cnt;
        inHeader->nOffset   +=used_cnt;
        if(inHeader->nFilledLen==0 && !ApeCtx.samples){
            inInfo->mOwnedByUs = false;
            inQueue.erase(inQueue.begin());
            inInfo = NULL;
            notifyEmptyBufferDone(inHeader);
            inHeader = NULL;
        }
        outInfo->mOwnedByUs = false;
        outQueue.erase(outQueue.begin());
        outInfo = NULL;
        notifyFillBufferDone(outHeader);
        outHeader = NULL;
        
        ++mInputBufferCount;
    }
}

void SoftApe::onPortFlushCompleted(OMX_U32 portIndex) 
{
	ALOGI("%s %d\n",__FUNCTION__,__LINE__);
	ape_flush(&ApeCtx);
}

void SoftApe::onPortEnableCompleted(OMX_U32 portIndex, bool enabled) 
{
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
    ALOGI("%s %d \n",__FUNCTION__,__LINE__);
    return new android::SoftApe(name, callbacks, appData, component);
}
