/*
 * Copyright (C) 2010 The Android Open Source Project
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
#define LOG_TAG "NU-AmAnotherPacketSource"

#include "AmAnotherPacketSource.h"

#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/foundation/AString.h>
#include <media/stagefright/foundation/hexdump.h>
#include <media/stagefright/MediaBuffer.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MetaData.h>
#include <utils/Vector.h>

#include <inttypes.h>

namespace android {

#define PEEK_TIMEUS_THRESHOLD 10

const int64_t kNearEOSMarkUs = 2000000ll; // 2 secs

AmAnotherPacketSource::AmAnotherPacketSource(const sp<MetaData> &meta)
    : mIsAudio(false),
      mIsVideo(false),
      mIsValid(true),
      mFormat(NULL),
      mLastQueuedTimeUs(0),
      mEOSResult(OK),
      mLatestEnqueuedMeta(NULL),
      mLatestDequeuedMeta(NULL),
      mQueuedDiscontinuityCount(0),
      mEstimatedBytePerSec(0) {
    setFormat(meta);
}

void AmAnotherPacketSource::setFormat(const sp<MetaData> &meta) {
    CHECK(mFormat == NULL);

    mIsAudio = false;
    mIsVideo = false;

    if (meta == NULL) {
        return;
    }

    mFormat = meta;
    const char *mime;
    CHECK(meta->findCString(kKeyMIMEType, &mime));

    if (!strncasecmp("audio/", mime, 6)) {
        mIsAudio = true;
    } else  if (!strncasecmp("video/", mime, 6)) {
        mIsVideo = true;
    } else {
        CHECK(!strncasecmp("text/", mime, 5));
    }
}

AmAnotherPacketSource::~AmAnotherPacketSource() {
}

status_t AmAnotherPacketSource::start(MetaData * /* params */) {
    return OK;
}

status_t AmAnotherPacketSource::stop() {
    return OK;
}

sp<MetaData> AmAnotherPacketSource::getFormat() {
    Mutex::Autolock autoLock(mLock);
    if (mFormat != NULL) {
        return mFormat;
    }

    List<sp<ABuffer> >::iterator it = mBuffers.begin();
    while (it != mBuffers.end()) {
        sp<ABuffer> buffer = *it;
        int32_t discontinuity;
        if (buffer->meta()->findInt32("discontinuity", &discontinuity)) {
            break;
        }

        sp<RefBase> object;
        if (buffer->meta()->findObject("format", &object)) {
            return mFormat = static_cast<MetaData*>(object.get());
        }

        ++it;
    }
    return NULL;
}

status_t AmAnotherPacketSource::dequeueAccessUnit(sp<ABuffer> *buffer) {
    buffer->clear();

    Mutex::Autolock autoLock(mLock);
    while (mEOSResult == OK && mBuffers.empty()) {
        mCondition.wait(mLock);
    }

    if (!mBuffers.empty()) {
        *buffer = *mBuffers.begin();
        mBuffers.erase(mBuffers.begin());

        int32_t discontinuity;
        if ((*buffer)->meta()->findInt32("discontinuity", &discontinuity)) {
            if (wasFormatChange(discontinuity)) {
                mFormat.clear();
            }

            --mQueuedDiscontinuityCount;
            return INFO_DISCONTINUITY;
        }

        mLatestDequeuedMeta = (*buffer)->meta()->dup();

        sp<RefBase> object;
        if ((*buffer)->meta()->findObject("format", &object)) {
            mFormat = static_cast<MetaData*>(object.get());
        }

        return OK;
    }

    return mEOSResult;
}

status_t AmAnotherPacketSource::read(
        MediaBuffer **out, const ReadOptions *) {
    *out = NULL;

    Mutex::Autolock autoLock(mLock);
    while (mEOSResult == OK && mBuffers.empty()) {
        mCondition.wait(mLock);
    }

    if (!mBuffers.empty()) {

        const sp<ABuffer> buffer = *mBuffers.begin();
        mBuffers.erase(mBuffers.begin());
        mLatestDequeuedMeta = buffer->meta()->dup();

        int32_t discontinuity;
        if (buffer->meta()->findInt32("discontinuity", &discontinuity)) {
            if (wasFormatChange(discontinuity)) {
                mFormat.clear();
            }

            return INFO_DISCONTINUITY;
        }

        sp<RefBase> object;
        if (buffer->meta()->findObject("format", &object)) {
            mFormat = static_cast<MetaData*>(object.get());
        }

        int64_t timeUs;
        CHECK(buffer->meta()->findInt64("timeUs", &timeUs));

        MediaBuffer *mediaBuffer = new MediaBuffer(buffer);

        mediaBuffer->meta_data()->setInt64(kKeyTime, timeUs);

        *out = mediaBuffer;
        return OK;
    }

    return mEOSResult;
}

bool AmAnotherPacketSource::wasFormatChange(
        int32_t discontinuityType) const {
    if (mIsAudio) {
        return (discontinuityType & AmATSParser::DISCONTINUITY_AUDIO_FORMAT) != 0;
    }

    if (mIsVideo) {
        return (discontinuityType & AmATSParser::DISCONTINUITY_VIDEO_FORMAT) != 0;
    }

    return false;
}

