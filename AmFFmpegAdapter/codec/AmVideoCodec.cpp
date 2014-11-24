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
 *
 * coded by senbai.tao@amlogic.com
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "AmVideoCodec"
#include <utils/Log.h>

#include "codec/AmVideoCodec.h"
#include "AmFFmpegUtils.h"

#include <media/stagefright/foundation/ADebug.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/internal.h>
#include <libavutil/opt.h>
#define ARCH_ARM 1
#define restrict
}

namespace android {

static int32_t GetCPUCoreCount() {
    int cpuCoreCount = 1;
#if defined(_SC_NPROCESSORS_CONF)
    cpuCoreCount = sysconf(_SC_NPROCESSORS_CONF);
#else
    // _SC_NPROC_CONF must be defined...
    cpuCoreCount = sysconf(_SC_NPROC_CONF);
#endif
    CHECK(cpuCoreCount >= 1);
    ALOGI("Number of CPU cores: %d", cpuCoreCount);
    return cpuCoreCount;
}

AmVideoCodec::AmVideoCodec()
    : mctx(NULL),
      mCodec(NULL),
      mFrame(NULL) {
    avcodec_register_all();
}

AmVideoCodec::~ AmVideoCodec() {
    decode_close();
}

int32_t AmVideoCodec::video_decode_init(const char * codecMime, VIDEO_INFO_T *video_info) {
    AVCodecID id = static_cast<AVCodecID>(convertMimeTypetoCodecId(codecMime));
    mCodec = avcodec_find_decoder(id);
    if(!mCodec) {
        ALOGE("%s decoder not found!!\n", codecMime);
        return UNKNOWN_ERROR;
    }
    mctx = avcodec_alloc_context3(mCodec);
    if(!mctx) {
        ALOGE("malloc codec context failed!!\n");
        return UNKNOWN_ERROR;
    }
    if(mCodec->capabilities & CODEC_CAP_TRUNCATED) {
        mctx->flags |= CODEC_FLAG_TRUNCATED;
    }
    if(mCodec->capabilities & CODEC_CAP_DR1) {
        mctx->flags |= CODEC_FLAG_EMU_EDGE;
    }

    int32_t thread_num = GetCPUCoreCount();
    ALOGI("decoder thread num : %d\n", thread_num);
    if(mCodec->capabilities & CODEC_CAP_FRAME_THREADS) {
        av_opt_set(mctx, "thread_type", "frame", 0);
    } else if (mCodec->capabilities & CODEC_CAP_SLICE_THREADS) {
        av_opt_set(mctx, "thread_type", "slice", 0);
    }
    av_opt_set_int(mctx, "threads", thread_num, 0);
#if 0
    if((thread_num = AmPropGetFloat("media.libplayer.ffdecthreadnum")) >= 0) {
        mctx->thread_count = thread_num;
    }
#endif

    if(id == AV_CODEC_ID_WMV2) {
        if(video_info->extra_data != NULL) {
            mctx->extradata = video_info->extra_data;
        }
        if(video_info->extra_data_size != 0) {
            mctx->extradata_size = video_info->extra_data_size;
        }
        if(video_info->width != 0) {
            mctx->width = video_info->width;
        }
        if(video_info->height!= 0) {
            mctx->height= video_info->height;
        }
        ALOGV("video info extra_data_size:%d width:%d height:%d", video_info->extra_data_size, video_info->width, video_info->height);
    }

    if(avcodec_open2(mctx, mCodec, NULL) < 0) {
        ALOGE("decoder not open!!\n");
        return UNKNOWN_ERROR;
    }
    return OK;
}

int32_t AmVideoCodec::video_decode_frame(VIDEO_FRAME_WRAPPER_T * data, int * got_frame, PACKET_WRAPPER_T * avpkt) {
    CHECK(mctx&&mCodec);
    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.data = avpkt->data;
    pkt.size = avpkt->size;
    int32_t ret = avcodec_decode_video2(mctx, mFrame, got_frame, &pkt);
    if(got_frame) {
        data->width = mFrame->width;
        data->height = mFrame->height;
        data->format = mFrame->format;
        data->data[0] = mFrame->data[0];
	 data->data[1] = mFrame->data[1];
	 data->data[2] = mFrame->data[2];
        data->linesize[0] = mFrame->linesize[0];
	 data->linesize[1] = mFrame->linesize[1];
	 data->linesize[2] = mFrame->linesize[2];
    }
    return ret;
}

int32_t AmVideoCodec::video_decode_get_display(int32_t * width, int32_t * height) {
    CHECK(mctx);
    *width = mctx->width;
    *height = mctx->height;
    return OK;
}

void AmVideoCodec::video_decode_alloc_frame() {
    mFrame = avcodec_alloc_frame();
    return;
}

void AmVideoCodec::video_decode_free_frame() {
    avcodec_free_frame(&mFrame);
    return;
}

void AmVideoCodec::video_decode_init_avpkt() {
    return;
}

int32_t AmVideoCodec::decode_close() {
    if(mctx) {
        avcodec_close(mctx);
        av_free(mctx);
        mctx = NULL;
    }
    mCodec = NULL;
    return OK;
}

}