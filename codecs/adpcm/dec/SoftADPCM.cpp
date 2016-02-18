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
 *
 *Note: The SoftADPCM code is added to stagefright by zedong.xiong, Amlogic Inc. 
 *          2012.9.25
 */

#define LOG_TAG "SoftADPCM"
#include <utils/Log.h>

#include "SoftADPCM.h"

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

SoftADPCM::SoftADPCM(
        const char *name,
        const OMX_CALLBACKTYPE *callbacks,
        OMX_PTR appData,
        OMX_COMPONENTTYPE **component)
    : SimpleSoftOMXComponent(name, callbacks, appData, component),
	  mIsIma(true),
	  mNumChannels(2),
	  mSampleRate(8000),
	  mBlockAlign(0),
	  mOutputPortSettingsChange(NONE),
      mSignalledError(false) {
	  if (!strcmp(name, "OMX.google.adpcm.ms.decoder")) {
			mIsIma = false;
	  } else {
			CHECK(!strcmp(name, "OMX.google.adpcm.ima.decoder"));
	  }
      initPorts();
}

SoftADPCM::~SoftADPCM() {
}

void SoftADPCM::initPorts() {
	
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
        const_cast<char *>(
                mIsIma
                    ? MEDIA_MIMETYPE_AUDIO_ADPCM_IMA
                    : MEDIA_MIMETYPE_AUDIO_ADPCM_MS);

	def.format.audio.pNativeRender = NULL;
    def.format.audio.bFlagErrorConcealment = OMX_FALSE;
    def.format.audio.eEncoding = OMX_AUDIO_CodingADPCM;

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

OMX_ERRORTYPE SoftADPCM::internalGetParameter(
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
            pcmParams->nSamplingRate = mSampleRate;

            return OMX_ErrorNone;
        }

        default:
            return SimpleSoftOMXComponent::internalGetParameter(index, params);
    }
}

OMX_ERRORTYPE SoftADPCM::internalSetParameter(
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

						if (pcmParams->nBlockAlign < 4) {
							ALOGI("adpcm: block_align not valid: %d\n", pcmParams->nBlockAlign);
							return OMX_ErrorUndefined;
						}

            mNumChannels = pcmParams->nChannels;
						mSampleRate = pcmParams->nSamplingRate;
						mBlockAlign = pcmParams->nBlockAlign;
            return OMX_ErrorNone;
        }

        case OMX_IndexParamStandardComponentRole:
        {
            const OMX_PARAM_COMPONENTROLETYPE *roleParams =
                (const OMX_PARAM_COMPONENTROLETYPE *)params;

            if (mIsIma) {
                if (strncmp((const char *)roleParams->cRole,
                            "audio_decoder.adpcmima",
                            OMX_MAX_STRINGNAME_SIZE - 1)) {
                    return OMX_ErrorUndefined;
                }
            } else {
                if (strncmp((const char *)roleParams->cRole,
                            "audio_decoder.adpcmms",
                            OMX_MAX_STRINGNAME_SIZE - 1)) {
                    return OMX_ErrorUndefined;
                }
            }

            return OMX_ErrorNone;
        }

        default:
            return SimpleSoftOMXComponent::internalSetParameter(index, params);
    }
}

void SoftADPCM::onQueueFilled(OMX_U32 portIndex) {
	
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

				if (inHeader->nFilledLen > mBlockAlign) {
            ALOGI("adpcm: input buffer too large (%ld).", inHeader->nFilledLen);

            notify(OMX_EventError, OMX_ErrorUndefined, 0, NULL);
            mSignalledError = true;
        }

				const uint8_t *inputptr = inHeader->pBuffer + inHeader->nOffset;

				if (mIsIma) {
            if(false == ima_decoder(outHeader->pBuffer, &(outHeader->nFilledLen),inputptr, inHeader->nFilledLen)){
							ALOGI("adpcm: ima_decoder failed!\n");
							return;
						}
        } else {
            if(false == ms_decoder(outHeader->pBuffer, &(outHeader->nFilledLen),inputptr, inHeader->nFilledLen)){
						ALOGI("adpcm: ms_decoder failed!\n");
						return;
					}
        }	
        outHeader->nTimeStamp = inHeader->nTimeStamp;
        outHeader->nOffset = 0;
        outHeader->nFlags = 0;

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
       
    }
}

