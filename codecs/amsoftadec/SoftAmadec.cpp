/*
 * Copyright (C) 2010 Amlogic Corporation.
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



/*****************************************************************
 *
 *           amlogic soft audio decoder implement
 *
 *
 *****************************************************************/

//#define LOG_NDEBUG 0
#define LOG_TAG "SoftAmadec"
#include <utils/Log.h>

#include "AmFFmpegCodec.h"
#include "SoftAmadec.h"
#include <stdio.h>
#include <stdlib.h>

#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/AmMediaDefsExt.h>
#include <media/IOMX.h>
#include <AmFFmpegUtils.h>
#include <OMX_IndexExt.h>
#include <OMX_AudioExt.h>

//#define COM_DEBUG
#ifdef COM_DEBUG
#define Trace(...) ALOGI(__VA_ARGS__)
#else
#define Trace(...)
#endif


#define OMX_MAX_COMPONENT_STRING_SIZE 64

namespace android {

/**
  * need to support more mime, just add here
**/
static const char * ConvertAudioRoleToMime(const char * role) {
    CHECK(role);
    if (!strcasecmp(role, "ffmpeg")) {
        return const_cast<char *>(MEDIA_MIMETYPE_AUDIO_FFMPEG);
    } else {
        ALOGE("Not support %s yet, need to add audio format!\n",role);
        return NULL;
    }
}

static OMX_AUDIO_CODINGTYPE FindMatchingOMXAudioType(const char * mime) {
    if (mime && !strcmp(mime, MEDIA_MIMETYPE_AUDIO_FFMPEG)) {
        return (OMX_AUDIO_CODINGTYPE)OMX_AUDIO_CodingFFMPEG;
    } else {
        ALOGE("omx audio coding type is unused.%s",mime);
        return OMX_AUDIO_CodingUnused;
    }
}

template<class T>
static void InitOMXParams(T *params) {
    params->nSize = sizeof(T);
    params->nVersion.s.nVersionMajor = 1;
    params->nVersion.s.nVersionMinor = 0;
    params->nVersion.s.nRevision = 0;
    params->nVersion.s.nStep = 0;
}

SoftAmadec::SoftAmadec(
    const char *name,
    const OMX_CALLBACKTYPE *callbacks,
    OMX_PTR appData,
    OMX_COMPONENTTYPE **component)
    : SimpleSoftOMXComponent(name, callbacks, appData, component),
      mAInfo(NULL),
      setflag(false),
      mOutputPortSettingsChange(NONE) {
    int32_t len = strlen(name);
    int32_t len1 = strlen("OMX.google.");
    int32_t len2 = strlen(".decoder");
    char role[OMX_MAX_COMPONENT_STRING_SIZE] = {0};
    strncpy(role, name+len1, len-len1-len2);
    memset(mComponentRole, 0, sizeof(mComponentRole));
    sprintf(mComponentRole, "%s%s", "audio_decoder.", role);
    Trace("role: %s\n", mComponentRole);
    mMimeType = ConvertAudioRoleToMime(role);
    Trace("get mimetype : %s\n", mMimeType);
    initPorts();
    CHECK_EQ(initDecoder(), (status_t)OK);
}

SoftAmadec::~SoftAmadec() {
    delete mAInfo;
    mAInfo = NULL;
    mCodec->decode_close();
    mCodec = NULL;
}

void SoftAmadec::initPorts() {
    OMX_PARAM_PORTDEFINITIONTYPE def;
    InitOMXParams(&def);

    def.nPortIndex = 0;
    def.eDir = OMX_DirInput;
    def.nBufferCountMin = kInputBuffersNum;
    def.nBufferCountActual = def.nBufferCountMin;
    def.nBufferSize = 256*1024;
    def.bEnabled = OMX_TRUE;
    def.bPopulated = OMX_FALSE;
    def.eDomain = OMX_PortDomainAudio;
    def.bBuffersContiguous = OMX_FALSE;
    def.nBufferAlignment = 1;

    def.format.audio.cMIMEType = const_cast<char *>(mMimeType);
    def.format.audio.pNativeRender = NULL;
    def.format.audio.bFlagErrorConcealment = OMX_FALSE;
    def.format.audio.eEncoding = FindMatchingOMXAudioType(mMimeType);

    addPort(def);

    def.nPortIndex = 1;
    def.eDir = OMX_DirOutput;
    def.nBufferCountMin = kOutputBuffersNum;
    def.nBufferCountActual = def.nBufferCountMin;
    def.nBufferSize = 512*1024;
    def.bEnabled = OMX_TRUE;
    def.bPopulated = OMX_FALSE;
    def.eDomain = OMX_PortDomainAudio;
    def.bBuffersContiguous = OMX_FALSE;
    def.nBufferAlignment = 2;

    def.format.audio.cMIMEType = const_cast<char *>(MEDIA_MIMETYPE_VIDEO_RAW);
    def.format.audio.pNativeRender = NULL;
    def.format.audio.bFlagErrorConcealment = OMX_FALSE;
    def.format.audio.eEncoding = OMX_AUDIO_CodingPCM;

    addPort(def);
}

status_t SoftAmadec::initDecoder() {
    status_t ret = NO_INIT;
    mAInfo = new AUDIO_INFO_T;
    if (mAInfo != NULL) {
        mCodec = AmFFmpegCodec::CreateCodec(mMimeType);
        if (mCodec != NULL)
            ret = OK;
    } else {
        ALOGE("error: allocate memory failed for audio info\n");
    }

    return ret;
}

OMX_ERRORTYPE SoftAmadec::internalGetParameter(
    OMX_INDEXTYPE index, OMX_PTR params) {
    switch (index) {
    case OMX_IndexParamAudioPcm:
    {
        Trace("internalGetParameter:OMX_IndexParamAudioPcm");
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
        pcmParams->eChannelMapping[2] = OMX_AUDIO_ChannelCF;
        pcmParams->eChannelMapping[3] = OMX_AUDIO_ChannelLFE;
        pcmParams->eChannelMapping[4] = OMX_AUDIO_ChannelLS;
        pcmParams->eChannelMapping[5] = OMX_AUDIO_ChannelRS;
        if (mAInfo->channels > 0 && mAInfo->channels <=8 )
            pcmParams->nChannels = mAInfo->channels;
        else
            pcmParams->nChannels = 2;
        if (mAInfo->samplerate > 0 && mAInfo->samplerate <= 192000)
            pcmParams->nSamplingRate = mAInfo->samplerate;
         else
            pcmParams->nSamplingRate = 48000;

        return OMX_ErrorNone;
    }

    default:
        return SimpleSoftOMXComponent::internalGetParameter(index, params);
    }
}

OMX_ERRORTYPE SoftAmadec::internalSetParameter(
    OMX_INDEXTYPE index, const OMX_PTR params) {
    switch (index) {
    case OMX_IndexParamStandardComponentRole:
    {
        Trace("internalSetParameter: OMX_IndexParamStandardComponentRole");
        const OMX_PARAM_COMPONENTROLETYPE *roleParams =
            (const OMX_PARAM_COMPONENTROLETYPE *)params;
        if (strncmp((const char *)roleParams->cRole,
                    mComponentRole,
                    OMX_MAX_STRINGNAME_SIZE - 1)) {
            return OMX_ErrorUndefined;
        }
        return OMX_ErrorNone;
    }

    case OMX_IndexParamAudioFFmpeg:
    {
        Trace("internalSetParameter: OMX_IndexParamAudioFFmpeg");
        OMX_AUDIO_PARAM_FFMPEGTYPE *FFmpegParams =
            (OMX_AUDIO_PARAM_FFMPEGTYPE *)params;

        if (FFmpegParams->nPortIndex != 0) {
            return OMX_ErrorUndefined;
        }

        if (FFmpegParams->nChannels < 1 || FFmpegParams->nChannels > 8) {
            ALOGE("NOTE:(cook) no support current channels(%d).\n", (int)FFmpegParams->nChannels);
            return OMX_ErrorUndefined;
        }

        if ((int)FFmpegParams->nBlockAlign < 0) {
            ALOGE("NOTE:(cook) block_align not valid: %d\n", (int)FFmpegParams->nBlockAlign);
            return OMX_ErrorUndefined;
        }

        mAInfo->channels = FFmpegParams->nChannels;
        mAInfo->samplerate = FFmpegParams->nSamplingRate;
        mAInfo->blockalign = FFmpegParams->nBlockAlign;
        mAInfo->bitrate = FFmpegParams->nBitRate;
        mAInfo->codec_id = FFmpegParams->nCodecID;
        mAInfo->extradata_size = FFmpegParams->nExtraData_Size;
        if ( mAInfo->codec_id == AV_CODEC_ID_ADPCM_IMA_WAV ||
            mAInfo->codec_id == AV_CODEC_ID_ADPCM_MS)
            mAInfo->bitspersample = 4;
        if (mAInfo->extradata_size > 0)
            memcpy((char *)mAInfo->extradata, (char *)FFmpegParams->nExtraData, mAInfo->extradata_size);
        setflag = true;
        status_t  ret = mCodec->audio_decode_init(mMimeType, mAInfo);
        CHECK_EQ(ret, (status_t)OK);
        return OMX_ErrorNone;
    }

    default:
        return SimpleSoftOMXComponent::internalSetParameter(index, params);
    }
}
void SoftAmadec::onQueueFilled(OMX_U32 portIndex) {
    if (mOutputPortSettingsChange != NONE || !setflag) {
        Trace("mOutputPortSettingsChange or setflag is false.\n");
        return;
    }

    List<BufferInfo *> &inQueue = getPortQueue(0);
    List<BufferInfo *> &outQueue = getPortQueue(1);

    AUDIO_FRAME_WRAPPER_T * frame = (AUDIO_FRAME_WRAPPER_T *)malloc(sizeof(AUDIO_FRAME_WRAPPER_T));
    PACKET_WRAPPER_T pkt;
    mCodec->audio_decode_alloc_frame();
    if (!frame) {
        ALOGE("Could not allocate audio frame!\n");
        return;
    }
    //memset(frame, 0, sizeof(AUDIO_FRAME_WRAPPER_T));
    int32_t got_picture = 0;

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
            free(frame);
            return;
        }

