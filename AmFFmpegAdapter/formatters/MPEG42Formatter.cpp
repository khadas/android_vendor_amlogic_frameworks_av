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

//#define LOG_NDEBUG 0
#define LOG_TAG "MPEG42Formatter"
#include <utils/Log.h>

#include "formatters/MPEG42Formatter.h"

#include "AmFFmpegUtils.h"

namespace android {

MPEG42Formatter::MPEG42Formatter(AVCodecContext *codec)
    : PassthruFormatter(codec) {
}

bool MPEG42Formatter::addCodecMeta(const sp<MetaData> &meta) const {
    if (mExtraSize == 0) {
        ALOGE("Invalid extra data for MPEG42 codec.");
        return false;
    }
    addESDSFromCodecPrivate(meta, false, mExtraData, mExtraSize);
    return true;
}

}  // namespace android
