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
#define LOG_TAG "StreamFormatter"
#include <utils/Log.h>

#include "formatters/StreamFormatter.h"

#include "AmFFmpegUtils.h"
#include <formatters/AACFormatter.h>
#include <formatters/AVCCFormatter.h>
#include <formatters/AVCTSFormatter.h>
#include <formatters/HVCCFormatter.h>
#include <formatters/MPEG42Formatter.h>
#include <formatters/PassthruFormatter.h>
#include <formatters/VorbisFormatter.h>
#include <formatters/VC1Formatter.h>
#include <formatters/WMAFormatter.h>
#include <formatters/APEFormatter.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/AmMediaDefsExt.h>

namespace android {

//static
sp<StreamFormatter> StreamFormatter::Create(
        AVCodecContext *codec, AVInputFormat *format) {
    ALOGI("Creating formatter for codec id : %u extradata size : %d",
            codec->codec_id, codec->extradata_size);

    const char *codecMime = convertCodecIdToMimeType(codec);
    if (!strcmp(codecMime, MEDIA_MIMETYPE_VIDEO_AVC)
            && (format == av_find_input_format("mp4")
                    || format == av_find_input_format("flv")
                    || format == av_find_input_format("matroska"))) {
        // Double check the extradata really includes AVCC (14496-15) structure
        // because some matroska streams are already Annex-B framed and does not
        // have AVCC. In this case, we fall back to the default formatter.
        if (codec->extradata_size >= 7
                && reinterpret_cast<uint8_t *>(codec->extradata)[0] == 0x01) {
            return new AVCCFormatter(codec);
        }
    } else if (!strcmp(codecMime, MEDIA_MIMETYPE_VIDEO_AVC)
            && (format == av_find_input_format("mpegts"))) {
        return new AVCTSFormatter(codec);
    } else if (!strcmp(codecMime, MEDIA_MIMETYPE_VIDEO_HEVC)
            && (format == av_find_input_format("mp4")
                    || format == av_find_input_format("flv")
                    || format == av_find_input_format("matroska"))) {
        if (codec->extradata_size >= 22) {
            return new HVCCFormatter(codec);
        }
    } else if (!strcmp(codecMime, MEDIA_MIMETYPE_AUDIO_AAC)
            && (format == av_find_input_format("mp4")
                    || format == av_find_input_format("avi")
                    || format == av_find_input_format("flv")
                    || format == av_find_input_format("matroska"))
            && codec->extradata_size > 0) {
        return new AACFormatter(codec);
    } else if (!strcmp(codecMime, MEDIA_MIMETYPE_AUDIO_WMA)) {
        return new WMAFormatter(codec);
    } else if (!strcmp(codecMime, MEDIA_MIMETYPE_AUDIO_VORBIS)) {
        return new VorbisFormatter(codec);
    }
    else if(!strcmp(codecMime, MEDIA_MIMETYPE_AUDIO_APE)){
        return new APEFormatter(codec);
    }
    return new PassthruFormatter(codec);
}

void StreamFormatter::checkNAL(const uint8_t* in, uint32_t inAllocLen) {

}

status_t StreamFormatter::dequeueAccessUnit(sp<ABuffer> *buffer) {
    return NOT_ENOUGH_DATA;
}

}  // namespace android
