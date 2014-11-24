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
#define LOG_TAG "VorbisFormatter"
#include <utils/Log.h>

#include "formatters/VorbisFormatter.h"

#include <media/stagefright/AmMetaDataExt.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/MediaErrors.h>

const uint32_t kVorbisIDHeaderSize = 30;

namespace android {

status_t addVorbisCodecInfo(
        const sp<MetaData> &meta,
        const void *_codecPrivate, size_t codecPrivateSize) {
    // hexdump(_codecPrivate, codecPrivateSize);

    if (codecPrivateSize < 1) {
        return ERROR_MALFORMED;
    }

    const uint8_t *codecPrivate = (const uint8_t *)_codecPrivate;

    if (codecPrivate[0] != 0x02) {
        return ERROR_MALFORMED;
    }

    // codecInfo starts with two lengths, len1 and len2, that are
    // "Xiph-style-lacing encoded"...

    size_t offset = 1;
    size_t len1 = 0;
    while (offset < codecPrivateSize && codecPrivate[offset] == 0xff) {
        len1 += 0xff;
        ++offset;
    }
    if (offset >= codecPrivateSize) {
        return ERROR_MALFORMED;
    }
    len1 += codecPrivate[offset++];

    size_t len2 = 0;
    while (offset < codecPrivateSize && codecPrivate[offset] == 0xff) {
        len2 += 0xff;
        ++offset;
    }
    if (offset >= codecPrivateSize) {
        return ERROR_MALFORMED;
    }
    len2 += codecPrivate[offset++];

    if (codecPrivateSize < offset + len1 + len2) {
        return ERROR_MALFORMED;
    }

    if (codecPrivate[offset] != 0x01) {
        return ERROR_MALFORMED;
    }
    meta->setData(kKeyVorbisInfo, 0, &codecPrivate[offset], len1);

    offset += len1;
    if (codecPrivate[offset] != 0x03) {
        return ERROR_MALFORMED;
    }

    offset += len2;
    if (codecPrivate[offset] != 0x05) {
        return ERROR_MALFORMED;
    }

    meta->setData(
            kKeyVorbisBooks, 0, &codecPrivate[offset],
            codecPrivateSize - offset);

    return OK;
}

VorbisFormatter::VorbisFormatter(AVCodecContext *codec)
    : PassthruFormatter(codec) {
}

bool VorbisFormatter::addCodecMeta(const sp<MetaData> &meta) const {
    if (mExtraSize < kVorbisIDHeaderSize) {
        return false;
    }
    // Try to find the start of the Vorbis ID header.  Depending on the
    // container, FFmpeg may have some leading bytes in the extradata field
    // which are not part of the Vorbis ID, Comment and Setup headers.
    // String representation of the magic number for the vorbis ID header is
    // "\001vorbis" For details, please refer the chapter 4 of
    // http://xiph.org/vorbis/doc/Vorbis_I_spec.pdf
    static const uint8_t kVorbisIDHeader[] = {
            0x01, 0x76, 0x6f, 0x72, 0x62, 0x69, 0x73 };
    uint32_t i = 0;
    uint32_t searchLimit = mExtraSize - kVorbisIDHeaderSize + 1;
    for (i = 0; i < searchLimit; ++i) {
        if (!memcmp(mExtraData + i, kVorbisIDHeader, sizeof(kVorbisIDHeader))) {
            break;
        }
    }

    if (i == searchLimit) {
        ALOGE("Failed to find Vorbis ID header in FFmpeg extradata field.");
        return false;
    }

    CHECK_LT(i, searchLimit);
    //meta->setData(kKeyCodecSpecific, 0, mExtraData + i, mExtraSize - i);

    // copy form android matroskaextractor
    status_t err = OK;
    err = addVorbisCodecInfo(meta, mExtraData, mExtraSize);
    return err;
}

}  // namespace android
