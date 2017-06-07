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
#define LOG_TAG "NU-MPEG2TSExtractor"

#include <inttypes.h>
#include <utils/Log.h>

#include "AmMPEG2TSExtractor.h"
#include "include/NuCachedSource2.h"

#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/ALooper.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MetaData.h>
#include <media/IStreamSource.h>
#include <utils/String8.h>

#include "AmAnotherPacketSource.h"
#include "AmATSParser.h"

namespace android {

static const size_t kTSPacketSize = 188;

struct AmMPEG2TSSource : public MediaSource {
    AmMPEG2TSSource(
            const sp<AmMPEG2TSExtractor> &extractor,
            const sp<AmAnotherPacketSource> &impl,
            bool doesSeek);

    virtual status_t start(MetaData *params = NULL);
    virtual status_t stop();
    virtual sp<MetaData> getFormat();

    virtual status_t read(
            MediaBuffer **buffer, const ReadOptions *options = NULL);

private:
    sp<AmMPEG2TSExtractor> mExtractor;
    sp<AmAnotherPacketSource> mImpl;

    // If there are both audio and video streams, only the video stream
    // will signal seek on the extractor; otherwise the single stream will seek.
    bool mDoesSeek;

    DISALLOW_EVIL_CONSTRUCTORS(AmMPEG2TSSource);
};

AmMPEG2TSSource::AmMPEG2TSSource(
        const sp<AmMPEG2TSExtractor> &extractor,
        const sp<AmAnotherPacketSource> &impl,
        bool doesSeek)
    : mExtractor(extractor),
      mImpl(impl),
      mDoesSeek(doesSeek) {
}

status_t AmMPEG2TSSource::start(MetaData *params) {
    return mImpl->start(params);
}

status_t AmMPEG2TSSource::stop() {
    return mImpl->stop();
}

sp<MetaData> AmMPEG2TSSource::getFormat() {
    return mImpl->getFormat();
}

status_t AmMPEG2TSSource::read(
        MediaBuffer **out, const ReadOptions *options) {
    *out = NULL;

    int64_t seekTimeUs;
    ReadOptions::SeekMode seekMode;
    if (mDoesSeek && options && options->getSeekTo(&seekTimeUs, &seekMode)) {
        ALOGI("mDoesSeek");
        // seek is needed
        status_t err = mExtractor->seek(seekTimeUs, seekMode);
        if (err != OK) {
            return err;
        }
    }

    if (mExtractor->feedUntilBufferAvailable(mImpl) != OK) {
        ALOGI("end of stream %d",__LINE__);
        return ERROR_END_OF_STREAM;
    }
#if 0 //   debug info
    sp<AMessage> format = new AMessage();
    sp<MetaData> meta = getFormat();
    if (meta == NULL) {
        ALOGE("no metadata for track");
        return OK;
    }
    const char *mime;
    CHECK(meta->findCString(kKeyMIMEType, &mime));
    status_t st;
    ALOGI("read %s getBufferedDurationUs %lld count %u",mime,
        (long long)mImpl->getBufferedDurationUs(&st),
        (int)mImpl->getAvailableBufferCount(&st));
#endif
    return mImpl->read(out, options);
}

////////////////////////////////////////////////////////////////////////////////

AmMPEG2TSExtractor::AmMPEG2TSExtractor(const sp<DataSource> &source)
    : mDataSource(source),
      mParser(new AmATSParser(AmATSParser::TS_TIMESTAMPS_ARE_ABSOLUTE)),
      mLastSyncEvent(0),
      mOffset(0) {
      ALOGI("AmMPEG2TSExtractor:AmMPEG2TSExtractor");
    init();
}

size_t AmMPEG2TSExtractor::countTracks() {
    return mSourceImpls.size();
}

sp<IMediaSource> AmMPEG2TSExtractor::getTrack(size_t index) {
    if (index >= mSourceImpls.size()) {
        return NULL;
    }

    // The seek reference track (video if present; audio otherwise) performs
    // seek requests, while other tracks ignore requests.
    return new AmMPEG2TSSource(this, mSourceImpls.editItemAt(index),
            (mSeekSyncPoints == &mSyncPoints.editItemAt(index)));
}

sp<MetaData> AmMPEG2TSExtractor::getTrackMetaData(
        size_t index, uint32_t /* flags */) {
    return index < mSourceImpls.size()
        ? mSourceImpls.editItemAt(index)->getFormat() : NULL;
}

sp<MetaData> AmMPEG2TSExtractor::getMetaData() {
    sp<MetaData> meta = new MetaData;
    meta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_CONTAINER_MPEG2TS);

