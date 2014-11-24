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

#ifndef VC1_FORMATTER_H_
#define VC1_FORMATTER_H_

#include "formatters/PassthruFormatter.h"

namespace android {

class VC1Formatter : public PassthruFormatter {
public:
    VC1Formatter(AVCodecContext *codec);
    virtual ~VC1Formatter() { }

    virtual uint32_t computeNewESLen(
            const uint8_t* in, uint32_t inAllocLen) const;

    virtual int32_t formatES(
          const uint8_t* in, uint32_t inAllocLen, uint8_t* out,
          uint32_t outAllocLen) const;
};

}  // namespace android

#endif  // VC1_FORMATTER_H_
