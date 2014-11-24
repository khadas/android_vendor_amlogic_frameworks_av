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

#define LOG_TAG "PCMBlurayFormatter"
#include <utils/Log.h>

#include "formatters/PCMBlurayFormatter.h"

namespace android {

const static uint32_t kLPCMHeaderLength = 4;

PCMBlurayFormatter::PCMBlurayFormatter(AVCodecContext *codec) {
}

bool PCMBlurayFormatter::addCodecMeta(const sp<MetaData> &meta) const {
    return true;
}

uint32_t PCMBlurayFormatter::computeNewESLen(
        const uint8_t* in, uint32_t inAllocLen) const {
    if (inAllocLen < kLPCMHeaderLength) {
        ALOGE("Frame size too small in %s", __FUNCTION__);
        return 0;
    }

    return inAllocLen - kLPCMHeaderLength;
}

int32_t PCMBlurayFormatter::formatES(
        const uint8_t* in, uint32_t inAllocLen, uint8_t* out,
        uint32_t outAllocLen) const {
    if ((NULL == in) || (NULL == out)) {
        ALOGE("NULL in or out in %s", __FUNCTION__);
        return -1;
    }

    if (outAllocLen < kLPCMHeaderLength) {
        ALOGE("not enough room in output buffer (%d) to store frame start code in"
                " %s", outAllocLen, __FUNCTION__);
        return -1;
    }

    uint32_t newSize = inAllocLen - kLPCMHeaderLength;

    memcpy(out, in + kLPCMHeaderLength, newSize);

    return newSize;
}

} // namespace android