    return meta;
}

void AmMPEG2TSExtractor::init() {
    bool haveAudio = false;
    bool haveVideo = false;
    int64_t startTime = ALooper::GetNowUs();

    while (feedMore(true /* isInit */) == OK) {
        if (haveAudio && haveVideo) {
            addSyncPoint_l(mLastSyncEvent);
            mLastSyncEvent.reset();
            break;
        }
        if (!haveVideo) {
            sp<AmAnotherPacketSource> impl =
                (AmAnotherPacketSource *)mParser->getSource(
                        AmATSParser::VIDEO).get();

            if (impl != NULL) {
                haveVideo = true;
                mSourceImpls.push(impl);
                mSyncPoints.push();
                mSeekSyncPoints = &mSyncPoints.editTop();
            }
        }

        if (!haveAudio) {
            sp<AmAnotherPacketSource> impl =
                (AmAnotherPacketSource *)mParser->getSource(
                        AmATSParser::AUDIO).get();

            if (impl != NULL) {
                haveAudio = true;
                mSourceImpls.push(impl);
                mSyncPoints.push();
                if (!haveVideo) {
                    mSeekSyncPoints = &mSyncPoints.editTop();
                }
            }
        }

        addSyncPoint_l(mLastSyncEvent);
        mLastSyncEvent.reset();

        // Wait only for 8 seconds to detect audio/video streams.
        if (ALooper::GetNowUs() - startTime > 20000000ll) {
            break;
        }
    }

    off64_t size;
    if (mDataSource->getSize(&size) == OK && (haveAudio || haveVideo) && size > 0) {// udp not to enter
        sp<AmAnotherPacketSource> impl = haveVideo
                ? (AmAnotherPacketSource *)mParser->getSource(
                        AmATSParser::VIDEO).get()
                : (AmAnotherPacketSource *)mParser->getSource(
                        AmATSParser::AUDIO).get();
        size_t prevSyncSize = 1;
        int64_t durationUs = -1;
        List<int64_t> durations;
        // Estimate duration --- stabilize until you get <500ms deviation.
        while (feedMore() == OK
                && ALooper::GetNowUs() - startTime <= 2000000ll) {
            if (mSeekSyncPoints->size() > prevSyncSize) {
                prevSyncSize = mSeekSyncPoints->size();
                int64_t diffUs = mSeekSyncPoints->keyAt(prevSyncSize - 1)
                        - mSeekSyncPoints->keyAt(0);
                off64_t diffOffset = mSeekSyncPoints->valueAt(prevSyncSize - 1)
                        - mSeekSyncPoints->valueAt(0);
                durationUs = size * diffUs / diffOffset;
                durations.push_back(durationUs);
                if (durations.size() > 5) {
                    durations.erase(durations.begin());
                    int64_t min = *durations.begin();
                    int64_t max = *durations.begin();
                    for (List<int64_t>::iterator i = durations.begin();
                            i != durations.end();
                            ++i) {
                        if (min > *i) {
                            min = *i;
                        }
                        if (max < *i) {
                            max = *i;
                        }
                    }
                    if (max - min < 500 * 1000) {
                        break;
                    }
                }
            }
        }
        status_t err;
        int64_t bufferedDurationUs;
        bufferedDurationUs = impl->getBufferedDurationUs(&err);
        if (err == ERROR_END_OF_STREAM) {
            durationUs = bufferedDurationUs;
        }
        if (durationUs > 0) {
            const sp<MetaData> meta = impl->getFormat();
            meta->setInt64(kKeyDuration, durationUs);
            impl->setFormat(meta);
        }
    }

    ALOGI("haveAudio=%d, haveVideo=%d, elaspedTime=%" PRId64,
            haveAudio, haveVideo, ALooper::GetNowUs() - startTime);
}

