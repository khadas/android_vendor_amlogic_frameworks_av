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

#ifndef AM_FFMPEG_UTILS_H_
#define AM_FFMPEG_UTILS_H_

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/internal.h>
#undef CodecType
}

#include <stdint.h>

#include <utils/StrongPointer.h>
#include <OMX_Types.h>

struct AVCodecContext;
struct AVFormatContext;
struct AVInputFormat;

namespace android {

class AmFFmpegByteIOAdapter;
class MetaData;
struct DataSource;

AVInputFormat *probeFormat(const sp<DataSource> &source);

AVFormatContext *openAVFormatContext(
        AVInputFormat *inputFormat, AmFFmpegByteIOAdapter *adapter);

const char *convertCodecIdToMimeType(AVCodecContext *codec);
int32_t convertMimeTypetoCodecId(const char *mime);
const char *convertInputFormatToMimeType(AVInputFormat *inputFormat);
bool getPcmFormatFromCodecId(AVCodecContext *codec, int32_t *bitsPerSample,
        OMX_ENDIANTYPE *dataEndian, OMX_NUMERICALDATATYPE *dataSigned);
float AmPropGetFloat(const char* str, float def = 0.0);

int64_t convertTimeBaseToMicroSec(int64_t time);
int64_t convertMicroSecToTimeBase(int64_t time);

void addESDSFromCodecPrivate(
        const sp<MetaData> &meta,
        bool isAudio, const void *priv, size_t privSize);

int32_t castHEVCSpecificData(uint8_t * data, int32_t size);

}  // namespace android

#endif  // AM_FFMPEG_UTILS_H_
