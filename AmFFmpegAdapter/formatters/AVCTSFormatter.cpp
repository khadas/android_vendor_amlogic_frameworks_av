/*
 * Copyright (C) 2011 The Android Open Source Project
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
#define LOG_TAG "AVCTSFormatter"
#include <utils/Log.h>

#include "formatters/AVCTSFormatter.h"

#include <media/stagefright/AmMetaDataExt.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/MetaData.h>
#include "AmFFmpegUtils.h"
#include <media/stagefright/foundation/ABitReader.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/Utils.h>

#include "include/avc_utils.h"

namespace android {

sp<ABuffer> mBuffer;
List<sp<ABuffer> > mBuffers;

AVCTSFormatter::AVCTSFormatter(AVCodecContext *codec)
    : mExtraData(NULL),
      mExtraSize(0) {
    parseCodecExtraData(codec);
}

AVCTSFormatter::~AVCTSFormatter() {
    if (mExtraData) {
        delete[] mExtraData;
    }
}

bool AVCTSFormatter::parseCodecExtraData(AVCodecContext* codec) {
    if (0 != codec->extradata_size) {
        CHECK(NULL == mExtraData);
        mExtraData = new uint8_t[codec->extradata_size];
        mExtraSize = static_cast<uint32_t>(codec->extradata_size);
        memcpy(mExtraData, codec->extradata, mExtraSize);
    }
    return true;
}

bool AVCTSFormatter::addCodecMeta(const sp<MetaData> &meta) const {
    if (0 == mExtraSize) {
        return false;
    }
    ALOGV("mExtraSize=%d,mExtraData=%x",mExtraSize,mExtraData);
    addESDSFromCodecPrivate(meta, false, mExtraData, mExtraSize);
    return true;
}

uint32_t AVCTSFormatter::computeNewESLen(
        const uint8_t* in, uint32_t inAllocLen) const {
    return inAllocLen;
}

int32_t AVCTSFormatter::formatES(
        const uint8_t* in, uint32_t inAllocLen, uint8_t* out,
        uint32_t outAllocLen) const {
    if (!inAllocLen || inAllocLen > outAllocLen) {
        return -1;
    }
    CHECK(in);
    CHECK(out);
    CHECK(in != out);
    memcpy(out, in, inAllocLen);
    return inAllocLen;
}

sp<ABuffer> AVCTSFormatter::parseSei() {
    if (mBuffer == NULL) {
        ALOGE("[parseSei]mBuffer == NULL\n");
        return NULL;
    }
    if (mBuffer->data() == NULL) {
        ALOGE("[parseSei]mBuffer->data() == NULL\n");
        return NULL;
    }
    if (mBuffer->size() <= 0) {
        ALOGE("[parseSei]mBuffer->size():%d\n", mBuffer->size());
        return NULL;
    }

    const uint8_t *data = mBuffer->data();
    size_t size = mBuffer->size();
    Vector<NALPosition> nals;
    size_t totalSize = 0;
    size_t seiCount = 0;
    status_t err;
    const uint8_t *nalStart;
    size_t nalSize;
    bool foundSlice = false;
    bool foundIDR = false;

    while ((err = getNextNALUnit(&data, &size, &nalStart, &nalSize)) == OK) {
        if (nalSize == 0) continue;

        unsigned nalType = nalStart[0] & 0x1f;
        bool flush = false;

        ALOGV("[parseSei]nalType:%d\n", nalType);
        if (nalType == 1 || nalType == 5) {
            if (nalType == 5) {
                foundIDR = true;
            }
            if (foundSlice) {
                ABitReader br(nalStart + 1, nalSize);
                unsigned first_mb_in_slice = parseUE(&br);

                if (first_mb_in_slice == 0) {
                    // This slice starts a new frame.
                    flush = true;
                }
            }

            foundSlice = true;
        } else if ((nalType == 9 || nalType == 7) && foundSlice) {
            // Access unit delimiter and SPS will be associated with the
            // next frame.

            flush = true;
        } else if (nalType == 6 && nalSize > 0) {
            // found non-zero sized SEI
            ++seiCount;
        }

        if (flush) {
            // The access unit will contain all nal units up to, but excluding
            // the current one, separated by 0x00 0x00 0x00 0x01 startcodes.
            size_t auSize = 4 * nals.size() + totalSize;
            sp<ABuffer> accessUnit = new ABuffer(auSize);
            sp<ABuffer> sei;

            ALOGV("[parseSei]seiCount:%d, nals.size():%d\n ", (int)seiCount, (int)nals.size());
            if (seiCount > 0) {
                sei = new ABuffer(seiCount * sizeof(NALPosition));
                accessUnit->meta()->setBuffer("sei", sei);
            }

#if !LOG_NDEBUG
            AString out;
#endif

            size_t dstOffset = 0;
            size_t seiIndex = 0;
            for (size_t i = 0; i < nals.size(); ++i) {
                const NALPosition &pos = nals.itemAt(i);

                unsigned nalType = mBuffer->data()[pos.nalOffset] & 0x1f;

                if (nalType == 6 && pos.nalSize > 0) {
                    if (seiIndex >= sei->size() / sizeof(NALPosition)) {
                        ALOGE("Wrong seiIndex");
                        return NULL;
                    }
                    NALPosition &seiPos = ((NALPosition *)sei->data())[seiIndex++];
                    seiPos.nalOffset = dstOffset + 4;
                    seiPos.nalSize = pos.nalSize;
                }

#if !LOG_NDEBUG
                char tmp[128];
                sprintf(tmp, "0x%02x", nalType);
                if (i > 0) {
                    out.append(", ");
                }
                out.append(tmp);
#endif

                memcpy(accessUnit->data() + dstOffset, "\x00\x00\x00\x01", 4);
                memcpy(accessUnit->data() + dstOffset + 4,
                       mBuffer->data() + pos.nalOffset,
                       pos.nalSize);

                dstOffset += pos.nalSize + 4;
            }

#if !LOG_NDEBUG
            ALOGV("accessUnit contains nal types %s", out.c_str());
#endif

            size_t nextScan = 0;
            const NALPosition &pos = nals.itemAt(nals.size() - 1);
            nextScan = pos.nalOffset + pos.nalSize;
            memmove(mBuffer->data(), mBuffer->data() + nextScan, mBuffer->size() - nextScan);
            mBuffer->setRange(0, mBuffer->size() - nextScan);

            return accessUnit;
        }

        NALPosition pos;
        pos.nalOffset = nalStart - mBuffer->data();
        pos.nalSize = nalSize;
        nals.push(pos);

        totalSize += nalSize;
    }
    if (err != (status_t)-EAGAIN) {
        ALOGE("Unexpeted err");
        return NULL;
    }

    return NULL;
}

status_t AVCTSFormatter::appendData(const void *data, size_t size) {
    if (mBuffer == NULL || mBuffer->size() == 0) {
        uint8_t *ptr = (uint8_t *)data;

        ssize_t startOffset = -1;
        for (size_t i = 0; i + 2 < size; ++i) {
            if (!memcmp("\x00\x00\x01", &ptr[i], 3)) {
                startOffset = i;
                break;
            }
        }

        if (startOffset < 0) {
            return ERROR_MALFORMED;
        }

        if (startOffset > 0) {
            ALOGI("found something resembling an H.264/MPEG syncword "
                  "at offset %zd",
                  startOffset);
        }

        data = &ptr[startOffset];
        size -= startOffset;
    }

    size_t neededSize = (mBuffer == NULL ? 0 : mBuffer->size()) + size;
    if (mBuffer == NULL || neededSize > mBuffer->capacity()) {
        neededSize = (neededSize + 65535) & ~65535;

        ALOGV("resizing buffer to size %zu", neededSize);

        sp<ABuffer> buffer = new ABuffer(neededSize);
        if (mBuffer != NULL) {
            memcpy(buffer->data(), mBuffer->data(), mBuffer->size());
            buffer->setRange(0, mBuffer->size());
        } else {
            buffer->setRange(0, 0);
        }
        mBuffer = buffer;
    }

    memcpy(mBuffer->data() + mBuffer->size(), data, size);
    mBuffer->setRange(0, mBuffer->size() + size);
    return OK;
}

void AVCTSFormatter::queueAccessUnit(const sp<ABuffer> &buffer) {
    mBuffers.push_back(buffer);
}

void AVCTSFormatter::checkNAL(const uint8_t* in, uint32_t inAllocLen) {
    sp<ABuffer> accessUnit;
    appendData(in, inAllocLen);
    while ((accessUnit = parseSei()) != NULL) {
        queueAccessUnit(accessUnit);
    }
}

status_t AVCTSFormatter::dequeueAccessUnit(sp<ABuffer> *buffer) {
    buffer->clear();
    if (!mBuffers.empty()) {
        *buffer = *mBuffers.begin();
        mBuffers.erase(mBuffers.begin());
        return OK;
    }

    return NOT_ENOUGH_DATA;
}

}  // namespace android
