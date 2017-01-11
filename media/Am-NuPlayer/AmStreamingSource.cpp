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
#define LOG_TAG "NU-StreamingSource"
#include <utils/Log.h>

#include "AmStreamingSource.h"

#include "AmATSParser.h"
#include "AmAnotherPacketSource.h"
#include "AmNuPlayerStreamListener.h"

#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/Utils.h>

namespace android {

const int32_t kNumListenerQueuePackets = 80;

AmNuPlayer::StreamingSource::StreamingSource(
        const sp<AMessage> &notify,
        const sp<IStreamSource> &source)
    : Source(notify),
      mSource(source),
      mFinalResult(OK),
      mBuffering(false) {
}

AmNuPlayer::StreamingSource::~StreamingSource() {
    if (mLooper != NULL) {
        mLooper->unregisterHandler(id());
        mLooper->stop();
    }
}

void AmNuPlayer::StreamingSource::prepareAsync() {
    if (mLooper == NULL) {
        mLooper = new ALooper;
        mLooper->setName("streaming");
        mLooper->start();

        mLooper->registerHandler(this);
    }

    notifyVideoSizeChanged();
    notifyFlagsChanged(0);
    notifyPrepared();
}

void AmNuPlayer::StreamingSource::start() {
    mStreamListener = new NuPlayerStreamListener(mSource, NULL);

    uint32_t sourceFlags = mSource->flags();

    uint32_t parserFlags = AmATSParser::TS_TIMESTAMPS_ARE_ABSOLUTE;
    if (sourceFlags & IStreamSource::kFlagAlignedVideoData) {
        parserFlags |= AmATSParser::ALIGNED_VIDEO_DATA;
    }

    mTSParser = new AmATSParser(parserFlags);

    mStreamListener->start();

    postReadBuffer();
}

status_t AmNuPlayer::StreamingSource::feedMoreTSData() {
    return postReadBuffer();
}

void AmNuPlayer::StreamingSource::onReadBuffer() {
    for (int32_t i = 0; i < kNumListenerQueuePackets; ++i) {
        char buffer[188];
        sp<AMessage> extra;
        ssize_t n = mStreamListener->read(buffer, sizeof(buffer), &extra);

        if (n == 0) {
            ALOGI("input data EOS reached.");
            mTSParser->signalEOS(ERROR_END_OF_STREAM);
            setError(ERROR_END_OF_STREAM);
            break;
        } else if (n == INFO_DISCONTINUITY) {
            int32_t type = AmATSParser::DISCONTINUITY_TIME;

            int32_t mask;
            if (extra != NULL
                    && extra->findInt32(
                        IStreamListener::kKeyDiscontinuityMask, &mask)) {
                if (mask == 0) {
                    ALOGE("Client specified an illegal discontinuity type.");
                    setError(ERROR_UNSUPPORTED);
                    break;
                }

                type = mask;
            }

            mTSParser->signalDiscontinuity(
                    (AmATSParser::DiscontinuityType)type, extra);
        } else if (n < 0) {
            break;
        } else {
            if (buffer[0] == 0x00) {
                // XXX legacy

                if (extra == NULL) {
                    extra = new AMessage;
                }

                uint8_t type = buffer[1];

                if (type & 2) {
                    int64_t mediaTimeUs;
                    memcpy(&mediaTimeUs, &buffer[2], sizeof(mediaTimeUs));

                    extra->setInt64(IStreamListener::kKeyMediaTimeUs, mediaTimeUs);
                }

                mTSParser->signalDiscontinuity(
                        ((type & 1) == 0)
                            ? AmATSParser::DISCONTINUITY_TIME
                            : AmATSParser::DISCONTINUITY_FORMATCHANGE,
                        extra);
            } else {
                status_t err = mTSParser->feedTSPacket(buffer, sizeof(buffer));

                if (err != OK) {
                    ALOGE("TS Parser returned error %d", err);

                    mTSParser->signalEOS(err);
                    setError(err);
                    break;
                }
            }
        }
    }
}

status_t AmNuPlayer::StreamingSource::postReadBuffer() {
    {
        Mutex::Autolock _l(mBufferingLock);
        if (mFinalResult != OK) {
            return mFinalResult;
        }
        if (mBuffering) {
            return OK;
        }
        mBuffering = true;
    }

    (new AMessage(kWhatReadBuffer, this))->post();
    return OK;
}

bool AmNuPlayer::StreamingSource::haveSufficientDataOnAllTracks() {
    // We're going to buffer at least 2 secs worth data on all tracks before
    // starting playback (both at startup and after a seek).

    static const int64_t kMinDurationUs = 2000000ll;

    sp<AmAnotherPacketSource> audioTrack = getSource(true /*audio*/);
    sp<AmAnotherPacketSource> videoTrack = getSource(false /*audio*/);

    status_t err;
    int64_t durationUs;
    if (audioTrack != NULL
            && (durationUs = audioTrack->getBufferedDurationUs(&err))
                    < kMinDurationUs
            && err == OK) {
        ALOGV("audio track doesn't have enough data yet. (%.2f secs buffered)",
              durationUs / 1E6);
        return false;
    }

    if (videoTrack != NULL
            && (durationUs = videoTrack->getBufferedDurationUs(&err))
                    < kMinDurationUs
            && err == OK) {
        ALOGV("video track doesn't have enough data yet. (%.2f secs buffered)",
              durationUs / 1E6);
        return false;
    }

    return true;
}

void AmNuPlayer::StreamingSource::setError(status_t err) {
    Mutex::Autolock _l(mBufferingLock);
    mFinalResult = err;
}

sp<AmAnotherPacketSource> AmNuPlayer::StreamingSource::getSource(bool audio) {
    if (mTSParser == NULL) {
        return NULL;
    }

    sp<MediaSource> source = mTSParser->getSource(
            audio ? AmATSParser::AUDIO : AmATSParser::VIDEO);

    return static_cast<AmAnotherPacketSource *>(source.get());
}

sp<AMessage> AmNuPlayer::StreamingSource::getFormat(bool audio) {
    sp<AmAnotherPacketSource> source = getSource(audio);

    sp<AMessage> format = new AMessage;
    if (source == NULL) {
        format->setInt32("err", -EWOULDBLOCK);
        return format;
    }

    sp<MetaData> meta = source->getFormat();
    status_t err = convertMetaDataToMessage(meta, &format);
    if (err != OK) {
        format->setInt32("err", err);
    }
    return format;
}

status_t AmNuPlayer::StreamingSource::dequeueAccessUnit(
        bool audio, sp<ABuffer> *accessUnit) {
    sp<AmAnotherPacketSource> source = getSource(audio);

    if (source == NULL) {
        return -EWOULDBLOCK;
    }

    if (!haveSufficientDataOnAllTracks()) {
        postReadBuffer();
    }

    status_t finalResult;
    if (!source->hasBufferAvailable(&finalResult)) {
        return finalResult == OK ? -EWOULDBLOCK : finalResult;
    }

    status_t err = source->dequeueAccessUnit(accessUnit);

#if !defined(LOG_NDEBUG) || LOG_NDEBUG == 0
    if (err == OK) {
        int64_t timeUs;
        CHECK((*accessUnit)->meta()->findInt64("timeUs", &timeUs));
        ALOGV("dequeueAccessUnit timeUs=%lld us", timeUs);
    }
#endif

    return err;
}

bool AmNuPlayer::StreamingSource::isRealTime() const {
    return mSource->flags() & IStreamSource::kFlagIsRealTimeData;
}

void AmNuPlayer::StreamingSource::onMessageReceived(
        const sp<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatReadBuffer:
        {
            onReadBuffer();

            {
                Mutex::Autolock _l(mBufferingLock);
                mBuffering = false;
            }
            break;
        }
        default:
        {
            TRESPASS();
        }
    }
}


}  // namespace android

