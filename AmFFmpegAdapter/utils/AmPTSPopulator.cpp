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
#define LOG_TAG "AmPTSPopulator"
#include <utils/Log.h>

#include <media/stagefright/MediaExtractor.h>
#include <media/stagefright/AmMediaDefsExt.h>
#include <AmPTSPopulator.h>

namespace android {

AmPTSPopulator::AmPTSPopulator(uint32_t streamsCount) {
    mStreamsCount = streamsCount;
    mIsPTSReliable = new bool[mStreamsCount];
    mLastPTS = new int64_t[mStreamsCount];
    for (unsigned int i = 0; i < mStreamsCount; i++) {
        mIsPTSReliable[i] = true;
        mLastPTS[i] = kUnknownPTS;
    }
}

AmPTSPopulator::~AmPTSPopulator() {
    mStreamsCount = 0;
    delete [] mIsPTSReliable;
    delete [] mLastPTS;
}

int64_t AmPTSPopulator::computePTS(uint32_t streamIndex,
        const int64_t pts, const int64_t dts, const bool isKeyFrame) {

    Mutex::Autolock autoLock(mLock);

    if ((mIsPTSReliable[streamIndex])
            && (pts != kUnknownPTS)
            && (mLastPTS[streamIndex] != kUnknownPTS)
            && (mLastPTS[streamIndex] == pts)) {
        mIsPTSReliable[streamIndex] = false;
        mLastPTS[streamIndex] = kUnknownPTS;
    }

    if (mIsPTSReliable[streamIndex]) {
        mLastPTS[streamIndex] = pts;
        return pts;
    } else {
        return kUnknownPTS;
    }
}

void AmPTSPopulator::reset() {

    Mutex::Autolock autoLock(mLock);

    for (unsigned int i = 0; i < mStreamsCount; i++) {
        mLastPTS[i] = kUnknownPTS;
    }
}

}  // namespace android
