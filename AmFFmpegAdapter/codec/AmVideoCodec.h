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

#ifndef AM_VIDEO_CODEC_H_
#define AM_VIDEO_CODEC_H_

#include "AmFFmpegCodec.h"

namespace android {

class AmVideoCodec : public AmFFmpegCodec {
public:
    AmVideoCodec();
    virtual int32_t video_decode_init(const char * codecMime, VIDEO_INFO_T *video_info);
    virtual int32_t video_decode_frame(VIDEO_FRAME_WRAPPER_T *data, int *got_frame, PACKET_WRAPPER_T * pkt);

    virtual void video_decode_alloc_frame();
    virtual void video_decode_free_frame();
    virtual void video_decode_init_avpkt();

    virtual int32_t video_decode_get_display(int32_t * width, int32_t * height);

    virtual int32_t decode_close();

private:
    virtual ~AmVideoCodec();

    AVCodecContext * mctx;
    AVCodec * mCodec;
    AVFrame * mFrame;

};

}

#endif