void AmAnotherPacketSource::queueAccessUnit(const sp<ABuffer> &buffer) {
    int32_t damaged;
    if (buffer->meta()->findInt32("damaged", &damaged) && damaged) {
        // LOG(VERBOSE) << "discarding damaged AU";
        return;
    }

    int64_t lastQueuedTimeUs;
    CHECK(buffer->meta()->findInt64("timeUs", &lastQueuedTimeUs));
    mLastQueuedTimeUs = lastQueuedTimeUs;
    ALOGV("queueAccessUnit timeUs=%" PRIi64 " us (%.2f secs)", mLastQueuedTimeUs, mLastQueuedTimeUs / 1E6);

    Mutex::Autolock autoLock(mLock);
    mBuffers.push_back(buffer);
    mCondition.signal();

    int32_t discontinuity;
    if (buffer->meta()->findInt32("discontinuity", &discontinuity)) {
        ++mQueuedDiscontinuityCount;
    } else {
        if (!mEstimatedBytePerSec) {
            status_t dummy;
            getBufferedDurationUs_l(&dummy, &mEstimatedBytePerSec);
            if (mEstimatedBytePerSec) {
                ALOGI("[%s] estimate bytes by second : %lld !", mIsAudio ? "audio" : "video", mEstimatedBytePerSec);
            }
        }
    }

    if (mLatestEnqueuedMeta == NULL) {
        mLatestEnqueuedMeta = buffer->meta()->dup();
    } else {
        int64_t latestTimeUs = 0;
        int64_t frameDeltaUs = 0;
        CHECK(mLatestEnqueuedMeta->findInt64("timeUs", &latestTimeUs));
        if (lastQueuedTimeUs > latestTimeUs) {
            mLatestEnqueuedMeta = buffer->meta()->dup();
            frameDeltaUs = lastQueuedTimeUs - latestTimeUs;
            mLatestEnqueuedMeta->setInt64("durationUs", frameDeltaUs);
        } else if (!mLatestEnqueuedMeta->findInt64("durationUs", &frameDeltaUs)) {
            // For B frames
            frameDeltaUs = latestTimeUs - lastQueuedTimeUs;
            mLatestEnqueuedMeta->setInt64("durationUs", frameDeltaUs);
        }
    }
}

void AmAnotherPacketSource::clear() {
    Mutex::Autolock autoLock(mLock);

    mBuffers.clear();
    mEOSResult = OK;
    mQueuedDiscontinuityCount = 0;

    mFormat = NULL;
    mLatestEnqueuedMeta = NULL;
}

void AmAnotherPacketSource::queueDiscontinuity(
        AmATSParser::DiscontinuityType type,
        const sp<AMessage> &extra,
        bool discard) {
    Mutex::Autolock autoLock(mLock);

    if (discard) {
        // Leave only discontinuities in the queue.
        List<sp<ABuffer> >::iterator it = mBuffers.begin();
        while (it != mBuffers.end()) {
            sp<ABuffer> oldBuffer = *it;

            int32_t oldDiscontinuityType;
            if (!oldBuffer->meta()->findInt32(
                        "discontinuity", &oldDiscontinuityType)) {
                it = mBuffers.erase(it);
                continue;
            }

            ++it;
        }
    }

    mEOSResult = OK;
    mLastQueuedTimeUs = 0;
    mLatestEnqueuedMeta = NULL;

    if (type == AmATSParser::DISCONTINUITY_NONE) {
        return;
    }

    ++mQueuedDiscontinuityCount;
    sp<ABuffer> buffer = new ABuffer(0);
    buffer->meta()->setInt32("discontinuity", static_cast<int32_t>(type));
    buffer->meta()->setMessage("extra", extra);

    mBuffers.push_back(buffer);
    mCondition.signal();
}

void AmAnotherPacketSource::signalEOS(status_t result) {
    //CHECK(result != OK);

    Mutex::Autolock autoLock(mLock);
    mEOSResult = result;
    mCondition.signal();
}

bool AmAnotherPacketSource::hasBufferAvailable(status_t *finalResult) {
    Mutex::Autolock autoLock(mLock);
    if (!mBuffers.empty()) {
        return true;
    }

    *finalResult = mEOSResult;
    return false;
}

int64_t AmAnotherPacketSource::getBufferedDurationUs(status_t *finalResult) {
    Mutex::Autolock autoLock(mLock);
    return getBufferedDurationUs_l(finalResult);
}

int64_t AmAnotherPacketSource::getBufferedDataSize() {
    Mutex::Autolock autoLock(mLock);
    if (mBuffers.empty()) {
        return 0;
    }
    int64_t data_size_bytes = 0;
    List<sp<ABuffer> >::iterator it = mBuffers.begin();
    while (it != mBuffers.end()) {
        const sp<ABuffer> &buffer = *it;
        data_size_bytes += buffer->size();
        ++it;
    }
    return data_size_bytes;
}

