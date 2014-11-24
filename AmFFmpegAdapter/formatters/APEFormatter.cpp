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
#define LOG_TAG "APEFormatter"
#include <utils/Log.h>

#include "formatters/APEFormatter.h"

#include "AmFFmpegUtils.h"
#include <media/stagefright/AmMetaDataExt.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/MetaData.h>

namespace android {

APEFormatter::APEFormatter(AVCodecContext *codec)
    : PassthruFormatter(codec)
{
      
      sample_rate = codec->sample_rate;
      mBitsPerSample = codec->bits_per_coded_sample;
      channels = codec->channels;
      mInitCheck = true;
}

bool APEFormatter::addCodecMeta(const sp<MetaData> &meta) const {
    if (mInitCheck) {
        meta->setInt32(kKeySampleRate, sample_rate);
        meta->setInt32(kKeyPCMBitsPerSample, mBitsPerSample);
        meta->setInt32(kKeyChannelCount, channels);
        meta->setData(kKeyExtraData,0,mExtraData,mExtraSize);
	    meta->setInt32(kKeyExtraDataSize,mExtraSize);
        return true;
    }
    return false;
}

}  // namespace android
