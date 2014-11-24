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
#define LOG_TAG "AmFFmpegCodec"
#include <utils/Log.h>

#include "AmFFmpegCodec.h"
#include "AmFFmpegUtils.h"
#include "codec/AmVideoCodec.h"
#include "codec/AmAudioCodec.h"

#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/AmMediaDefsExt.h>
#include <media/stagefright/MediaDefs.h>

namespace android {

static bool MimeSupportOrNot(const char * codecMime) {
    if(!strcmp(MEDIA_MIMETYPE_VIDEO_VP6, codecMime)
	|| !strcmp(MEDIA_MIMETYPE_VIDEO_VP6F, codecMime)
	|| !strcmp(MEDIA_MIMETYPE_VIDEO_VP6A, codecMime)
	|| !strcmp(MEDIA_MIMETYPE_VIDEO_AVC, codecMime)
	|| !strcmp(MEDIA_MIMETYPE_VIDEO_WMV2, codecMime)
	|| !strcmp(MEDIA_MIMETYPE_VIDEO_RM, codecMime)) {
        return true;
    }

    //support audio codec
    if(!strcmp(MEDIA_MIMETYPE_AUDIO_COOK, codecMime)
       || !strcmp(MEDIA_MIMETYPE_AUDIO_FLAC, codecMime)){
        ALOGI("NOTE: audio using soft decoder\n");
        return true;
    }

    return false;
}

//static
sp<AmFFmpegCodec> AmFFmpegCodec::CreateCodec(const char * codecMime) {
    CHECK(codecMime);
    ALOGI("CreateCodec for codecMime : %s", codecMime);
    if(MimeSupportOrNot(codecMime)) {
        if(!strncasecmp(codecMime, "video", 5)) {
            return new AmVideoCodec();
        } else if(!strncasecmp(codecMime, "audio", 5)) {
            //TODO: add audio codec
            //done
            ALOGI("CreateCodec: new AmAudioCodec\n");
            return new AmAudioCodec();
        }
    }
    return NULL;
}

}
