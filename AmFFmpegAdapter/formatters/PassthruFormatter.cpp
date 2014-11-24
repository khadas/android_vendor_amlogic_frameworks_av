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
#define LOG_TAG "PassthruFormatter"
#include <utils/Log.h>

#include "formatters/PassthruFormatter.h"

#include <media/stagefright/AmMetaDataExt.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/MetaData.h>
#include "AmFFmpegUtils.h"

namespace android {

PassthruFormatter::PassthruFormatter(AVCodecContext *codec)
    : mExtraData(NULL),
      mExtraSize(0) {
    parseCodecExtraData(codec);
}

PassthruFormatter::~PassthruFormatter() {
    if (mExtraData) {
        delete[] mExtraData;
    }
}

bool PassthruFormatter::parseCodecExtraData(AVCodecContext* codec) {
    if (0 != codec->extradata_size) {
        CHECK(NULL == mExtraData);
        mExtraData = new uint8_t[codec->extradata_size];
        mExtraSize = static_cast<uint32_t>(codec->extradata_size);
        memcpy(mExtraData, codec->extradata, mExtraSize);
    }
    return true;
}

bool PassthruFormatter::addCodecMeta(const sp<MetaData> &meta) const {
    if (0 == mExtraSize) {
        return false;
    }
    ALOGV("mExtraSize=%d,mExtraData=%x",mExtraSize,mExtraData);
    addESDSFromCodecPrivate(meta, false, mExtraData, mExtraSize);
    return true;
}

uint32_t PassthruFormatter::computeNewESLen(
        const uint8_t* in, uint32_t inAllocLen) const {
    return inAllocLen;
}

int32_t PassthruFormatter::formatES(
        const uint8_t* in, uint32_t inAllocLen, uint8_t* out,
        uint32_t outAllocLen) const {
    if (!inAllocLen || inAllocLen > outAllocLen) {
        return -1;
    }
    CHECK(in);
    CHECK(out);
    CHECK(in != out);
    memcpy(out, in, inAllocLen);
    return inAllocLen;
}

}  // namespace android
