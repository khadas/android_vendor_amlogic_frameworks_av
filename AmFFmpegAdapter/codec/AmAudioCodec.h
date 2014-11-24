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

#ifndef AM_AUDIO_CODEC_H_
#define AM_AUDIO_CODEC_H_

#include "AmFFmpegCodec.h"

namespace android {

class AmAudioCodec : public AmFFmpegCodec {
public:
    AmAudioCodec();
	
    virtual int32_t audio_decode_init(const char * codecMime, AUDIO_INFO_T *ainfo);
    virtual int32_t audio_decode_frame(AUDIO_FRAME_WRAPPER_T * data, int * got_frame, PACKET_WRAPPER_T * avpkt);
    virtual void audio_decode_free_frame();
    virtual void audio_decode_alloc_frame();
    virtual int32_t decode_close();
private:
    virtual ~AmAudioCodec();
    AVCodecContext * mctx;
    AVCodec * mCodec;
    AVFrame * mFrame;
};

}
#endif  //end AM_AUDIO_CODEC_H_
