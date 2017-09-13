/*
 * Copyright (C) 2012 The Android Open Source Project
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

 //TODO: audio codec
 //done

#define LOG_NDEBUG 0
#define LOG_TAG "AmAudioCodec"
#include <utils/Log.h>

#include "codec/AmAudioCodec.h"
#include "AmFFmpegUtils.h"

#include <media/stagefright/foundation/ADebug.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/internal.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
}

//#define ACODEC_DEBUG
#ifdef ACODEC_DEBUG
#define Trace(...) ALOGI(__VA_ARGS__) 
#else
#define Trace(...)
#endif

namespace android{
AmAudioCodec::AmAudioCodec()
    :mctx(NULL),
     mCodec(NULL),
     mFrame(NULL)
{
    avcodec_register_all();
}

AmAudioCodec::~AmAudioCodec()
{
    decode_close();
}

int32_t AmAudioCodec::audio_decode_init(const char * codecMime, AUDIO_INFO_T *ainfo)
{
    AVCodecID id = static_cast<AVCodecID>(ainfo->codec_id);
    mCodec = avcodec_find_decoder(id);
    Trace("audio:codec name: %s\n", mCodec->name);
    if(!mCodec) {
        ALOGE("%s decoder not found!!\n", codecMime);
        return UNKNOWN_ERROR;
    }

    mctx = avcodec_alloc_context3(mCodec);
    if(!mctx) {
        ALOGE("malloc codec context failed!!\n");
        return UNKNOWN_ERROR;
    }

    Trace("========get audio info==========\n");
    Trace("extradata_size: %d\n", ainfo->extradata_size);
    Trace("0x%x,0x%x,0x%x,0x%x\n",ainfo->extradata[0],ainfo->extradata[1], ainfo->extradata[2],ainfo->extradata[3]);
    Trace("blockalign: %d\n", ainfo->blockalign);
    Trace("channels:%d,sr:%d,bitrate:%d\n", ainfo->channels, ainfo->samplerate, ainfo->bitrate);

    if(mctx->extradata_size <= 0){
        mctx->extradata_size = ainfo->extradata_size;
        mctx->extradata = (uint8_t*)malloc(mctx->extradata_size *sizeof(uint8_t));
        if(mctx->extradata != NULL){
            memcpy((char *)mctx->extradata, (char *)ainfo->extradata, mctx->extradata_size);
        }else{
            ALOGE("error:[%s,%d]malloc memory failed for extradata.\n", __FUNCTION__, __LINE__);
        }
    }
    if(mctx->block_align <= 0)
        mctx->block_align = ainfo->blockalign;
    mctx->channels = ainfo->channels;
    mctx->sample_rate = ainfo->samplerate;
    mctx->bit_rate = ainfo->bitrate;
    mctx->bits_per_coded_sample = ainfo->bitspersample;
    Trace("=========init audio info==========\n");
    Trace("extradata_size: %d\n", mctx->extradata_size);
    Trace("0x%x,0x%x,0x%x,0x%x\n",mctx->extradata[0],mctx->extradata[1], mctx->extradata[2],mctx->extradata[3]);
    Trace("blockalign: %d\n", mctx->block_align);
    Trace("channels:%d,sr:%d,bitrate:%d\n", mctx->channels, mctx->sample_rate, mctx->bit_rate);

#if 1
    if(mCodec->capabilities & CODEC_CAP_TRUNCATED) {
        mctx->flags |= CODEC_FLAG_TRUNCATED;
    }

    if(mCodec->capabilities & CODEC_CAP_DR1) {
        mctx->flags |= CODEC_FLAG_EMU_EDGE;
    }
#endif

    if(avcodec_open2(mctx, mCodec, NULL) < 0) {
        ALOGE("decoder not open!!\n");
        return UNKNOWN_ERROR;
    }

    return OK;

}

int32_t AmAudioCodec::audio_decode_frame(AUDIO_FRAME_WRAPPER_T * data, int * got_frame, PACKET_WRAPPER_T * avpkt)
{
    CHECK(mctx&&mCodec);
    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.data = avpkt->data;
    pkt.size = avpkt->size;
    av_frame_unref(mFrame);
    int32_t ret = avcodec_decode_audio4(mctx, mFrame, got_frame, &pkt);
    Trace("used data: %d, no used data:%d", ret, avpkt->size - ret);
    data->datasize = 0;
    memset(data->data, 0, 8192);
    if(ret < 0){
        return ret;
    }

    if(*got_frame) {
        int data_size = av_samples_get_buffer_size(NULL, av_frame_get_channels(mFrame), mFrame->nb_samples,
                                                  (enum AVSampleFormat)mFrame->format, 1);
        if(data_size > 0){
          int64_t dec_channel_layout = (mFrame->channel_layout && av_frame_get_channels(mFrame) == 
                                      av_get_channel_layout_nb_channels(mFrame->channel_layout)) ?
                                      mFrame->channel_layout : av_get_default_channel_layout(av_frame_get_channels(mFrame));
          int channels = av_frame_get_channels(mFrame);
          int samplesize =   av_get_bytes_per_sample((enum AVSampleFormat)mFrame->format);

          data->samplerate = mFrame->sample_rate;
          data->channels = channels;
          data->bytes = samplesize;
          if(mFrame->format == AV_SAMPLE_FMT_FLTP){
            int i,ch;
            float * in = (float *)mFrame->data[0];
            short * out = (short *)data->data;
            for (int i = 0; i < mFrame->nb_samples; i++)
            {    
                for (ch = 0; ch < channels; ch++)
                {
                    float* extended_data = (float*)mFrame->extended_data[ch];
                    float sample = extended_data[i];
                    if (sample < -1.0f) 
                        sample = -1.0f;
                    else if (sample > 1.0f) 
                        sample = 1.0f;
                    out[i * channels + ch] = (short)round(sample * 32767.0f);
                }
            }

            data->datasize = data_size/2;
            Trace("decoder output: channel = %d,sample_rate = %d\n", channels, mFrame->sample_rate);
            Trace("decoder output: sample_size = %d\n",samplesize);
            Trace("output pcm data size: %d\n", data_size);

          }else if(mFrame->format == AV_SAMPLE_FMT_S16P) {
            int i;
            short * in1 = (short *)mFrame->data[0];
            short * in2 = (short *)mFrame->data[1];
            short * out = (short *)data->data;
            if (channels == 2) {
                for (i= 0; i < data_size/4; i++ ) {
                   out[2*i] = in1[i];
                   out[2*i+1] = in2[i];
                }

            }else if (channels == 1) {
                memcpy((char *)data->data, mFrame->data[0], data_size);
            }
            Trace("output pcm data size: %d\n", data_size);
            data->datasize = data_size;

          }else {
            Trace("output pcm data size: %d\n", data_size);
            memcpy((char *)data->data, mFrame->data[0], data_size);
            data->datasize = data_size;
          }
        }else{
            ALOGI("current frame decodes pcm number <= 0\n");
        }
    }

    return ret;
}

void AmAudioCodec::audio_decode_free_frame() {
    av_frame_free(&mFrame);
    return;
}

void AmAudioCodec::audio_decode_alloc_frame() {
    mFrame = av_frame_alloc();
    return;
}

int32_t AmAudioCodec::decode_close()
{
    if(mctx){
        if(mctx->extradata != NULL){
            free(mctx->extradata);
            mctx->extradata = NULL;
        }
        avcodec_close(mctx);
        av_free(mctx);
        mctx = NULL;
    }

    mCodec = NULL;
    return OK;
}

}//namespace domain end
