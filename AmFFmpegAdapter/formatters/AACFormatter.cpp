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
#define LOG_TAG "AACFormatter"
#include <utils/Log.h>

#include "formatters/AACFormatter.h"

#include "AmFFmpegUtils.h"

namespace android {

AACFormatter::AACFormatter(AVCodecContext *codec)
    :PassthruFormatter(codec) {
}

bool AACFormatter::addCodecMeta(const sp<MetaData> &meta) const {
    if (mExtraSize < 2) {
        ALOGE("Invalid extra data for AAC codec.");
        return false;
    }
    addESDSFromCodecPrivate(meta, true, mExtraData, mExtraSize);
    return true;
}

}  // namespace android