bool SoftADPCM::ms_decoder(unsigned char *out, OMX_U32 * outlen,  const uint8_t *in, size_t inSize)
{
	ALOGI("adpcm: ms_decoder\n");

	short * pcm_buf;
	int Output_Size = 0;
		
	if(inSize > 0)
	{
		ALOGI("adpcm: inHeader->nFilledLen = %d\n",inSize);
		
		pcm_buf = (short *)malloc(mBlockAlign*4*4);
		if(pcm_buf == NULL){
			ALOGI("ms_decoder: malloc pcm_buf failed!\n");
			return false;
		}

		if(inSize < mBlockAlign){
			ALOGI("ms_decoder: data missalign\n");
		}
		
		ALOGI("ms_decoder: Output_Size = %d\n",(Output_Size = ms_decode_block(pcm_buf, in, mNumChannels, mBlockAlign)));
		Output_Size = Output_Size - Output_Size % mNumChannels;
		memcpy(out, (char*)pcm_buf, 2 * Output_Size);
		(*outlen) = Output_Size * 2;
		
		if(pcm_buf != NULL ){
			free(pcm_buf);
			pcm_buf = NULL;
		}
		
		return true;
		
	}else{
		ALOGI("ms_decoder: inHeader->nFilledLen<=0 failed!\n");
		return false;
	}
	
}

bool SoftADPCM::ima_decoder(unsigned char *out, OMX_U32 * outlen,  const uint8_t *in, size_t inSize)
{	
	ALOGI("adpcm: ima_decoder\n");

	short * pcm_buf;
	int Output_Size = 0;
	
	if(inSize > 0)
	{
		ALOGI("adpcm: inHeader->nFilledLen = %d\n",inSize);

		pcm_buf = (short *)malloc(mBlockAlign*4*4);
		if(pcm_buf == NULL){
			ALOGI("ima_decoder: malloc pcm_buf failed!\n");
			return false;
		}

		if(inSize < mBlockAlign){
			ALOGI("ima_decoder: data missalign\n");
		}
		
		ALOGI("ima_decoder: Output_Size = %d\n",(Output_Size = ima_decode_block((unsigned short *)pcm_buf, in, mNumChannels, mBlockAlign)));
		memcpy(out, (char*)pcm_buf, 2*Output_Size);
		(*outlen) = Output_Size * 2;
		
		if(pcm_buf != NULL ){
			free(pcm_buf);
			pcm_buf = NULL;
		}
	
		return true;

	}else{
		ALOGI("ima_decoder: inHeader->nFilledLen<=0 failed!\n");
		return false;
	}
	
}


