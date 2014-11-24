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

#define LOG_TAG "VendorReformatter"
#include <utils/Log.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "formatters/VC1Formatter.h"

namespace android {

VC1Formatter::VC1Formatter(AVCodecContext *codec)
    : PassthruFormatter(codec) {
}

uint32_t VC1Formatter::computeNewESLen(
        const uint8_t* in, uint32_t inAllocLen) const {
    uint32_t newSize = inAllocLen;

    const uint8_t* packetData = in;
    if((packetData[0]!=0) || (packetData[1] != 0) || (packetData[2] != 1)
            || (packetData[3] != 0x0D && packetData[3] != 0x0F
                    && packetData[3] != 0x0B && packetData[3] != 0x0C)) {
        newSize += 4;
    }

    return newSize;
}

int32_t VC1Formatter::formatES(
        const uint8_t* in, uint32_t inAllocLen, uint8_t* out,
        uint32_t outAllocLen) const {
    if ((NULL == in) || (NULL == out)) {
        ALOGE("NULL in or out in %s", __FUNCTION__);
        return -1;
    }

    if (outAllocLen < 4) {
        ALOGE("not enough room in output buffer (%d) to store frame start code in"
                " %s", outAllocLen, __FUNCTION__);
        return -1;
    }

    uint32_t newSize = inAllocLen;

    const uint8_t* packetData = in;
    if((packetData[0]!=0) || (packetData[1] != 0) || (packetData[2] != 1)
            || (packetData[3] != 0x0D && packetData[3] != 0x0F
                    && packetData[3] != 0x0B && packetData[3] != 0x0C) ) {
        out[0] = 0x00;
        out[1] = 0x00;
        out[2] = 0x01;
        out[3] = 0x0D;
        out = out + 4;
        newSize = newSize + 4;
    }
    memcpy(out, in, inAllocLen);

    return newSize;
}

}  // namespace googletv
