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

//#define LOG_NDEBUG 0
#define LOG_TAG "AmSimpleMediaExtractorPlugin"
#include <utils/Log.h>

#include "AmFFmpegExtractor.h"
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/AmMediaDefsExt.h>
#include <media/stagefright/MediaExtractor.h>
#include <media/stagefright/MetaData.h>
#include <include/MPEG4Extractor.h>
#include <utils/String8.h>

#ifdef __cplusplus
extern "C" {
#endif

bool sniffAmExtFormat(
        const android::sp<android::DataSource> &source,
        android::String8 *mimeType, float *confidence,
        android::sp<android::AMessage> *msg) {
    return android::SniffAmFFmpeg(source, mimeType, confidence, msg);
}

android::sp<android::MediaExtractor> createAmMediaExtractor(
        const android::sp<android::DataSource> &source, const char *mime) {
    android::MediaExtractor *ret = NULL;    
    ret = new android::AmFFmpegExtractor(source);
    return ret;
}

#ifdef __cplusplus
}  // extern "C"
#endif
