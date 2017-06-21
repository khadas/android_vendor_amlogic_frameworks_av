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

#ifndef HVCC_FORMATTER_H_
#define HVCC_FORMATTER_H_

#include "formatters/StreamFormatter.h"

namespace android {

class HVCCFormatter: public StreamFormatter {
public:
    HVCCFormatter(AVCodecContext *codec);
    virtual ~HVCCFormatter();

    virtual bool addCodecMeta(const sp<MetaData> &meta) const;

    virtual uint32_t computeNewESLen(
            const uint8_t* in, uint32_t inAllocLen) const;

    virtual int32_t formatES(
            const uint8_t* in, uint32_t inAllocLen, uint8_t* out,
            uint32_t outAllocLen) const;

    virtual int32_t getMetaFromES(
            const uint8_t* data, uint32_t size, const sp<MetaData> &meta);

private:
    bool parseCodecExtraData(AVCodecContext* codec);
    size_t parseNALSize(const uint8_t *data) const ;

    bool mHVCCFound;
    uint8_t* mHVCC;
    uint32_t mHVCCSize;
    size_t mNALLengthSize;
    bool mHVCCSkip;
    int mCheckHDR;
};

}  // namespace android

#endif  // HVCC_FORMATTER_H_