status_t AmMPEG2TSExtractor::feedMore(bool isInit) {
    Mutex::Autolock autoLock(mLock);
    int64_t startTime = ALooper::GetNowUs();
    uint8_t packet[kTSPacketSize];
    ssize_t n = 0,len = 0;//offset is sync
    status_t err = OK;
    AmATSParser::SyncEvent event(mOffset);
    do {  //this is a problem,mabey it will block
        if (ALooper::GetNowUs() - startTime > 10000000ll) {
            ALOGI("read timeout ");
            break;
        }
        if (err == BAD_VALUE) {
            len = mDataSource->readAt(mOffset,packet,1);
            if (len == 1 && packet[0] == 0x47u) {
                n = 1;
                err = OK;
            }
        }
        len= mDataSource->readAt(mOffset, packet+n, kTSPacketSize-n);
        if (len > 0)
            n += len;
        if (n < (ssize_t)kTSPacketSize)
            continue;
        if (n > 0)
            mOffset += n;
        err = mParser->feedTSPacket(packet, kTSPacketSize, &event);
        if (err != OK) {
            ALOGI("to sync ts header errno %d",err);
            continue;
        }
    }while (err);

    if (event.hasReturnedData()) {
        if (isInit) {
            mLastSyncEvent = event;
        } else {
            addSyncPoint_l(event);
        }
    }
    return err;
}

void AmMPEG2TSExtractor::addSyncPoint_l(const AmATSParser::SyncEvent &event) {
    if (!event.hasReturnedData()) {
        return;
    }

    for (size_t i = 0; i < mSourceImpls.size(); ++i) {
        if (mSourceImpls[i].get() == event.getMediaSource().get()) {
            KeyedVector<int64_t, off64_t> *syncPoints = &mSyncPoints.editItemAt(i);
            syncPoints->add(event.getTimeUs(), event.getOffset());
            // We're keeping the size of the sync points at most 5mb per a track.
            size_t size = syncPoints->size();
            if (size >= 327680) {
                int64_t firstTimeUs = syncPoints->keyAt(0);
                int64_t lastTimeUs = syncPoints->keyAt(size - 1);
                if (event.getTimeUs() - firstTimeUs > lastTimeUs - event.getTimeUs()) {
                    syncPoints->removeItemsAt(0, 4096);
                } else {
                    syncPoints->removeItemsAt(size - 4096, 4096);
                }
            }
            break;
        }
    }
}

uint32_t AmMPEG2TSExtractor::flags() const {
    return /*CAN_PAUSE | CAN_SEEK_BACKWARD | CAN_SEEK_FORWARD*/ 0;//udp this will not surpost
}

status_t AmMPEG2TSExtractor::seek(int64_t seekTimeUs,
        const MediaSource::ReadOptions::SeekMode &seekMode) {
    ALOGI("AmMPEG2TSExtractor=>seek udp is not to seek");
    if (mSeekSyncPoints == NULL || mSeekSyncPoints->isEmpty()) {
        ALOGW("No sync point to seek to.");
        // ... and therefore we have nothing useful to do here.
        return OK;
    }

    // Determine whether we're seeking beyond the known area.
    bool shouldSeekBeyond =
            (seekTimeUs > mSeekSyncPoints->keyAt(mSeekSyncPoints->size() - 1));

    // Determine the sync point to seek.
    size_t index = 0;
    for (; index < mSeekSyncPoints->size(); ++index) {
        int64_t timeUs = mSeekSyncPoints->keyAt(index);
        if (timeUs > seekTimeUs) {
            break;
        }
    }

    switch (seekMode) {
        case MediaSource::ReadOptions::SEEK_NEXT_SYNC:
            if (index == mSeekSyncPoints->size()) {
                ALOGW("Next sync not found; starting from the latest sync.");
                --index;
            }
            break;
        case MediaSource::ReadOptions::SEEK_CLOSEST_SYNC:
        case MediaSource::ReadOptions::SEEK_CLOSEST:
            ALOGW("seekMode not supported: %d; falling back to PREVIOUS_SYNC",
                    seekMode);
            // fall-through
        case MediaSource::ReadOptions::SEEK_PREVIOUS_SYNC:
            if (index == 0) {
                ALOGW("Previous sync not found; starting from the earliest "
                        "sync.");
            } else {
                --index;
            }
            break;
    }
    if (!shouldSeekBeyond || mOffset <= mSeekSyncPoints->valueAt(index)) {
        int64_t actualSeekTimeUs = mSeekSyncPoints->keyAt(index);
        mOffset = mSeekSyncPoints->valueAt(index);
        status_t err = queueDiscontinuityForSeek(actualSeekTimeUs);
        if (err != OK) {
            return err;
        }
    }

    if (shouldSeekBeyond) {
        status_t err = seekBeyond(seekTimeUs);
        if (err != OK) {
            return err;
        }
    }

    // Fast-forward to sync frame.
    for (size_t i = 0; i < mSourceImpls.size(); ++i) {
        const sp<AmAnotherPacketSource> &impl = mSourceImpls[i];
        status_t err;
        feedUntilBufferAvailable(impl);
        while (impl->hasBufferAvailable(&err)) {
            sp<AMessage> meta = impl->getMetaAfterLastDequeued(0);
            sp<ABuffer> buffer;
            if (meta == NULL) {
                ALOGI("UNKNOWN_ERROR");
                return UNKNOWN_ERROR;
            }
            int32_t sync;
            if (meta->findInt32("isSync", &sync) && sync) {
                break;
            }
            err = impl->dequeueAccessUnit(&buffer);
            if (err != OK) {
                return err;
            }
            feedUntilBufferAvailable(impl);
        }
    }

    return OK;
}

