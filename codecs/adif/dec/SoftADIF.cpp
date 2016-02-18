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

#define LOG_TAG "SoftADIF"
#include <utils/Log.h>

#include "SoftADIF.h"

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

SoftADIF::SoftADIF(
        const char *name,
        const OMX_CALLBACKTYPE *callbacks,
        OMX_PTR appData,
        OMX_COMPONENTTYPE **component)
    : SimpleSoftOMXComponent(name, callbacks, appData, component),
      mNumChannels(1),
      mNumSampleRate(44100),
      mNumFrameLength(0),
      hDecoder(NULL),
      frameInfo(NULL),
      config(NULL),
      mInputBufferCount(0),
      mAnchorTimeUs(0),
      b(NULL),
      mSignalledError(false),
      mOutputPortSettingsChange(NONE){
      CHECK(!strcmp(name, "OMX.google.adif.decoder"));
    initPorts();
	CHECK_EQ(initDecoder(), (status_t)OK);
	pAdifHeader=NULL;
	AdifHeaderSize=0;
}

SoftADIF::~SoftADIF() {
	NeAACDecClose(hDecoder);
	if(frameInfo != NULL){
		free(frameInfo);
		frameInfo = NULL;
	}
	if(b->buffer != NULL){
		free(b->buffer);
		b->buffer = NULL;
	}
	if(b != NULL){
		free(b);
		b = NULL;
	}
	if(pAdifHeader!=NULL)
	   free(pAdifHeader);

}

