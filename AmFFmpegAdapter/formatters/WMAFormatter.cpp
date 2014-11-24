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
#define LOG_TAG "WMAFormatter"
#include <utils/Log.h>

#include "formatters/WMAFormatter.h"

#include "AmFFmpegUtils.h"
#include <media/stagefright/AmMetaDataExt.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/MetaData.h>

namespace android {

WMAFormatter::WMAFormatter(AVCodecContext *codec)
    : PassthruFormatter(codec),
      mBlockAlign(0),
      mBitsPerSample(0),
      mFormatTag(0),
      mInitCheck(false) {

    if (    codec->codec_tag == 0x0160
            ||codec->codec_tag == 0x0161            // WMA
            || codec->codec_tag == 0x0162     // WMA Pro
            || codec->codec_tag == 0x0163) {  // WMA Lossless
        mBlockAlign = codec->block_align;
        mBitsPerSample = codec->bits_per_coded_sample;
        mFormatTag = codec->codec_tag;
        mInitCheck = true;
    } else {
        ALOGW("Unsupported format tag %x", codec->codec_tag);
    }
}

bool WMAFormatter::addCodecMeta(const sp<MetaData> &meta) const {
    if (mInitCheck) {
        meta->setInt32(kKeyWMABlockAlign, mBlockAlign);
        meta->setInt32(kKeyWMABitsPerSample, mBitsPerSample);
        meta->setInt32(kKeyWMAFormatTag, mFormatTag);
        meta->setData(kKeyCodecSpecific, 0, mExtraData, mExtraSize);
        return true;
    }
    return false;
}

}  // namespace android