int SoftADPCM::ms_decode_block(short *pcm_buf,const unsigned char *buf, int channel, int block)
{
	
	int sampleblk = 2036;
	short bpred[2];
	short idelta[2];
	int blockindx=0;
	int sampleindx=0;
	short bytecode = 0;
	int predict = 0;
	int current = 0;
	int delta = 0;
	int i = 0;
	int j = 0;
	short s0 = 0;
	short s1 = 0;
	short s2 = 0;
	short s3 = 0;
	short s4 = 0;
	short s5 = 0;
	
	j = 0;
	if(channel==1)
	{
		bpred[0] = buf[0];
		bpred[1] = 0;
		if(bpred[0]>=7)
		{
			//do nothing
		}
		idelta[0] = buf[1]|buf[2]<<8;
		idelta[1] = 0;
	
		s1 = buf[3]|buf[4]<<8;		  
		s0 = buf[5]|buf[6]<<8;
	
		blockindx = 7;
		sampleindx = 2;
	}
	else if(channel==2)
	{
		bpred[0] = buf[0];
		bpred[1] = buf[1];
		if(bpred[0]>=7 || bpred[1]>=7)
		{		 
			//do nothing
		}
		idelta[0] = buf[2]|buf[3]<<8;
		idelta[1] = buf[4]|buf[5]<<8;
	 
		s2 = buf[6]|buf[7]<<8;
		s3 = buf[8]|buf[9]<<8;
		s0 = buf[10]|buf[11]<<8;
		s1 = buf[12]|buf[13]<<8;
		blockindx = 14;
		sampleindx = 4;
	}
	
		/*--------------------------------------------------------
		This was left over from a time when calculations were done
		as ints rather than shorts. Keep this around as a reminder
		in case I ever find a file which decodes incorrectly.
		
		  if (chan_idelta [0] & 0x8000)
		  chan_idelta [0] -= 0x10000 ;
		  if (chan_idelta [1] & 0x8000)
		  chan_idelta [1] -= 0x10000 ;
		--------------------------------------------------------*/
		
		/* Pull apart the packed 4 bit samples and store them in their
		** correct sample positions.
		*/
	
		/* Decode the encoded 4 bit samples. */
		int chan;
		
		for(i=channel*2; (blockindx < block);i++)
		{
			if(sampleindx<=i)
			{
				if(blockindx<block)
				{
					bytecode = buf[blockindx++];
	
			  
					if(channel==1)
					{
						s2 = (bytecode>>4)&0x0f;
						s3 = bytecode&0x0f;
					}
					else if(channel==2)
					{
						s4 = (bytecode>>4)&0x0f;
						s5 = bytecode&0x0f;
					}
					sampleindx++;
					sampleindx++;
	
				}
			}
			chan = (channel>1)?(i%2):0;
	   
			if(channel==1)
			{
				bytecode = s2&0x0f;
			}
			else if(channel==2)
			{
				bytecode = s4&0x0f;
			}
			/* Compute next Adaptive Scale Factor (ASF) */
			delta = idelta[chan];
	
			/* => / 256 => FIXED_POINT_ADAPTATION_BASE == 256 */
			idelta[chan] = (AdaptationTable[bytecode]*delta)>>8;
	
			if(idelta[chan]<16)
				idelta[chan]=16;
			if(bytecode&0x8)
				bytecode-=0x10;
			 /* => / 256 => FIXED_POINT_COEFF_BASE == 256 */
	
			if(channel==1)
			{
				predict = s1*AdaptCoeff1[bpred[chan]];
				predict+= s0*AdaptCoeff2[bpred[chan]];
			}
			else if(channel==2)
			{
				predict = s2*AdaptCoeff1[bpred[chan]];
				predict+= s0*AdaptCoeff2[bpred[chan]];
			}
	
			predict>>=8;
			current = bytecode*delta+predict;		 
#if 1        
			if (current > 32767)
				current = 32767 ;
			else if (current < -32768)
				current = -32768 ;
#else
			current = _min(current, 32767);
			current = _max(current, -32768);
#endif
			if(channel==1)
			{
				s2 = current;
					}
			else if(channel==2)
			{
				s4 = current;
					}
	 
			pcm_buf[j++] = s0;
	
			if(channel==1)
			{
				s0 = s1;
				s1 = s2;
				s2 = s3;
			}
			else if(channel==2)
			{
				s0 = s1;
				s1 = s2;
				s2 = s3;
				s3 = s4;
				s4 = s5;
			}
		}
	
		if(channel==1)
		{
			pcm_buf[j++] = s0;
			pcm_buf[j++] = s1;
			}
		else if(channel==2)
		{
			pcm_buf[j++] = s0;
			pcm_buf[j++] = s1;
			pcm_buf[j++] = s2;
			pcm_buf[j++] = s3;
		}
	   
		return j;
}

	

