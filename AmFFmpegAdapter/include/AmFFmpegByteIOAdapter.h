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

#ifndef AM_FFMPEG_BYTE_IO_ADAPTER_H_
#define AM_FFMPEG_BYTE_IO_ADAPTER_H_

#include <stdint.h>

extern "C" {
#include <libavformat/avio.h>
}

#include <utils/RefBase.h>

namespace android {

class DataSource;

class AmFFmpegByteIOAdapter : public RefBase {
public:
    AmFFmpegByteIOAdapter();
    ~AmFFmpegByteIOAdapter();

    bool init(sp<DataSource> src);
    AVIOContext* getContext() { return mInitCheck ? mContext : NULL; }

private:
    bool mInitCheck;
    AVIOContext* mContext;
    sp<DataSource> mSource;

    int64_t mNextReadPos;
    int32_t mWakeupHandle;

    int32_t read(uint8_t* buf, int amt);
    int64_t seek(int64_t offset, int whence);

    // I/O callback functions which will be called from FFmpeg.
    static int32_t staticRead(void* thiz, uint8_t* buf, int amt);
    static int32_t staticWrite(void* thiz, uint8_t* buf, int amt);
    static int64_t staticSeek(void* thiz, int64_t offset, int whence);
};

}  // namespace android

#endif  // AM_FFMPEG_BYTE_IO_ADAPTER_H_