void SoftADIF::initPorts() {
	
    OMX_PARAM_PORTDEFINITIONTYPE def;
    InitOMXParams(&def);

    def.nPortIndex = 0;
    def.eDir = OMX_DirInput;
    def.nBufferCountMin = kNumBuffers;
    def.nBufferCountActual = def.nBufferCountMin;
    def.nBufferSize = 8192;
    def.bEnabled = OMX_TRUE;
    def.bPopulated = OMX_FALSE;
    def.eDomain = OMX_PortDomainAudio;
    def.bBuffersContiguous = OMX_FALSE;
    def.nBufferAlignment = 1;

    def.format.audio.cMIMEType = 
        const_cast<char *>(MEDIA_MIMETYPE_AUDIO_AAC_ADIF);

    def.format.audio.pNativeRender = NULL;
    def.format.audio.bFlagErrorConcealment = OMX_FALSE;
    def.format.audio.eEncoding = OMX_AUDIO_CodingAAC;

    addPort(def);

    def.nPortIndex = 1;
    def.eDir = OMX_DirOutput;
    def.nBufferCountMin = kNumBuffers;
    def.nBufferCountActual = def.nBufferCountMin;
    def.nBufferSize = 8192;
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

status_t SoftADIF::initDecoder() {
	ALOGI("SoftAAC::initDecoder\n");
	status_t status = UNKNOWN_ERROR;

	unsigned long cap = NeAACDecGetCapabilities();
	ALOGI("NeAACDecCapabilities=%ld\n",cap);

	if(NULL == (hDecoder = NeAACDecOpen())){
		ALOGI("NeAACDecOpen failed\n");
		return status;
	}
	if(NULL == (config = NeAACDecGetCurrentConfiguration(hDecoder))){
		ALOGI("NeAACDecGetCurrentConfiguration failed\n");
		return status;
	}else{
		config->defSampleRate = 44100;
    	config->defObjectType = LC;
    	config->outputFormat = FAAD_FMT_16BIT;
    	config->downMatrix = 1;
    	config->useOldADTSFormat = 0;
	}
	
	if(0 == (NeAACDecSetConfiguration(hDecoder, config))){
		ALOGI("NeAACDecSetConfiguration failed!\n");
		return status;
	}
	if(NULL == (frameInfo = (NeAACDecFrameInfo *)malloc(sizeof(NeAACDecFrameInfo)))){
		ALOGI("frameinfo malloc failed\n");
		return status;
	}

	if(NULL == (b = (aac_buffer *)malloc(sizeof(aac_buffer)))){
		ALOGI("aac_buffer malloc failed\n");
		return status;
	}
	memset(b, 0, sizeof(aac_buffer));
	
	if(!(b->buffer = (unsigned char * )malloc(AAC_MIN_STREAMSIZE * AAC_CHANNELS * 2))){
		ALOGI("buffer malloc fialed\n");
		return status;
	}
	b->buflen=FAAD_MIN_STREAMSIZE * AAC_CHANNELS* 2;
	memset(b->buffer, 0, FAAD_MIN_STREAMSIZE * AAC_CHANNELS* 2);
	b->bytes_into_buffer = 0;
	b->bytes_consumed = 0;
    b->file_offset = 0;
	b->at_eof = 0;
	
	return OK;
}

OMX_ERRORTYPE SoftADIF::internalGetParameter(
        OMX_INDEXTYPE index, OMX_PTR params) {

    switch (index) {
		case OMX_IndexParamAudioAac:
        {
            OMX_AUDIO_PARAM_AACPROFILETYPE *aacParams =
                (OMX_AUDIO_PARAM_AACPROFILETYPE *)params;

            if (aacParams->nPortIndex != 0) {
                return OMX_ErrorUndefined;
            }

            aacParams->nBitRate = 0;
            aacParams->nAudioBandWidth = 0;
            aacParams->nAACtools = 0;
            aacParams->nAACERtools = 0;
            aacParams->eAACProfile = OMX_AUDIO_AACObjectLC;
            aacParams->eAACStreamFormat = OMX_AUDIO_AACStreamFormatADIF;
            aacParams->eChannelMode = OMX_AUDIO_ChannelModeJointStereo;

            
            if (!isConfigured()) {
                aacParams->nChannels = 1;
                aacParams->nSampleRate = 44100;
                aacParams->nFrameLength = 0;
            } else {
                aacParams->nChannels = mNumChannels;
                aacParams->nSampleRate = mNumSampleRate;
                aacParams->nFrameLength = mNumFrameLength;
            }
            

            return OMX_ErrorNone;
        }
        case OMX_IndexParamAudioPcm:
        {
            OMX_AUDIO_PARAM_PCMMODETYPE *pcmParams =
                (OMX_AUDIO_PARAM_PCMMODETYPE *)params;

            if (pcmParams->nPortIndex > 1) {
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
                pcmParams->nChannels = 1;
                pcmParams->nSamplingRate = 44100;
            } else {
                pcmParams->nChannels = mNumChannels;
                pcmParams->nSamplingRate = mNumSampleRate;
            }

            return OMX_ErrorNone;
        }

        default:
            return SimpleSoftOMXComponent::internalGetParameter(index, params);
    }
}

OMX_ERRORTYPE SoftADIF::internalSetParameter(
        OMX_INDEXTYPE index, const OMX_PTR params) {
        
    switch (index) {
        case OMX_IndexParamAudioPcm:
        {
            OMX_AUDIO_PARAM_PCMMODETYPE *pcmParams =
                (OMX_AUDIO_PARAM_PCMMODETYPE *)params;

            if (pcmParams->nPortIndex != 0) {
                return OMX_ErrorUndefined;
            }

            if (pcmParams->nChannels < 1 || pcmParams->nChannels > 2) {
                return OMX_ErrorUndefined;
            }

            mNumChannels = pcmParams->nChannels;

            return OMX_ErrorNone;
        }

        case OMX_IndexParamStandardComponentRole:
        {
            const OMX_PARAM_COMPONENTROLETYPE *roleParams =
                (const OMX_PARAM_COMPONENTROLETYPE *)params;

            
                if (strncmp((const char *)roleParams->cRole,
                            "audio_decoder.adif",
                            OMX_MAX_STRINGNAME_SIZE - 1)) {
                    return OMX_ErrorUndefined;
                }
         

            return OMX_ErrorNone;
        }
		case OMX_IndexParamAudioAac:
        {
            const OMX_AUDIO_PARAM_AACPROFILETYPE *aacParams =
                (const OMX_AUDIO_PARAM_AACPROFILETYPE *)params;

            if (aacParams->nPortIndex != 0) {
                return OMX_ErrorUndefined;
            }

            return OMX_ErrorNone;
        }

        default:
            return SimpleSoftOMXComponent::internalSetParameter(index, params);
    }
}

bool SoftADIF::isConfigured() const {
    return mInputBufferCount > 0;
}

void SoftADIF::onQueueFilled(OMX_U32 portIndex) {

    if (mSignalledError || mOutputPortSettingsChange != NONE) {
        return;
    }

    List<BufferInfo *> &inQueue = getPortQueue(0);
    List<BufferInfo *> &outQueue = getPortQueue(1);

	long bytes_into_buffer = 0;
    unsigned char *buffer = NULL;
	int bread = 0;

	if (portIndex == 0 && mInputBufferCount == 0) {

			++mInputBufferCount;

			BufferInfo *info = *inQueue.begin();
			OMX_BUFFERHEADERTYPE *header = info->mHeader;
	
			buffer = header->pBuffer + header->nOffset;
			bytes_into_buffer = header->nFilledLen;

			buffer = header->pBuffer + header->nOffset;
			bytes_into_buffer = header->nFilledLen;

			if ((bread = NeAACDecInit(hDecoder, buffer,
       				 bytes_into_buffer, (unsigned long *)(&mNumSampleRate), (unsigned char *)(&mNumChannels))) < 0)
    		{
       			 /* If some error initializing occured, skip the file */
       		 	ALOGI("Error initializing decoder library.\n");
				mSignalledError = true;
				notify(OMX_EventError, OMX_ErrorUndefined, bread, NULL);
        		NeAACDecClose(hDecoder);
        		return;
    		}else{
    			ALOGI("NeAACDecInit success!\n");
				ALOGI("mNumSampleRate=%ld,mNumChannels=%d\n",mNumSampleRate,mNumChannels);	
				if(mNumChannels > 2)
					mNumChannels = 2;
			}

			pAdifHeader=(unsigned char *)malloc(bread);
			if(pAdifHeader!=NULL){
			    memcpy(pAdifHeader,buffer,bread);
			    AdifHeaderSize=bread;
			    ALOGI("AdifHeaderSize/%d\n",AdifHeaderSize);
			}else{
			    ALOGE("malloc for AdifHeaderSize failed!\n");
			}
			inQueue.erase(inQueue.begin());
			info->mOwnedByUs = false;
			notifyEmptyBufferDone(header);
			notify(OMX_EventPortSettingsChanged, 1, 0, NULL);
			mOutputPortSettingsChange = AWAITING_DISABLED;
			return;

		}

    while (!inQueue.empty() && !outQueue.empty()) {
        BufferInfo *inInfo = *inQueue.begin();
        OMX_BUFFERHEADERTYPE *inHeader = inInfo->mHeader;

        BufferInfo *outInfo = *outQueue.begin();
        OMX_BUFFERHEADERTYPE *outHeader = outInfo->mHeader;
        int MinBytesNeed=AAC_MIN_STREAMSIZE * AAC_CHANNELS;

        if (inHeader->nFlags & OMX_BUFFERFLAG_EOS){
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

		if (inHeader->nFilledLen > MinBytesNeed) {
            ALOGE("input buffer too large (%ld).", inHeader->nFilledLen);
            notify(OMX_EventError, OMX_ErrorUndefined, 0, NULL);
            mSignalledError = true;
        }
		
		if(b->bytes_into_buffer< MinBytesNeed)
		{
		    if(b->bytes_into_buffer+inHeader->nFilledLen < MinBytesNeed){
		        memcpy(b->buffer+b->bytes_into_buffer, inHeader->pBuffer + inHeader->nOffset, inHeader->nFilledLen);
		        b->bytes_into_buffer = b->bytes_into_buffer + inHeader->nFilledLen;
		        inHeader->nFilledLen = 0;
		        inInfo->mOwnedByUs = false;
		        inQueue.erase(inQueue.begin());
		        inInfo = NULL;
		        notifyEmptyBufferDone(inHeader);
		        return;
		    }else{
		        int byte_need=MinBytesNeed-b->bytes_into_buffer;
		        memcpy(b->buffer+b->bytes_into_buffer, inHeader->pBuffer + inHeader->nOffset,byte_need);
		        b->bytes_into_buffer += byte_need;
		        inHeader->nOffset    += byte_need;
		        inHeader->nFilledLen -= byte_need;
		    }
		}

		void * sample_buffer = NeAACDecDecode(hDecoder, frameInfo,b->buffer, b->bytes_into_buffer);
		b->bytes_into_buffer = b->bytes_into_buffer - frameInfo->bytesconsumed;
		if (b->bytes_into_buffer > 0 )
		     memmove(b->buffer, (b->buffer + frameInfo->bytesconsumed),b->bytes_into_buffer);

		if (frameInfo->error > 0)
        {
            ALOGI("Decoder Error: %s\n", NeAACDecGetErrorMessage(frameInfo->error));
        }
		
		if ((frameInfo->error == 0) && (frameInfo->samples > 0))
        {
            short *sample_buffer16 = (short*)sample_buffer;
			char * data = (char *)outHeader->pBuffer;
			int i;
			
			for (i = 0; i < frameInfo->samples; i++)
			{
				data[i*2] = (char)(sample_buffer16[i] & 0xFF);
				data[i*2+1] = (char)((sample_buffer16[i] >> 8) & 0xFF);
				
			}

			outHeader->nFilledLen =  frameInfo->samples * 16 * sizeof(char)/8;
		}

		outHeader->nTimeStamp = inHeader->nTimeStamp;
        outHeader->nOffset = 0;
        outHeader->nFlags = 0;

		if(inHeader->nFilledLen==0){
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
    }
}

void SoftADIF::onPortFlushCompleted(OMX_U32 portIndex) 
{

    ALOGI("%s %d\n",__FUNCTION__,__LINE__);
    if(AdifHeaderSize>0)
    {
        NeAACDecClose(hDecoder);
        hDecoder = NeAACDecOpen();
        config = NeAACDecGetCurrentConfiguration(hDecoder);
        config->defSampleRate = 44100;
        config->defObjectType = LC;
        config->outputFormat = FAAD_FMT_16BIT;
        config->downMatrix = 1;
        config->useOldADTSFormat = 0;
        NeAACDecSetConfiguration(hDecoder, config);
        if(b!=NULL && b->buffer!=NULL ){
            memset(b->buffer, 0, FAAD_MIN_STREAMSIZE * AAC_CHANNELS* 2);
            b->bytes_into_buffer = 0;
            b->bytes_consumed = 0;
            b->file_offset = 0;
            b->at_eof = 0;
        }
        NeAACDecInit(hDecoder, pAdifHeader,AdifHeaderSize, (unsigned long *)(&mNumSampleRate), (unsigned char *)(&mNumChannels));
    }
}

void SoftADIF::onPortEnableCompleted(OMX_U32 portIndex, bool enabled) {
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
    return new android::SoftADIF(name, callbacks, appData, component);
}

