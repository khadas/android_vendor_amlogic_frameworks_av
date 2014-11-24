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

#ifndef APE_FORMATTER_H_
#define APE_FORMATTER_H_

#include "formatters/PassthruFormatter.h"

namespace android {

class APEFormatter: public PassthruFormatter {
public:
    APEFormatter(AVCodecContext *codec);
    virtual bool addCodecMeta(const sp<MetaData> &meta) const;

private:
    uint32_t sample_rate;
    uint32_t mBitsPerSample;
    uint32_t channels;
    bool mInitCheck;
};

}  // namespace android

#endif  // WMA_FORMATTER_H_
