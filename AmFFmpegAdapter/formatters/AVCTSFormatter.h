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

#ifndef AVC_FORMATTER_H_
#define AVC_FORMATTER_H_

#include "formatters/StreamFormatter.h"
#include <utils/List.h>

namespace android {

class AVCTSFormatter: public android::StreamFormatter {
public:
    AVCTSFormatter(AVCodecContext *codec);
    virtual ~AVCTSFormatter();

    virtual bool addCodecMeta(const sp<MetaData> &meta) const;

    virtual uint32_t computeNewESLen(
            const uint8_t* in, uint32_t inAllocLen) const;

    virtual int32_t formatES(
            const uint8_t* in, uint32_t inAllocLen, uint8_t* out,
            uint32_t outAllocLen) const;

    void checkNAL(const uint8_t* in, uint32_t inAllocLen);

    status_t dequeueAccessUnit(sp<ABuffer> *buffer);

protected:
    uint8_t* mExtraData;
    uint32_t mExtraSize;

private:
    void queueAccessUnit(const sp<ABuffer> &buffer);
    status_t appendData(const void *data, size_t size);
    sp<ABuffer> parseSei();
    bool parseCodecExtraData(AVCodecContext* codec);
};

}  // namespace android

#endif  // AVC_FORMATTER_H_