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

#ifndef AAC_FORMATTER_H_
#define AAC_FORMATTER_H_

#include "formatters/PassthruFormatter.h"

namespace android {

class AACFormatter: public PassthruFormatter {
public:
    AACFormatter(AVCodecContext *codec);
    virtual bool addCodecMeta(const sp<MetaData> &meta) const;
};

}  // namespace android

#endif  // AAC_FORMATTER_H_
