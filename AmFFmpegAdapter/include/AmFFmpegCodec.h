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

#ifndef AM_FFMPEG_CODEC_H_
#define AM_FFMPEG_CODEC_H_

#include <utils/Errors.h>
#include <utils/RefBase.h>
#include <utils/StrongPointer.h>

struct AVCodecContext;
struct AVCodec;
struct AVFrame;

typedef struct VIDEO_FRAME_WRAPPER {
#define NUM_DATA_POINTERS 8
    uint8_t * data[NUM_DATA_POINTERS];
    int32_t linesize[NUM_DATA_POINTERS];
    int32_t width;
    int32_t height;
    int32_t format;
}VIDEO_FRAME_WRAPPER_T;

typedef struct AUDIO_FRAME_WRAPPER{
    uint8_t data[512*1024];
    unsigned int datasize;
    int samplerate;
    int channels;
    int bytes;
}AUDIO_FRAME_WRAPPER_T;

typedef struct AUDIO_INFO{
    int32_t channels;
    int32_t bitrate;
    int32_t samplerate;
    int32_t bitspersample;
    int32_t blockalign;
    int32_t codec_id;
    int32_t extradata_size;
    uint8_t extradata[16384]; 
} AUDIO_INFO_T;

typedef struct PACKET_WRAPPER {
    uint8_t *data;
    int32_t size;
}PACKET_WRAPPER_T;

typedef struct VIDEO_INFO {
    uint8_t *extra_data;
	int32_t extra_data_size;
    int32_t width;
	int32_t height;
}VIDEO_INFO_T;

namespace android {

class AmFFmpegCodec : public RefBase{
public:
    static sp<AmFFmpegCodec> CreateCodec(const char * codecMime);
    virtual ~AmFFmpegCodec() {}

    /************** video api ******************/
    virtual int32_t video_decode_init(const char * codecMime, VIDEO_INFO_T *video_info=NULL){return 0;} //= 0;
    virtual int32_t video_decode_frame(VIDEO_FRAME_WRAPPER_T *data, int *got_frame, PACKET_WRAPPER_T * pkt) {return 0;}//= 0;

    virtual void video_decode_alloc_frame(){}// = 0;
    virtual void video_decode_free_frame() {}//= 0;
    virtual void video_decode_init_avpkt() {}//= 0;

    virtual int32_t video_decode_get_display(int32_t * width, int32_t * height) {return 0;}//= 0;

    /************** audio api ******************/
    //TODO: some audio decode api to add
    //done
    virtual int32_t audio_decode_init(const char * codeMime, AUDIO_INFO_T *ainfo){return 0;}
    virtual int32_t audio_decode_frame(AUDIO_FRAME_WRAPPER_T * data, int * got_frame, PACKET_WRAPPER_T * avpkt){return 0;}
    virtual void audio_decode_alloc_frame(){}// = 0;
    virtual void audio_decode_free_frame() {}//= 0;
    /************** common api ******************/
    virtual int32_t decode_close(){return 0;}// = 0;

};

}

#endif