        pkt.data = inHeader->pBuffer + inHeader->nOffset;
        pkt.size  = inHeader->nFilledLen;
        outHeader->nOffset = 0;
        outHeader->nFilledLen=0;
        uint8_t *dst;
		if (mAInfo->codec_id == AV_CODEC_ID_PCM_BLURAY) {
           uint8_t header[4];
           uint8_t bps = 0;
           uint8_t bits_per_samples[4] = { 0, 16, 20, 24 };
           int tmp_buf[10] = {0};
           int i ,j;
           short *sample;
           for (i = 0;i < 4 ;i++)
               header [i] = pkt.data [i];
           /* get the sample depth and derive the sample format from it */
           bps = bits_per_samples[header[3] >> 6];
           pkt.data = pkt.data + 4;
           pkt.size = pkt.size - 4;
           sample = (short *)pkt.data;
           if (bps == 16) {
                if (mAInfo->channels == 1) {
                    for (i = 0, j = 0; i < pkt.size;) {
                        sample[j + 1] = sample[j] = (pkt.data[i] << 8) | pkt.data[i + 1];
                        i += 2;
                        j += 2;
                    }
                } else if (mAInfo->channels == 2) {
                    for (i = 0, j = 0; i < pkt.size;) {
                        sample[j++] = (pkt.data[i] << 8) | pkt.data[i + 1];
                        i += 2;
                        sample[j++] = (pkt.data[i] << 8) | pkt.data[i + 1];
                        i += 2;
                    }
                } else if (mAInfo->channels > 2 && mAInfo->channels <= 8) {
                    int k;
                    memset(tmp_buf, 0, sizeof(tmp_buf));
                    for (i = 0, j = 0; i < pkt.size;) {
                        for (k = 0; k < mAInfo->channels; k++) {
                            tmp_buf[k] = (int16_t)((pkt.data[i] << 8) | pkt.data[i + 1]);
                            i += 2;
                        }
                        //LoRo downmix:L R C Ls Rs LFE  Lsr Rsr
                        sample[j++] = av_clip_int16(tmp_buf[0] + 0.707f * tmp_buf[2] + 0.707f * (tmp_buf[3] + tmp_buf[6])); //Lo=L+0.707C+0.707 (Ls+Lsr)
                        sample[j++] = av_clip_int16(tmp_buf[1] + 0.707f * tmp_buf[2] + 0.707f * (tmp_buf[4] + tmp_buf[7])); //Ro=R+0.707C+0.707(Rs+Rsr)
                    }
                }
           } else if (bps == 24 || bps == 20) {
                int k;
                memset(tmp_buf, 0, sizeof(tmp_buf));
                if (mAInfo->channels == 1) {
                    for (i = 0, j = 0; i < pkt.size;) {
                        sample[j++] = (pkt.data[i] << 8) | pkt.data[i + 1];
                        i += 3;
                    }
                } else if (mAInfo->channels == 2) {
                    for (i = 0, j = 0; i < pkt.size;) {
                        sample[j++] = (pkt.data[i] << 8) | pkt.data[i + 1];
                        i += 3;
                        sample[j++] = (pkt.data[i] << 8) | pkt.data[i + 1];
                        i += 3;
                    }
                } else if (mAInfo->channels >= 2 && mAInfo->channels <= 8) {
                    int k;
                    memset(tmp_buf, 0, sizeof(tmp_buf));
                    for (i = 0, j = 0; i < pkt.size;) {
                        for (k = 0; k < mAInfo->channels; k++) {
                            tmp_buf[k] = (int16_t)(pkt.data[i] << 8) | pkt.data[i + 1];
                            i += 3;
                        }
                        //LoRo downmix:L R C Ls Rs LFE  Lsr Rsr
                        sample[j++] = av_clip_int16(tmp_buf[0] + 0.707f * tmp_buf[2] + 0.707f * (tmp_buf[3] + tmp_buf[6])); //Lo=L+0.707C+0.707 (Ls+Lsr)
                        sample[j++] = av_clip_int16(tmp_buf[1] + 0.707f * tmp_buf[2] + 0.707f * (tmp_buf[4] + tmp_buf[7])); //Ro=R+0.707C+0.707(Rs+Rsr)
                    }
                }
           } else {
               ALOGI("[%s %d]blueray pcm is %d bps, don't process now\n", __FUNCTION__, __LINE__, bps);
           }
           dst = outHeader->pBuffer+outHeader->nFilledLen;
           memcpy((char *)dst, (char *)pkt.data, 2*j);
           outHeader->nFilledLen += 2 *j;
        } else {
            do {
                memset(frame, 0, sizeof(AUDIO_FRAME_WRAPPER_T));
                int32_t len = mCodec->audio_decode_frame(frame, &got_picture, &pkt);
                if (len < 0 || !got_picture) {
                    ALOGE("Cannot decode this frame! len/%d got_picture/%d\n",len,got_picture);
                    break;
                }
                pkt.data +=len;
                pkt.size -=len;
                if (frame->channels > 0 && frame->channels <= 8 && frame->samplerate > 0 && frame->samplerate <=192000) {
                    if (mAInfo->samplerate != frame->samplerate ||mAInfo->channels != frame->channels) {
                        mAInfo->samplerate = frame->samplerate;
                        mAInfo->channels = frame->channels;
                        notify(OMX_EventPortSettingsChanged, 1, 0, NULL);
                        mOutputPortSettingsChange = AWAITING_DISABLED;
                        free(frame);
                        return;
                    }
                }
                dst = outHeader->pBuffer+outHeader->nFilledLen;
                if (frame->datasize > 512 * 1024) {
                    frame->datasize = 512 * 1024;
                    ALOGI("framesize %d exceed the bufsize 512* 1024",frame->datasize);
                }
                memcpy((char *)dst, (char *)frame->data, frame->datasize);
                outHeader->nFilledLen += frame->datasize;
            } while( pkt.size>0);
        }
#if 0
        FILE *fp = NULL;
        fp = fopen("/data/pcm", "ab+");
        if (fp != NULL) {
            if (fwrite((char *)frame->data, 1, (size_t)frame->datasize, fp)
                    != (size_t)frame->datasize)
                ALOGI("lose pcm data.");
            fclose(fp);
        }
        else {
            ALOGI("open data/pcm failed.");
        }
#endif
        outHeader->nFlags = 0;
        outHeader->nTimeStamp = inHeader->nTimeStamp;
        outInfo->mOwnedByUs = false;
        outQueue.erase(outQueue.begin());
        outInfo = NULL;
        notifyFillBufferDone(outHeader);
        outHeader = NULL;

        inInfo->mOwnedByUs = false;
        inQueue.erase(inQueue.begin());
        inInfo = NULL;
        notifyEmptyBufferDone(inHeader);
        inHeader = NULL;
    }
    mCodec->audio_decode_free_frame();
    free(frame);
}

void SoftAmadec::onPortFlushCompleted(OMX_U32 portIndex) {
}

void SoftAmadec::onPortEnableCompleted(OMX_U32 portIndex, bool enabled) {
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
    return new android::SoftAmadec(name, callbacks, appData, component);
}
