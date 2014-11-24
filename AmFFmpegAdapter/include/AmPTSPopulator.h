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

#ifndef AM_PTS_POPULATOR_H_
#define AM_PTS_POPULATOR_H_

#include <utils/RefBase.h>
#include <utils/threads.h>

namespace android {

class AmPTSPopulator : public RefBase {
public:
    AmPTSPopulator(uint32_t streamsCount);
    ~AmPTSPopulator();
    virtual int64_t computePTS(uint32_t streamIndex,
            const int64_t pts, const int64_t dts, const bool isKeyFrame);
    virtual void reset();
private:
    Mutex mLock;
    uint32_t mStreamsCount;
    bool *mIsPTSReliable;
    int64_t *mLastPTS;
};

}  // namespace android

#endif  // AM_PTS_POPULATOR_H_
