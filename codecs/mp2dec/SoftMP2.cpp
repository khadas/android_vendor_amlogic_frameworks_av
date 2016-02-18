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
#define LOG_TAG "SoftMP2"
#include <utils/Log.h>

#include "SoftMP2.h"

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

SoftMP2::SoftMP2(
        const char *name,
        const OMX_CALLBACKTYPE *callbacks,
        OMX_PTR appData,
        OMX_COMPONENTTYPE **component)
    : SimpleSoftOMXComponent(name, callbacks, appData, component),
      mAnchorTimeUs(0),
      mNumFramesOutput(0),
	  m_handle(NULL),
      mNumChannels(2),
      mSamplingRate(44100),
      mSignalledError(false),
      mOutputPortSettingsChange(NONE) {
    initPorts();
    initDecoder();
}

SoftMP2::~SoftMP2() {
	
	cleanup(m_handle);
}

void SoftMP2::initPorts() {
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
        const_cast<char *>(MEDIA_MIMETYPE_AUDIO_MPEG_LAYER_II);

    def.format.audio.pNativeRender = NULL;
    def.format.audio.bFlagErrorConcealment = OMX_FALSE;
    def.format.audio.eEncoding = OMX_AUDIO_CodingMP3;

    addPort(def);

    def.nPortIndex = 1;
    def.eDir = OMX_DirOutput;
    def.nBufferCountMin = kNumBuffers;
    def.nBufferCountActual = def.nBufferCountMin;
    def.nBufferSize = kOutputBufferSize;
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

void SoftMP2::initDecoder() {
	
	if(false == init_mpg123decoder()){
		cleanup(m_handle);
		return;
	}
}

OMX_ERRORTYPE SoftMP2::internalGetParameter(
        OMX_INDEXTYPE index, OMX_PTR params) {
    switch (index) {
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

            pcmParams->nChannels = mNumChannels;
            pcmParams->nSamplingRate = mSamplingRate;

            return OMX_ErrorNone;
        }

        case OMX_IndexParamAudioMp3:
        {
            OMX_AUDIO_PARAM_MP3TYPE *mp3Params =
                (OMX_AUDIO_PARAM_MP3TYPE *)params;

            if (mp3Params->nPortIndex > 1) {
                return OMX_ErrorUndefined;
            }

            mp3Params->nChannels = mNumChannels;
            mp3Params->nSampleRate = mSamplingRate;
            // other fields are encoder-only

            return OMX_ErrorNone;
        }

        default:
            return SimpleSoftOMXComponent::internalGetParameter(index, params);
    }
}

OMX_ERRORTYPE SoftMP2::internalSetParameter(
        OMX_INDEXTYPE index, const OMX_PTR params) {
    switch (index) {
        case OMX_IndexParamStandardComponentRole:
        {
            const OMX_PARAM_COMPONENTROLETYPE *roleParams =
                (const OMX_PARAM_COMPONENTROLETYPE *)params;

            if (strncmp((const char *)roleParams->cRole,
                        "audio_decoder.mp2",
                        OMX_MAX_STRINGNAME_SIZE - 1)) {
                return OMX_ErrorUndefined;
            }

            return OMX_ErrorNone;
        }

        case OMX_IndexParamAudioPcm:
        {
            const OMX_AUDIO_PARAM_PCMMODETYPE *pcmParams = (const OMX_AUDIO_PARAM_PCMMODETYPE *)params;

            if (pcmParams->nPortIndex != 1) {
                return OMX_ErrorUndefined;
            }

            mNumChannels = pcmParams->nChannels;
            mSamplingRate = pcmParams->nSamplingRate;

           return OMX_ErrorNone;
        }

        default:
            return SimpleSoftOMXComponent::internalSetParameter(index, params);
    }
}
void SoftMP2::onPortEnableCompleted(OMX_U32 portIndex, bool enabled) {
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
void SoftMP2::onQueueFilled(OMX_U32 portIndex) {
    if (mSignalledError || mOutputPortSettingsChange != NONE) {
        return;
    }

    List<BufferInfo *> &inQueue = getPortQueue(0);
    List<BufferInfo *> &outQueue = getPortQueue(1);

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
            mNumFramesOutput = 0;
        }
		
		const OMX_U8 * inputptr = inHeader->pBuffer + inHeader->nOffset;
		size_t size;
		/* Feed input chunk and get first chunk of decoded audio. */
		int ret = mpg123_decode(m_handle, inputptr, inHeader->nFilledLen, outHeader->pBuffer,kOutputBufferSize,&size);
		if(1/*ret == MPG123_NEW_FORMAT*/)
		{
			long rate;
			int channels, enc;
			mpg123_getformat(m_handle, &rate, &channels, &enc);
			if (rate != mSamplingRate || channels!= mNumChannels) {
            	mSamplingRate = rate;
           		mNumChannels = channels;
            	notify(OMX_EventPortSettingsChanged, 1, 0, NULL);
            	mOutputPortSettingsChange = AWAITING_DISABLED;
            	return;
        	}
		}

		while(ret != MPG123_ERR && ret != MPG123_NEED_MORE)
		{ /* Get all decoded audio that is available now before feeding more input. */
			ret = mpg123_decode(m_handle, NULL, 0, outHeader->pBuffer, kOutputBufferSize,&size);
			//write(1,out,size);
			//outc += size;
		}
		if(ret == MPG123_ERR){ 
			ALOGI("some error: %s", mpg123_strerror(m_handle)); 
			return; 
		}

		outHeader->nFilledLen = size;
        outHeader->nOffset = 0;

        outHeader->nTimeStamp =
            mAnchorTimeUs
                + (mNumFramesOutput * 1000000ll) / mSamplingRate; 

        outHeader->nFlags = 0;

		inHeader->nFilledLen = 0;
		if (inHeader->nFilledLen == 0) {
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

bool SoftMP2::init_mpg123decoder()
{
	int  err  = MPG123_ERR;
	if(m_handle != NULL){
		cleanup(m_handle);
		m_handle = NULL;
	}


	
	if((mpg123_init() != MPG123_OK)||((m_handle= mpg123_new(NULL, &err)) == NULL))
	{
		return false;
	}

	mpg123_param(m_handle, MPG123_VERBOSE, 2, 0); /* Brabble a bit about the parsing/decoding. */

	mpg123_open_feed(m_handle);
	if(m_handle == NULL){
		return false;
	}
	return true;
}
void SoftMP2::cleanup(mpg123_handle * handle)
{
	mpg123_close(handle);
	mpg123_delete(handle);
	mpg123_exit();
}
}  // namespace android

android::SoftOMXComponent *createSoftOMXComponent(
        const char *name, const OMX_CALLBACKTYPE *callbacks,
        OMX_PTR appData, OMX_COMPONENTTYPE **component) {
    return new android::SoftMP2(name, callbacks, appData, component);
}