int SoftADPCM::ima_decode_block(unsigned short *output, const unsigned char *input, int channels, int block_size)
{

  int predictor_l = 0;
  int predictor_r = 0;
  int index_l = 0;
  int index_r = 0;
  int i;
  int channel_counter;
  int channel_index;
  int channel_index_l;
  int channel_index_r;

  predictor_l = LE_16(&input[0]);
  SE_16BIT(predictor_l);
  index_l = input[2];
  if (channels == 2)
  {
    predictor_r = LE_16(&input[4]);
    SE_16BIT(predictor_r);
    index_r = input[6];
  }

  if (channels == 1)
    for (i = 0;
      i < (block_size - MS_IMA_ADPCM_PREAMBLE_SIZE * channels); i++)
    {
      output[i * 2 + 0] = input[MS_IMA_ADPCM_PREAMBLE_SIZE + i] & 0x0F;
      output[i * 2 + 1] = input[MS_IMA_ADPCM_PREAMBLE_SIZE + i] >> 4;
    }
  else
  {
    // encoded as 8 nibbles (4 bytes) per channel; switch channel every
    // 4th byte
    channel_counter = 0;
    channel_index_l = 0;
    channel_index_r = 1;
    channel_index = channel_index_l;
    for (i = 0;
      i < (block_size - MS_IMA_ADPCM_PREAMBLE_SIZE * channels); i++)
    {
      output[channel_index + 0] =
        input[MS_IMA_ADPCM_PREAMBLE_SIZE * 2 + i] & 0x0F;
      output[channel_index + 2] =
        input[MS_IMA_ADPCM_PREAMBLE_SIZE * 2 + i] >> 4;
      channel_index += 4;
      channel_counter++;
      if (channel_counter == 4)
      {
        channel_index_l = channel_index;
        channel_index = channel_index_r;
      }
      else if (channel_counter == 8)
      {
        channel_index_r = channel_index;
        channel_index = channel_index_l;
        channel_counter = 0;
      }
    }
  }
  
  decode_nibbles(output,
    (block_size - MS_IMA_ADPCM_PREAMBLE_SIZE * channels) * 2,
    channels,
    predictor_l, index_l,
    predictor_r, index_r);

  return (block_size - MS_IMA_ADPCM_PREAMBLE_SIZE * channels) * 2;
}



 void SoftADPCM::decode_nibbles(unsigned short *output,
  int output_size, int channels,
  int predictor_l, int index_l,
  int predictor_r, int index_r)
{

  int step[2];
  int predictor[2];
  int index[2];
  int diff;
  int i;
  int sign;
  int delta;
  int channel_number = 0;

  step[0] = adpcm_step[index_l];
  step[1] = adpcm_step[index_r];
  predictor[0] = predictor_l;
  predictor[1] = predictor_r;
  index[0] = index_l;
  index[1] = index_r;

  for (i = 0; i < output_size; i++)
  {
    delta = output[i];

    index[channel_number] += adpcm_index[delta];
    CLAMP_0_TO_88(index[channel_number]);

    sign = delta & 8;
    delta = delta & 7;

    diff = step[channel_number] >> 3;
    if (delta & 4) diff += step[channel_number];
    if (delta & 2) diff += step[channel_number] >> 1;
    if (delta & 1) diff += step[channel_number] >> 2;

    if (sign)
      predictor[channel_number] -= diff;
    else
      predictor[channel_number] += diff;

    CLAMP_S16(predictor[channel_number]);
    output[i] = predictor[channel_number];
    step[channel_number] = adpcm_step[index[channel_number]];

    // toggle channel
    channel_number ^= channels - 1;

  }

}
void SoftADPCM::onPortEnableCompleted(OMX_U32 portIndex, bool enabled) {
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
    return new android::SoftADPCM(name, callbacks, appData, component);
}

