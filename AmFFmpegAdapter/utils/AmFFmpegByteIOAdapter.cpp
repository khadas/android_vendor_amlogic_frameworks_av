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
#define LOG_TAG "AmFFmpegByteIOAdapter"
#include <utils/Log.h>

#include <assert.h>
#include <stdint.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avio.h>
#undef CodecType
}

#include <media/stagefright/DataSource.h>
#include <media/stagefright/foundation/ADebug.h>

#include "AmFFmpegByteIOAdapter.h"

namespace android {

static uint32_t kAdapterBufferSize = 32768;

AmFFmpegByteIOAdapter::AmFFmpegByteIOAdapter()
    : mInitCheck(false),
      mContext(NULL),
      mNextReadPos(0) {
}

AmFFmpegByteIOAdapter::~AmFFmpegByteIOAdapter() {
    if (mInitCheck && NULL != mContext->buffer) {
        // This may be the original buffer we allocated for init_put_byte or
        // ffmpeg may have freed it and replaced it with another one in the
        // AVIOContext. In either case we need to free the one now attached
        // to the AVIOContext.
        av_free(mContext->buffer);
    }
    av_free(mContext);
}

bool AmFFmpegByteIOAdapter::init(sp<DataSource> src) {
    // Make certain the parameters the called passed are reasonable.
    if (NULL == src.get()) {
        ALOGE("Input source should not be NULL.");
        return false;
    }

    // Already inited?  If so, this is an error.
    if (mInitCheck) {
        ALOGW("Adapter is already initialized.");
        return false;
    }

    uint32_t targetSize = kAdapterBufferSize + FF_INPUT_BUFFER_PADDING_SIZE;
    // Need to use ffmpeg's buffer allocation/free routines for this memory - it
    // may re-allocate the buffer during play.
    uint8_t *buffer = static_cast<uint8_t *>(av_mallocz(targetSize));
    if (NULL != buffer) {
        mContext = avio_alloc_context(
                buffer,
                static_cast<int>(kAdapterBufferSize),
                0,  // write_flag = false.
                static_cast<void *>(this),
                staticRead,
                staticWrite,
                staticSeek);
        if (mContext != NULL) {
            mSource = src;
            mInitCheck = true;
        } else {
            ALOGE("Failed to initialize AVIOContext.");
        }
    } else {
        ALOGE("Failed to allocate %u bytes for ByteIOAdapter.", targetSize);
    }

    if (!mInitCheck) {
        av_free(buffer);
        buffer = NULL;
        mSource = NULL;
    }

    return mInitCheck;
}

// On success, the number of bytes read is returned. On error, -1 is returned.
int AmFFmpegByteIOAdapter::read(uint8_t* buf, int amt) {
    if (!mInitCheck || NULL == buf) {
        return -1;
    }
    if (!amt) {
        return 0;
    }

    CHECK(NULL != mSource.get());
    // TODO: ChrumiumHTTPDataSource::initCheck() sometimes returns non-OK value
    // even if it is connected.
#if 0
    if (OK != mSource->initCheck()) {
        return -1;
    }
#endif

    ssize_t result = 0;
    uint64_t pos = mNextReadPos;

    ALOGV("readAt pos %lld amt %d", pos, amt);
    result = mSource->readAt(pos, buf, amt);
    if (result > 0) {
        mNextReadPos += result;
    }

    return static_cast<int>(result);
}

// Upon successful completion, returns the current position after seeking.
// If whence is AVSEEK_SIZE, returns the size of underlying source.
// Otherwise, -1 is returned.
int64_t AmFFmpegByteIOAdapter::seek(int64_t offset, int whence) {
    // TODO: ChrumiumHTTPDataSource::initCheck() sometimes returns non-OK value
    // even if it is connected.
    // if (!mInitCheck || OK != mSource->initCheck()) {
    if (!mInitCheck) {
        return -1;
    }

    int64_t target = -1;
    int64_t size = 0;
    bool sizeSupported = (OK == mSource->getSize(&size));

    switch(whence) {
    case SEEK_SET:
        target = offset;
        break;
    case SEEK_CUR:
        target = mNextReadPos + offset;
        break;
    case SEEK_END:
        if (sizeSupported) {
            target = size + offset;
        }
        break;
    case AVSEEK_SIZE:
        return size;
    default:
        ALOGE("Invalid seek whence (%d) in ByteIOAdapter::Seek", whence);
    }

    if ((target < 0) || (target > size)) {
        ALOGW("Invalid seek request to %lld (size: %lld).", target, size);
        return -1;
    }

    mNextReadPos = target;

    return target;
}

int32_t AmFFmpegByteIOAdapter::staticRead(void* thiz, uint8_t* buf, int amt) {
    CHECK(thiz);
    return static_cast<AmFFmpegByteIOAdapter *>(thiz)->read(buf, amt);
}

int32_t AmFFmpegByteIOAdapter::staticWrite(void* thiz, uint8_t* buf, int amt) {
    ALOGE("Write operation is not allowed.");
    return -1;
}

int64_t AmFFmpegByteIOAdapter::staticSeek(
        void* thiz, int64_t offset, int whence) {
    CHECK(thiz);
    return static_cast<AmFFmpegByteIOAdapter *>(thiz)->seek(offset, whence);
}

}  // namespace android