status_t AmMPEG2TSExtractor::queueDiscontinuityForSeek(int64_t actualSeekTimeUs) {
    // Signal discontinuity
    sp<AMessage> extra(new AMessage);
    extra->setInt64(IStreamListener::kKeyMediaTimeUs, actualSeekTimeUs);
    mParser->signalDiscontinuity(AmATSParser::DISCONTINUITY_TIME, extra);

    // After discontinuity, impl should only have discontinuities
    // with the last being what we queued. Dequeue them all here.
    for (size_t i = 0; i < mSourceImpls.size(); ++i) {
        const sp<AmAnotherPacketSource> &impl = mSourceImpls.itemAt(i);
        sp<ABuffer> buffer;
        status_t err;
        while (impl->hasBufferAvailable(&err)) {
            if (err != OK) {
                return err;
            }
            err = impl->dequeueAccessUnit(&buffer);
            // If the source contains anything but discontinuity, that's
            // a programming mistake.
            CHECK(err == INFO_DISCONTINUITY);
        }
    }

    // Feed until we have a buffer for each source.
    for (size_t i = 0; i < mSourceImpls.size(); ++i) {
        const sp<AmAnotherPacketSource> &impl = mSourceImpls.itemAt(i);
        sp<ABuffer> buffer;
        status_t err = feedUntilBufferAvailable(impl);
        if (err != OK) {
            return err;
        }
    }

    return OK;
}

status_t AmMPEG2TSExtractor::seekBeyond(int64_t seekTimeUs) {
    // If we're seeking beyond where we know --- read until we reach there.
    size_t syncPointsSize = mSeekSyncPoints->size();

    while (seekTimeUs > mSeekSyncPoints->keyAt(
            mSeekSyncPoints->size() - 1)) {
        status_t err;
        if (syncPointsSize < mSeekSyncPoints->size()) {
            syncPointsSize = mSeekSyncPoints->size();
            int64_t syncTimeUs = mSeekSyncPoints->keyAt(syncPointsSize - 1);
            // Dequeue buffers before sync point in order to avoid too much
            // cache building up.
            sp<ABuffer> buffer;
            for (size_t i = 0; i < mSourceImpls.size(); ++i) {
                const sp<AmAnotherPacketSource> &impl = mSourceImpls[i];
                int64_t timeUs;
                while ((err = impl->nextBufferTime(&timeUs)) == OK) {
                    if (timeUs < syncTimeUs) {
                        impl->dequeueAccessUnit(&buffer);
                    } else {
                        break;
                    }
                }
                if (err != OK && err != -EWOULDBLOCK) {
                    return err;
                }
            }
        }
        if (feedMore() != OK) {
            ALOGI("end of stream %d",__LINE__);
            return ERROR_END_OF_STREAM;
        }
    }

    return OK;
}

status_t AmMPEG2TSExtractor::feedUntilBufferAvailable(
        const sp<AmAnotherPacketSource> &impl) {
    status_t finalResult;
    while (!impl->hasBufferAvailable(&finalResult)) {
        if (finalResult != OK) {
            return finalResult;
        }

        status_t err = feedMore();
        if (err != OK) {
            impl->signalEOS(err);
        }
    }
    return OK;
}

////////////////////////////////////////////////////////////////////////////////

bool SniffMPEG2TS(
        const sp<DataSource> &source, String8 *mimeType, float *confidence,
        sp<AMessage> *) {
    for (int i = 0; i < 5; ++i) {
        char header;
        if (source->readAt(kTSPacketSize * i, &header, 1) != 1
                || header != 0x47) {
            return false;
        }
    }

    *confidence = 0.1f;
    mimeType->setTo(MEDIA_MIMETYPE_CONTAINER_MPEG2TS);

    return true;
}

}  // namespace android