int64_t AmAnotherPacketSource::getEstimatedBytesPerSec() {
    Mutex::Autolock autoLock(mLock);
    return mEstimatedBytePerSec;
}

int64_t AmAnotherPacketSource::getBufferedDurationUs_l(status_t *finalResult, int64_t *estimateBytePerSec) {
    *finalResult = mEOSResult;

    if (mBuffers.empty()) {
        return 0;
    }

    int64_t time1 = -1;
    int64_t time2 = -1;
    int64_t durationUs = 0;
    int64_t dataSize = 0;

    List<sp<ABuffer> >::iterator it = mBuffers.begin();
    while (it != mBuffers.end()) {
        const sp<ABuffer> &buffer = *it;

        dataSize += buffer->size();

        int64_t timeUs;
        if (buffer->meta()->findInt64("timeUs", &timeUs)) {
            if (time1 < 0 || timeUs < time1) {
                time1 = timeUs;
            }

            if (time2 < 0 || timeUs > time2) {
                time2 = timeUs;
            }
        } else {
            // This is a discontinuity, reset everything.
            durationUs += time2 - time1;
            time1 = time2 = -1;
        }

        ++it;
    }

    int64_t result_dur = durationUs + (time2 - time1);
    if (estimateBytePerSec) {
        if (result_dur > 2000000) {
            *estimateBytePerSec = dataSize / 2;
        } else {
            *estimateBytePerSec = 0;
        }
    }

    return result_dur;
}

// A cheaper but less precise version of getBufferedDurationUs that we would like to use in
// AmLiveSession::dequeueAccessUnit to trigger downwards adaptation.
int64_t AmAnotherPacketSource::getEstimatedDurationUs() {
    Mutex::Autolock autoLock(mLock);
    if (mBuffers.empty()) {
        return 0;
    }

    if (mQueuedDiscontinuityCount > 0) {
        status_t finalResult;
        return getBufferedDurationUs_l(&finalResult);
    }

    List<sp<ABuffer> >::iterator it = mBuffers.begin();
    sp<ABuffer> buffer = *it;

    int64_t startTimeUs;
    buffer->meta()->findInt64("timeUs", &startTimeUs);
    if (startTimeUs < 0) {
        return 0;
    }

    it = mBuffers.end();
    --it;
    buffer = *it;

    int64_t endTimeUs;
    buffer->meta()->findInt64("timeUs", &endTimeUs);
    if (endTimeUs < 0) {
        return 0;
    }

    int64_t diffUs;
    if (endTimeUs > startTimeUs) {
        diffUs = endTimeUs - startTimeUs;
    } else {
        diffUs = startTimeUs - endTimeUs;
    }
    return diffUs;
}

status_t AmAnotherPacketSource::nextBufferTime(int64_t *timeUs) {
    *timeUs = 0;

    Mutex::Autolock autoLock(mLock);

    if (mBuffers.empty()) {
        return mEOSResult != OK ? mEOSResult : -EWOULDBLOCK;
    }

    sp<ABuffer> buffer = *mBuffers.begin();
    CHECK(buffer->meta()->findInt64("timeUs", timeUs));

    return OK;
}

int64_t AmAnotherPacketSource::peekFirstVideoTimeUs() {
    Mutex::Autolock autoLock(mLock);
    if (mIsAudio || mBuffers.size() < PEEK_TIMEUS_THRESHOLD) {
        return -1;
    }
    int32_t count = 0;
    int64_t timeUs = 0, min_timeUs = -1;
    List<sp<ABuffer> >::iterator it = mBuffers.begin();
    while (it != mBuffers.end() && count++ < PEEK_TIMEUS_THRESHOLD) {
        const sp<ABuffer> &buffer = *it;
        buffer->meta()->findInt64("timeUs", &timeUs);
        if (min_timeUs < 0) {
            min_timeUs = timeUs;
        } else {
            min_timeUs = (timeUs < min_timeUs) ? timeUs : min_timeUs;
        }
        ++it;
    }
    return min_timeUs;
}

bool AmAnotherPacketSource::isFinished(int64_t duration) const {
    if (duration > 0) {
        int64_t diff = duration - mLastQueuedTimeUs;
        if (diff < kNearEOSMarkUs && diff > -kNearEOSMarkUs) {
            ALOGV("Detecting EOS due to near end");
            return true;
        }
    }
    return (mEOSResult != OK);
}

sp<AMessage> AmAnotherPacketSource::getLatestEnqueuedMeta() {
    Mutex::Autolock autoLock(mLock);
    return mLatestEnqueuedMeta;
}

sp<AMessage> AmAnotherPacketSource::getLatestDequeuedMeta() {
    Mutex::Autolock autoLock(mLock);
    return mLatestDequeuedMeta;
}

void AmAnotherPacketSource::setValid(bool valid) {
    Mutex::Autolock autoLock(mLock);
    mIsValid = valid;
}

bool AmAnotherPacketSource::getValid() {
    Mutex::Autolock autoLock(mLock);
    return mIsValid;
}

}  // namespace android
