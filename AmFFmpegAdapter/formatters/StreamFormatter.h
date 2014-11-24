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

#ifndef STREAM_FORMATTER_H_
#define STREAM_FORMATTER_H_

#include <utils/Errors.h>
#include <utils/RefBase.h>
#include <utils/StrongPointer.h>

extern "C" {
#include <libavformat/avformat.h>
#undef CodecType
}

namespace android {

class MetaData;

class StreamFormatter : public RefBase{
  public:
    static sp<StreamFormatter> Create(
            AVCodecContext *codec, AVInputFormat *format);

    virtual ~StreamFormatter() { }

    // Used to add codec specific configuration metadata for this stream.
    // Examples include AVCDecoderConfigurationRecord/AVCC (14496-15) for AVC
    // or ES_Descriptor/ESDS (14496-1) for AAC and MPEG4. This adds nothing
    // for streams which have headers at the start of each frame like MP3 or
    // AAC in ADTS mode. Returns true if metadata is added. Otherwise, returns
    // false.
    virtual bool addCodecMeta(const sp<MetaData> &meta) const = 0;

    // Returns the size of formatted elementary stream. This will be used for
    // output buffer allocation for formatES().
    virtual uint32_t computeNewESLen(
            const uint8_t* in, uint32_t inAllocLen) const = 0;

    // Used to reformat the elementary stream to satisfy the requirements of
    // the codec. For example, one implementation converts MP4 style length
    // prefixed H.264 NALs into Annex-B style start-code prefixed NALs.
    // Returns the number of bytes used if successful and returns -1 otherwise.
    virtual int32_t formatES(
            const uint8_t* in, uint32_t inAllocLen, uint8_t* out,
            uint32_t outAllocLen) const = 0;
};

}  // namespace android

#endif  // STREAM_FORMATTER_H_
