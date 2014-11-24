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

#ifndef AVCC_FORMATTER_H_
#define AVCC_FORMATTER_H_

#include "formatters/StreamFormatter.h"

namespace android {

class AVCCFormatter: public StreamFormatter {
public:
    AVCCFormatter(AVCodecContext *codec);
    virtual ~AVCCFormatter();

    virtual bool addCodecMeta(const sp<MetaData> &meta) const;

    virtual uint32_t computeNewESLen(
            const uint8_t* in, uint32_t inAllocLen) const;

    virtual int32_t formatES(
            const uint8_t* in, uint32_t inAllocLen, uint8_t* out,
            uint32_t outAllocLen) const;

private:
    bool parseCodecExtraData(AVCodecContext* codec);
    size_t parseNALSize(const uint8_t *data) const ;

    bool mAVCCFound;
    uint8_t* mAVCC;
    uint32_t mAVCCSize;
    size_t mNALLengthSize;
};

}  // namespace android

#endif  // AVCC_FORMATTER_H_
