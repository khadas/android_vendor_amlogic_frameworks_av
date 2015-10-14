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
#define LOG_TAG "NU-LiveSession"
#include <utils/Log.h>

#include "AmLiveSession.h"

#include "AmM3UParser.h"
#include "AmPlaylistFetcher.h"
#include "include/HEVC_utils.h"
#include "StreamSniffer.h"

#include "include/HTTPBase.h"
#include "mpeg2ts/AnotherPacketSource.h"

#include <cutils/properties.h>
#include <media/IMediaHTTPConnection.h>
#include <media/IMediaHTTPService.h>
#include <media/stagefright/foundation/hexdump.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/FileSource.h>
#include <media/stagefright/MediaErrors.h>
#include "include/HTTPBase.h"

#include <media/stagefright/MediaHTTP.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/Utils.h>

#include <utils/Mutex.h>

#include <ctype.h>
#include <inttypes.h>
#include <openssl/aes.h>
#include <openssl/md5.h>

namespace android {

// Number of recently-read bytes to use for bandwidth estimation
const size_t LiveSession::kBandwidthHistoryBytes = 200 * 1024;
const int64_t kNearEOSTimeoutUs = 2000000ll; // 2 secs
const size_t kSizePerRead = 1500;

//static
const String8 LiveSession::kHTTPUserAgentDefault("Mozilla/5.0 (Linux; Android 5.1.1) AppleWebKit/537.36 (KHTML, like Gecko) Version/4.0 Chrome/39.0.0.0 Safari/537.36");

LiveSession::LiveSession(
        const sp<AMessage> &notify, uint32_t flags,
        const sp<IMediaHTTPService> &httpService, interruptcallback pfunc)
    : mInterruptCallback(pfunc),
      mNotify(notify),
      mFlags(flags),
      mHTTPService(httpService),
      mBuffTimeSec(2),
      mFailureWaitSec(0),
      mAbnormalWaitSec(0),
      mFirstSniff(true),
      mDebug(false),
      mCodecSpecificDataSend(false),
      mSeeked(false),
      mNeedExit(false),
      mInPreparationPhase(true),
      mCodecSpecificData(NULL),
      mCodecSpecificDataSize(0),
      mCurBandwidthIndex(-1),
      mStreamMask(0),
      mNewStreamMask(0),
      mSwapMask(0),
      mEstimatedBWbps(0),
      mCheckBandwidthGeneration(0),
      mSwitchGeneration(0),
      mSubtitleGeneration(0),
      mLastDequeuedTimeUs(0ll),
      mRealTimeBaseUs(0ll),
      mReconfigurationInProgress(false),
      mSwitchInProgress(false),
      mDisconnectReplyID(0),
      mSeekReplyID(0),
      mFirstTimeUsValid(false),
      mFirstTimeUs(0),
      mLastSeekTimeUs(0),
      mEOSTimeoutAudio(0),
      mEOSTimeoutVideo(0) {
    char value[PROPERTY_VALUE_MAX];
    if (property_get("media.hls.read_pts", value, NULL)
        && (!strcmp(value, "1") || !strcasecmp(value, "true"))) {
        mDebug = true;
    }
    char value1[PROPERTY_VALUE_MAX];
    if (property_get("media.hls.bufftime_s", value1, NULL)) {
        mBuffTimeSec = atoi(value1);
    }
    char value2[PROPERTY_VALUE_MAX];
    if (property_get("media.hls.failure_wait_sec", value2, "30")) {
        mFailureWaitSec = atoi(value2);
    }
    char value3[PROPERTY_VALUE_MAX];
    if (property_get("media.hls.abnormal_wait_sec", value3, "3600")) {
        mAbnormalWaitSec = atoi(value3);
    }

    mStreams[kAudioIndex] = StreamItem("audio");
    mStreams[kVideoIndex] = StreamItem("video");
    mStreams[kSubtitleIndex] = StreamItem("subtitles");

    for (size_t i = 0; i < kMaxStreams; ++i) {
        mDiscontinuities.add(indexToType(i), new AnotherPacketSource(NULL /* meta */));
        mPacketSources.add(indexToType(i), new AnotherPacketSource(NULL /* meta */));
        mPacketSources2.add(indexToType(i), new AnotherPacketSource(NULL /* meta */));
        mBuffering[i] = false;
    }

    curl_global_init(CURL_GLOBAL_ALL);
}

LiveSession::~LiveSession() {
    if (mCodecSpecificData != NULL) {
        free(mCodecSpecificData);
        mCodecSpecificData = NULL;
    }
    curl_global_cleanup();
}

void LiveSession::setParentThreadId(android_thread_id_t thread_id) {
    mParentThreadId = thread_id;
}

sp<ABuffer> LiveSession::createFormatChangeBuffer(bool swap) {
    ABuffer *discontinuity = new ABuffer(0);
    discontinuity->meta()->setInt32("discontinuity", ATSParser::DISCONTINUITY_FORMATCHANGE);
    discontinuity->meta()->setInt32("swapPacketSource", swap);
    discontinuity->meta()->setInt32("switchGeneration", mSwitchGeneration);
    discontinuity->meta()->setInt64("timeUs", -1);
    return discontinuity;
}

void LiveSession::swapPacketSource(StreamType stream) {
    sp<AnotherPacketSource> &aps = mPacketSources.editValueFor(stream);
    sp<AnotherPacketSource> &aps2 = mPacketSources2.editValueFor(stream);
    sp<AnotherPacketSource> tmp = aps;
    aps = aps2;
    aps2 = tmp;
    aps2->clear();
}

bool LiveSession::haveSufficientDataOnAVTracks() {
    // buffer 2secs data
    static const int64_t kMinDurationUs = mBuffTimeSec * 1000000ll;

    sp<AnotherPacketSource> audioTrack = mPacketSources.valueFor(STREAMTYPE_AUDIO);
    sp<AnotherPacketSource> videoTrack = mPacketSources.valueFor(STREAMTYPE_VIDEO);

    if (audioTrack == NULL && videoTrack == NULL) {
        ALOGI("no audio and video track!\n");
        return false;
    }

    int64_t mediaDurationUs = 0;
    getDuration(&mediaDurationUs);
    if ((audioTrack != NULL && audioTrack->isFinished(mediaDurationUs))
            || (videoTrack != NULL && videoTrack->isFinished(mediaDurationUs))) {
        ALOGI("audio or video finished!\n");
        return true;
    }

    status_t err;
    int64_t durationUs, estimate;
    bool buffer_flag = true;
    if (audioTrack != NULL) {
        estimate = audioTrack->getEstimatedBytesPerSec();
        // just estimate, prevent wrong duration caused by pts jitter.
        if ((durationUs = audioTrack->getBufferedDurationUs(&err)) < kMinDurationUs && err == OK) {
            buffer_flag = false;
        }
        if (!buffer_flag && estimate) {
            if (audioTrack->getBufferedDataSize() >= mBuffTimeSec * estimate) {
                buffer_flag = true;
            }
        }
        if (!buffer_flag) {
            return false;
        }
    }
    if (videoTrack != NULL) {
        estimate = videoTrack->getEstimatedBytesPerSec();
        // just estimate, prevent wrong duration caused by pts jitter.
        if ((durationUs = videoTrack->getBufferedDurationUs(&err)) < kMinDurationUs && err == OK) {
            buffer_flag = false;
        }
        if (!buffer_flag && estimate) {
            if (videoTrack->getBufferedDataSize() >= mBuffTimeSec * estimate) {
                buffer_flag = true;
            }
        }
        if (!buffer_flag) {
            return false;
        }
    }

    ALOGI("audio and video track have enough data!\n");
    return true;
}

void LiveSession::setEOSTimeout(bool audio, int64_t timeout) {
    if (audio) {
        mEOSTimeoutAudio = timeout;
    } else {
        mEOSTimeoutVideo = timeout;
    }
}

status_t LiveSession::hasBufferAvailable(bool audio, bool * needBuffering) {
    StreamType stream = audio ? STREAMTYPE_AUDIO : STREAMTYPE_VIDEO;
    sp<AnotherPacketSource> t_source = mPacketSources.valueFor(stream);
    if (t_source == NULL) {
        return -EWOULDBLOCK;
    }
    status_t finalResult;
    if (!t_source->hasBufferAvailable(&finalResult)) {
        if (finalResult == OK) {
            int64_t mediaDurationUs = 0;
            getDuration(&mediaDurationUs);
            StreamType otherStream = !audio ? STREAMTYPE_AUDIO : STREAMTYPE_VIDEO;
            sp<AnotherPacketSource> otherSource = mPacketSources.valueFor(otherStream);
            status_t otherFinalResult;

            // If other source already signaled EOS, this source should also signal EOS
            if (otherSource != NULL &&
                    !otherSource->hasBufferAvailable(&otherFinalResult) &&
                    otherFinalResult == ERROR_END_OF_STREAM) {
                t_source->signalEOS(ERROR_END_OF_STREAM);
                return ERROR_END_OF_STREAM;
            }

            // If this source has detected near end, give it some time to retrieve more
            // data before signaling EOS
            if (t_source->isFinished(mediaDurationUs)) {
                int64_t eosTimeout = audio ? mEOSTimeoutAudio : mEOSTimeoutVideo;
                if (eosTimeout == 0) {
                    setEOSTimeout(audio, ALooper::GetNowUs());
                } else if ((ALooper::GetNowUs() - eosTimeout) > kNearEOSTimeoutUs) {
                    setEOSTimeout(audio, 0);
                    t_source->signalEOS(ERROR_END_OF_STREAM);
                    return ERROR_END_OF_STREAM;
                }
                return -EWOULDBLOCK;
            }

            if (!(otherSource != NULL && otherSource->isFinished(mediaDurationUs))) {
                // We should not enter buffering mode
                // if any of the sources already have detected EOS.
                *needBuffering = true;
            }

            return -EWOULDBLOCK;
        }
        return finalResult;
    }
    setEOSTimeout(audio, 0);
    return OK;
}

status_t LiveSession::dequeueAccessUnit(
        StreamType stream, sp<ABuffer> *accessUnit) {
    if (!(mStreamMask & stream)) {
        // return -EWOULDBLOCK to avoid halting the decoder
        // when switching between audio/video and audio only.
        return -EWOULDBLOCK;
    }

    status_t finalResult;
    sp<AnotherPacketSource> discontinuityQueue  = mDiscontinuities.valueFor(stream);
    if (discontinuityQueue->hasBufferAvailable(&finalResult)) {
        discontinuityQueue->dequeueAccessUnit(accessUnit);
        // seeking, track switching
        sp<AMessage> extra;
        int64_t timeUs;
        if ((*accessUnit)->meta()->findMessage("extra", &extra)
                && extra != NULL
                && extra->findInt64("timeUs", &timeUs)) {
            // seeking only
            mLastSeekTimeUs = getSegmentStartTimeUsAfterSeek(timeUs);
            mAudioDiscontinuityOffsetTimesUs.clear();
            mVideoDiscontinuityOffsetTimesUs.clear();
            mAudioDiscontinuityAbsStartTimesUs.clear();
            mVideoDiscontinuityAbsStartTimesUs.clear();
        }
        return INFO_DISCONTINUITY;
    }

    sp<AnotherPacketSource> packetSource = mPacketSources.valueFor(stream);

    ssize_t idx = typeToIndex(stream);
    if (!packetSource->hasBufferAvailable(&finalResult)) {
        if (finalResult == OK) {
            mBuffering[idx] = true;
            return -EAGAIN;
        } else {
            return finalResult;
        }
    }

    int32_t targetDuration = 0;
    sp<AMessage> meta = packetSource->getLatestEnqueuedMeta();
    if (meta != NULL) {
        meta->findInt32("targetDuration", &targetDuration);
    }

    int64_t targetDurationUs = targetDuration * 1000000ll;
    if (targetDurationUs == 0 ||
            targetDurationUs > PlaylistFetcher::kMinBufferedDurationUs) {
        // Fetchers limit buffering to
        // min(3 * targetDuration, kMinBufferedDurationUs)
        targetDurationUs = PlaylistFetcher::kMinBufferedDurationUs;
    }

    if (mBuffering[idx]) {
        if (mSwitchInProgress
                || packetSource->isFinished(0)
                || packetSource->getEstimatedDurationUs() > targetDurationUs) {
            mBuffering[idx] = false;
        }
    }

    if (mBuffering[idx]) {
        return -EAGAIN;
    }

    // wait for counterpart
    sp<AnotherPacketSource> otherSource;
    uint32_t mask = mNewStreamMask & mStreamMask;
    uint32_t fetchersMask  = 0;
    for (size_t i = 0; i < mFetcherInfos.size(); ++i) {
        uint32_t fetcherMask = mFetcherInfos.valueAt(i).mFetcher->getStreamTypeMask();
        fetchersMask |= fetcherMask;
    }
    mask &= fetchersMask;
    if (stream == STREAMTYPE_AUDIO && (mask & STREAMTYPE_VIDEO)) {
        otherSource = mPacketSources.valueFor(STREAMTYPE_VIDEO);
    } else if (stream == STREAMTYPE_VIDEO && (mask & STREAMTYPE_AUDIO)) {
        otherSource = mPacketSources.valueFor(STREAMTYPE_AUDIO);
    }
    if (otherSource != NULL && !otherSource->hasBufferAvailable(&finalResult)) {
        return finalResult == OK ? -EAGAIN : finalResult;
    }

    status_t err;
    if (stream == STREAMTYPE_VIDEO) {
        // need to send HEVC CodecSpecificData, lost when seek instantly after start.
        if (mSeeked == true && mCodecSpecificData != NULL && !mCodecSpecificDataSend) {
            int cast_size = HEVCCastSpecificData(mCodecSpecificData, mCodecSpecificDataSize);
            if (cast_size > 0) {
                mCodecSpecificDataSize = cast_size;
            }
            sp<ABuffer> tmpAU = new ABuffer(mCodecSpecificDataSize);
            memcpy(tmpAU->data(), mCodecSpecificData, mCodecSpecificDataSize);
            (*accessUnit) = tmpAU;
            (*accessUnit)->meta()->setInt64("timeUs", 0ll);
            mCodecSpecificDataSend = true;
            mSeeked = false;
            err = OK;
        } else {
            err = packetSource->dequeueAccessUnit(accessUnit);
        }
    } else {
        err = packetSource->dequeueAccessUnit(accessUnit);
    }

    size_t streamIdx;
    const char *streamStr;
    switch (stream) {
        case STREAMTYPE_AUDIO:
            streamIdx = kAudioIndex;
            streamStr = "audio";
            break;
        case STREAMTYPE_VIDEO:
            streamIdx = kVideoIndex;
            streamStr = "video";
            break;
        case STREAMTYPE_SUBTITLES:
            streamIdx = kSubtitleIndex;
            streamStr = "subs";
            break;
        default:
            TRESPASS();
    }

    StreamItem& strm = mStreams[streamIdx];
    if (err == INFO_DISCONTINUITY) {
        // adaptive streaming, discontinuities in the playlist
        int32_t type;
        CHECK((*accessUnit)->meta()->findInt32("discontinuity", &type));

        sp<AMessage> extra;
        if (!(*accessUnit)->meta()->findMessage("extra", &extra)) {
            extra.clear();
        }

        ALOGI("[%s] read discontinuity of type %d, extra = %s",
              streamStr,
              type,
              extra == NULL ? "NULL" : extra->debugString().c_str());

        int32_t swap;
        if ((*accessUnit)->meta()->findInt32("swapPacketSource", &swap) && swap) {
            int32_t switchGeneration;
            CHECK((*accessUnit)->meta()->findInt32("switchGeneration", &switchGeneration));
            {
                Mutex::Autolock lock(mSwapMutex);
                if (switchGeneration == mSwitchGeneration) {
                    swapPacketSource(stream);
                    sp<AMessage> msg = new AMessage(kWhatSwapped, id());
                    msg->setInt32("stream", stream);
                    msg->setInt32("switchGeneration", switchGeneration);
                    msg->post();
                }
            }
        } else {
            size_t seq = strm.mCurDiscontinuitySeq;
            int64_t offsetTimeUs;
            if (stream == STREAMTYPE_AUDIO && mAudioDiscontinuityOffsetTimesUs.indexOfKey(seq) >= 0) {
                offsetTimeUs = mAudioDiscontinuityOffsetTimesUs.valueFor(seq);
            } else if (stream == STREAMTYPE_VIDEO && mVideoDiscontinuityOffsetTimesUs.indexOfKey(seq) >= 0) {
                offsetTimeUs = mVideoDiscontinuityOffsetTimesUs.valueFor(seq);
            } else {
                offsetTimeUs = 0;
            }

            seq += 1;
            if (stream == STREAMTYPE_AUDIO && mAudioDiscontinuityAbsStartTimesUs.indexOfKey(strm.mCurDiscontinuitySeq) >= 0) {
                int64_t firstTimeUs;
                firstTimeUs = mAudioDiscontinuityAbsStartTimesUs.valueFor(strm.mCurDiscontinuitySeq);
                offsetTimeUs += strm.mLastDequeuedTimeUs - firstTimeUs;
                offsetTimeUs += strm.mLastSampleDurationUs;
            } else if (stream == STREAMTYPE_VIDEO && mVideoDiscontinuityAbsStartTimesUs.indexOfKey(strm.mCurDiscontinuitySeq) >= 0) {
                int64_t firstTimeUs;
                firstTimeUs = mVideoDiscontinuityAbsStartTimesUs.valueFor(strm.mCurDiscontinuitySeq);
                offsetTimeUs += strm.mLastDequeuedTimeUs - firstTimeUs;
                offsetTimeUs += strm.mLastSampleDurationUs;
            } else {
                offsetTimeUs += strm.mLastSampleDurationUs;
            }
            if (stream == STREAMTYPE_AUDIO) {
                mAudioDiscontinuityOffsetTimesUs.add(seq, offsetTimeUs);
            } else if (stream == STREAMTYPE_VIDEO) {
                mVideoDiscontinuityOffsetTimesUs.add(seq, offsetTimeUs);
            }
        }
    } else if (err == OK) {

        if (stream == STREAMTYPE_AUDIO || stream == STREAMTYPE_VIDEO) {
            int64_t timeUs, origin_timeUs;
            int32_t discontinuitySeq = 0;
            CHECK((*accessUnit)->meta()->findInt64("timeUs",  &origin_timeUs));
            (*accessUnit)->meta()->findInt32("discontinuitySeq", &discontinuitySeq);
            strm.mCurDiscontinuitySeq = discontinuitySeq;
            timeUs = origin_timeUs;

            int32_t discard = 0;
            int64_t firstTimeUs;
            if (stream == STREAMTYPE_AUDIO && mAudioDiscontinuityAbsStartTimesUs.indexOfKey(strm.mCurDiscontinuitySeq) >= 0) {
                int64_t durUs; // approximate sample duration
                if (timeUs > strm.mLastDequeuedTimeUs) {
                    durUs = timeUs - strm.mLastDequeuedTimeUs;
                } else {
                    durUs = strm.mLastDequeuedTimeUs - timeUs;
                }
                strm.mLastSampleDurationUs = durUs;
                firstTimeUs = mAudioDiscontinuityAbsStartTimesUs.valueFor(strm.mCurDiscontinuitySeq);
            } else if (stream == STREAMTYPE_VIDEO && mVideoDiscontinuityAbsStartTimesUs.indexOfKey(strm.mCurDiscontinuitySeq) >= 0) {
                int64_t durUs; // approximate sample duration
                if (timeUs > strm.mLastDequeuedTimeUs) {
                    durUs = timeUs - strm.mLastDequeuedTimeUs;
                } else {
                    durUs = strm.mLastDequeuedTimeUs - timeUs;
                }
                strm.mLastSampleDurationUs = durUs;
                firstTimeUs = mVideoDiscontinuityAbsStartTimesUs.valueFor(strm.mCurDiscontinuitySeq);
            } else if ((*accessUnit)->meta()->findInt32("discard", &discard) && discard) {
                firstTimeUs = timeUs;
            } else {
                if (stream == STREAMTYPE_AUDIO) {
                    mAudioDiscontinuityAbsStartTimesUs.add(strm.mCurDiscontinuitySeq, timeUs);
                } else {
                    mVideoDiscontinuityAbsStartTimesUs.add(strm.mCurDiscontinuitySeq, timeUs);
                }
                firstTimeUs = timeUs;
            }

            strm.mLastDequeuedTimeUs = timeUs;
            if (timeUs >= firstTimeUs) {
                timeUs -= firstTimeUs;
            }
            timeUs += mLastSeekTimeUs;
            int64_t offset_timeUs = 0;
            if (stream == STREAMTYPE_AUDIO && mAudioDiscontinuityOffsetTimesUs.indexOfKey(discontinuitySeq) >= 0) {
                offset_timeUs = mAudioDiscontinuityOffsetTimesUs.valueFor(discontinuitySeq);
                timeUs += offset_timeUs;
            } else if (stream == STREAMTYPE_VIDEO && mVideoDiscontinuityOffsetTimesUs.indexOfKey(discontinuitySeq) >= 0) {
                offset_timeUs = mVideoDiscontinuityOffsetTimesUs.valueFor(discontinuitySeq);
                timeUs += offset_timeUs;
            }

            if (mDebug) {
                ALOGI("[%s] read buffer at time (%lld)us, origin time (%lld)us, first time (%lld)us, seek time (%lld)us, offset time (%lld)us", streamStr, timeUs, origin_timeUs, firstTimeUs, mLastSeekTimeUs, offset_timeUs);
            } else {
                ALOGV("[%s] read buffer at time %" PRId64 " us", streamStr, timeUs);
            }

            (*accessUnit)->meta()->setInt64("timeUs",  timeUs);
            mLastDequeuedTimeUs = timeUs;
            mRealTimeBaseUs = ALooper::GetNowUs() - timeUs;
        } else if (stream == STREAMTYPE_SUBTITLES) {
            int32_t subtitleGeneration;
            if ((*accessUnit)->meta()->findInt32("subtitleGeneration", &subtitleGeneration)
                    && subtitleGeneration != mSubtitleGeneration) {
               return -EAGAIN;
            };
            (*accessUnit)->meta()->setInt32(
                    "trackIndex", mPlaylist->getSelectedIndex());
            (*accessUnit)->meta()->setInt64("baseUs", mRealTimeBaseUs);
        }
    } else {
        ALOGI("[%s] encountered error %d", streamStr, err);
    }

    return err;
}

status_t LiveSession::getStreamFormat(StreamType stream, sp<AMessage> *format) {
    // No swapPacketSource race condition; called from the same thread as dequeueAccessUnit.
    if (!(mStreamMask & stream)) {
        return UNKNOWN_ERROR;
    }

    sp<AnotherPacketSource> packetSource = mPacketSources.valueFor(stream);

    sp<MetaData> meta = packetSource->getFormat();

    if (meta == NULL) {
        return -EAGAIN;
    }

    return convertMetaDataToMessage(meta, format);
}

void LiveSession::connectAsync(
        const char *url, const KeyedVector<String8, String8> *headers) {
    sp<AMessage> msg = new AMessage(kWhatConnect, id());
    msg->setString("url", url);

    if (headers != NULL) {
        msg->setPointer(
                "headers",
                new KeyedVector<String8, String8>(*headers));
    }

    msg->post();
}

status_t LiveSession::disconnect() {
    ALOGI("[%s:%d] start !", __FUNCTION__, __LINE__);
    {
        Mutex::Autolock autoLock(mWaitLock);
        mWaitCondition.broadcast();
    }

    mNeedExit = true;
    sp<AMessage> msg = new AMessage(kWhatDisconnect, id());

    sp<AMessage> response;
    status_t err = msg->postAndAwaitResponse(&response);

    ALOGI("[%s:%d] end !", __FUNCTION__, __LINE__);
    return err;
}

status_t LiveSession::seekTo(int64_t timeUs) {

    {
        Mutex::Autolock autoLock(mWaitLock);
        mWaitCondition.broadcast();
    }

    mSeeked = true;
    sp<AMessage> msg = new AMessage(kWhatSeek, id());
    msg->setInt64("timeUs", timeUs);

    sp<AMessage> response;
    status_t err = msg->postAndAwaitResponse(&response);

    return err;
}

void LiveSession::onMessageReceived(const sp<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatConnect:
        {
            onConnect(msg);
            break;
        }

        case kWhatDisconnect:
        {
            CHECK(msg->senderAwaitsResponse(&mDisconnectReplyID));

            if (mReconfigurationInProgress) {
                break;
            }

            finishDisconnect();
            break;
        }

        case kWhatSeek:
        {
            uint32_t seekReplyID;
            CHECK(msg->senderAwaitsResponse(&seekReplyID));
            mSeekReplyID = seekReplyID;
            mSeekReply = new AMessage;

            status_t err = onSeek(msg);

            if (err != OK) {
                msg->post(50000);
            }
            break;
        }

        case kWhatFetcherNotify:
        {
            int32_t what;
            CHECK(msg->findInt32("what", &what));
            ALOGI("fetcher notify : %d\n", what);

            switch (what) {
                case PlaylistFetcher::kWhatCodecSpecificData:
                {
                    if (mCodecSpecificData == NULL) {
                        sp<ABuffer> buffer;
                        msg->findBuffer("buffer", &buffer);
                        mCodecSpecificData = (uint8_t *)malloc(buffer->size());
                        mCodecSpecificDataSize = buffer->size();
                        memcpy(mCodecSpecificData, buffer->data(), buffer->size());
                        ALOGI("HEVC set CodecSpecificData, size : %d", mCodecSpecificDataSize);
                    }
                    break;
                }
                case PlaylistFetcher::kWhatStarted:
                    break;
                case PlaylistFetcher::kWhatPaused:
                case PlaylistFetcher::kWhatStopped:
                {
                    if (what == PlaylistFetcher::kWhatStopped) {
                        AString uri;
                        CHECK(msg->findString("uri", &uri));
                        mFetcherInfos.removeItem(uri);
                        void * ptr;
                        CHECK(msg->findPointer("looper", &ptr));
                        sp<ALooper> looper = static_cast<ALooper *>(ptr);
                        looper->stop();
                        for (size_t i = 0; i < mFetcherLooper.size(); i++) {
                            if ((mFetcherLooper.itemAt(i)).get() == ptr) {
                                ALOGI("fetcher stopped, release looper now!\n");
                                mFetcherLooper.removeAt(i);
                                break;
                            }
                        }

                        if (mSwitchInProgress) {
                            tryToFinishBandwidthSwitch();
                        }
                    }

                    if (mContinuation != NULL) {
                        CHECK_GT(mContinuationCounter, 0);
                        if (--mContinuationCounter == 0) {
                            mContinuation->post();

                            if (mSeekReplyID != 0) {
                                CHECK(mSeekReply != NULL);
                                mSeekReply->setInt32("err", OK);
                                mSeekReply->postReply(mSeekReplyID);
                                mSeekReplyID = 0;
                                mSeekReply.clear();
                            }
                        }
                    }
                    break;
                }

                case PlaylistFetcher::kWhatDurationUpdate:
                {
                    AString uri;
                    CHECK(msg->findString("uri", &uri));

                    int64_t durationUs;
                    CHECK(msg->findInt64("durationUs", &durationUs));

                    if (mFetcherInfos.indexOfKey(uri) >= 0) {
                        FetcherInfo *info = &mFetcherInfos.editValueFor(uri);
                        info->mDurationUs = durationUs;
                    }
                    break;
                }

                case PlaylistFetcher::kWhatError:
                {
                    status_t err;
                    CHECK(msg->findInt32("err", &err));

                    ALOGE("XXX Received error %d from PlaylistFetcher.", err);

                    if (mInterruptCallback(mParentThreadId)) {
                        ALOGI("Maybe reset or seek, ignore this error : %d", err);
                        break;
                    }

                    // handle EOS on subtitle tracks independently
                    AString uri;
                    if (err == ERROR_END_OF_STREAM && msg->findString("uri", &uri)) {
                        ssize_t i = mFetcherInfos.indexOfKey(uri);
                        if (i >= 0) {
                            const sp<PlaylistFetcher> &fetcher = mFetcherInfos.valueAt(i).mFetcher;
                            if (fetcher != NULL) {
                                uint32_t type = fetcher->getStreamTypeMask();
                                if (type == STREAMTYPE_SUBTITLES) {
                                    mPacketSources.valueFor(
                                            STREAMTYPE_SUBTITLES)->signalEOS(err);;
                                    break;
                                }
                            }
                        }
                    }

                    if (mInPreparationPhase && err != ERROR_UNSUPPORTED) { // ingore err when unsupport.
                        postPrepared(err);
                    }

                    cancelBandwidthSwitch();

                    mPacketSources.valueFor(STREAMTYPE_AUDIO)->signalEOS(err);

                    mPacketSources.valueFor(STREAMTYPE_VIDEO)->signalEOS(err);

                    mPacketSources.valueFor(
                            STREAMTYPE_SUBTITLES)->signalEOS(err);

                    sp<AMessage> notify = mNotify->dup();
                    notify->setInt32("what", kWhatError);
                    notify->setInt32("err", err);
                    notify->post();
                    break;
                }

                case PlaylistFetcher::kWhatTemporarilyDoneFetching:
                {
                    AString uri;
                    CHECK(msg->findString("uri", &uri));

                    if (mFetcherInfos.indexOfKey(uri) < 0) {
                        ALOGE("couldn't find uri");
                        break;
                    }
                    FetcherInfo *info = &mFetcherInfos.editValueFor(uri);
                    info->mIsPrepared = true;

                    if (mInPreparationPhase) {
                        bool allFetchersPrepared = true;
                        for (size_t i = 0; i < mFetcherInfos.size(); ++i) {
                            if (!mFetcherInfos.valueAt(i).mIsPrepared) {
                                allFetchersPrepared = false;
                                break;
                            }
                        }

                        if (allFetchersPrepared) {
                            postPrepared(OK);
                        }
                    }
                    break;
                }

                case PlaylistFetcher::kWhatStartedAt:
                {
                    int32_t switchGeneration;
                    CHECK(msg->findInt32("switchGeneration", &switchGeneration));

                    if (switchGeneration != mSwitchGeneration) {
                        break;
                    }

                    // Resume fetcher for the original variant; the resumed fetcher should
                    // continue until the timestamps found in msg, which is stored by the
                    // new fetcher to indicate where the new variant has started buffering.
                    for (size_t i = 0; i < mFetcherInfos.size(); i++) {
                        const FetcherInfo info = mFetcherInfos.valueAt(i);
                        if (info.mToBeRemoved) {
                            info.mFetcher->resumeUntilAsync(msg);
                        }
                    }
                    break;
                }

                default:
                    TRESPASS();
            }

            break;
        }

#if 0
        case kWhatCheckBandwidth:
        {
            int32_t generation;
            CHECK(msg->findInt32("generation", &generation));

            if (generation != mCheckBandwidthGeneration) {
                break;
            }

            onCheckBandwidth(msg);
            break;
        }
#endif
        case kWhatChangeConfiguration:
        {
            onChangeConfiguration(msg);
            break;
        }

        case kWhatChangeConfiguration2:
        {
            onChangeConfiguration2(msg);
            break;
        }

        case kWhatChangeConfiguration3:
        {
            onChangeConfiguration3(msg);
            break;
        }

        case kWhatFinishDisconnect2:
        {
            onFinishDisconnect2();
            break;
        }

        case kWhatSwapped:
        {
            onSwapped(msg);
            break;
        }

        case kWhatCheckSwitchDown:
        {
            onCheckSwitchDown();
            break;
        }

        case kWhatSwitchDown:
        {
            onSwitchDown();
            break;
        }

        default:
            TRESPASS();
            break;
    }
}

// static
int LiveSession::SortByBandwidth(const BandwidthItem *a, const BandwidthItem *b) {
    if (a->mBandwidth < b->mBandwidth) {
        return -1;
    } else if (a->mBandwidth == b->mBandwidth) {
        return 0;
    }

    return 1;
}

// static
LiveSession::StreamType LiveSession::indexToType(int idx) {
    CHECK(idx >= 0 && idx < kMaxStreams);
    return (StreamType)(1 << idx);
}

// static
ssize_t LiveSession::typeToIndex(int32_t type) {
    switch (type) {
        case STREAMTYPE_AUDIO:
            return 0;
        case STREAMTYPE_VIDEO:
            return 1;
        case STREAMTYPE_SUBTITLES:
            return 2;
        default:
            return -1;
    };
    return -1;
}

void LiveSession::onConnect(const sp<AMessage> &msg) {
    AString url;
    CHECK(msg->findString("url", &url));

    KeyedVector<String8, String8> *headers = NULL;
    if (!msg->findPointer("headers", (void **)&headers)) {
        mExtraHeaders.clear();
    } else {
        mExtraHeaders = *headers;

        delete headers;
        headers = NULL;
    }

    // TODO currently we don't know if we are coming here from incognito mode
    ALOGI("onConnect %s", uriDebugString(url).c_str());

    mMasterURL = url;

    bool dummy;
    status_t dummy_err;
    CFContext * cfc_handle = NULL;
    mPlaylist = fetchPlaylist(url.c_str(), NULL /* curPlaylistHash */, &dummy, dummy_err, &cfc_handle);
    if (cfc_handle) {
        curl_fetch_close(cfc_handle);
    }

    if (mPlaylist == NULL) {
        ALOGE("unable to fetch master playlist %s.", uriDebugString(url).c_str());

        postPrepared(ERROR_IO);
        return;
    }

    // We trust the content provider to make a reasonable choice of preferred
    // initial bandwidth by listing it first in the variant playlist.
    // At startup we really don't have a good estimate on the available
    // network bandwidth since we haven't tranferred any data yet. Once
    // we have we can make a better informed choice.
    size_t initialBandwidth = 0;
    size_t initialBandwidthIndex = 0;

    if (mPlaylist->isVariantPlaylist()) {
        for (size_t i = 0; i < mPlaylist->size(); ++i) {
            BandwidthItem item;

            item.mPlaylistIndex = i;

            sp<AMessage> meta;
            AString uri;
            mPlaylist->itemAt(i, &uri, &meta);

            unsigned long bandwidth;
            CHECK(meta->findInt32("bandwidth", (int32_t *)&item.mBandwidth));

            if (initialBandwidth == 0) {
                initialBandwidth = item.mBandwidth;
            }

            mBandwidthItems.push(item);
        }

        CHECK_GT(mBandwidthItems.size(), 0u);

        mBandwidthItems.sort(SortByBandwidth);

        for (size_t i = 0; i < mBandwidthItems.size(); ++i) {
            if (mBandwidthItems.itemAt(i).mBandwidth == initialBandwidth) {
                initialBandwidthIndex = i;
                break;
            }
        }
    } else {
        // dummy item.
        BandwidthItem item;
        item.mPlaylistIndex = 0;
        item.mBandwidth = 0;
        mBandwidthItems.push(item);
    }

    mPlaylist->pickRandomMediaItems();
    changeConfiguration(
            0ll /* timeUs */, initialBandwidthIndex, false /* pickTrack */);
}

void LiveSession::finishDisconnect() {
    ALOGI("[%s:%d] start !", __FUNCTION__, __LINE__);
    // No reconfiguration is currently pending, make sure none will trigger
    // during disconnection either.
    cancelCheckBandwidthEvent();

    // Protect mPacketSources from a swapPacketSource race condition through disconnect.
    // (finishDisconnect, onFinishDisconnect2)
    cancelBandwidthSwitch();

    // cancel switch down monitor
    mSwitchDownMonitor.clear();

    for (size_t i = 0; i < mFetcherInfos.size(); ++i) {
        mFetcherInfos.valueAt(i).mFetcher->stopAsync();
    }

    sp<AMessage> msg = new AMessage(kWhatFinishDisconnect2, id());

    mContinuationCounter = mFetcherInfos.size();
    mContinuation = msg;

    if (mContinuationCounter == 0) {
        msg->post();
    }
    ALOGI("[%s:%d] end !", __FUNCTION__, __LINE__);
}

void LiveSession::onFinishDisconnect2() {
    ALOGI("[%s:%d] start !", __FUNCTION__, __LINE__);
    mContinuation.clear();

    mPacketSources.valueFor(STREAMTYPE_AUDIO)->signalEOS(ERROR_END_OF_STREAM);
    mPacketSources.valueFor(STREAMTYPE_VIDEO)->signalEOS(ERROR_END_OF_STREAM);

    mPacketSources.valueFor(
            STREAMTYPE_SUBTITLES)->signalEOS(ERROR_END_OF_STREAM);

    sp<AMessage> response = new AMessage;
    response->setInt32("err", OK);

    response->postReply(mDisconnectReplyID);
    mDisconnectReplyID = 0;
    ALOGI("[%s:%d] end !", __FUNCTION__, __LINE__);
}

sp<PlaylistFetcher> LiveSession::addFetcher(const char *uri) {
    ssize_t index = mFetcherInfos.indexOfKey(uri);

    if (index >= 0) {
        return NULL;
    }

    // run fetchers in independent threads.
    // prevent blocking in download.
    sp<ALooper> fetcherLooper = new ALooper;
    fetcherLooper->setName("playlist fetcher");
    fetcherLooper->start();

    sp<AMessage> notify = new AMessage(kWhatFetcherNotify, id());
    notify->setString("uri", uri);
    notify->setInt32("switchGeneration", mSwitchGeneration);

    FetcherInfo info;
    info.mFetcher = new PlaylistFetcher(notify, this, uri, mSubtitleGeneration);
    info.mDurationUs = -1ll;
    info.mStatus = STATUS_ACTIVE;
    info.mIsPrepared = false;
    info.mToBeRemoved = false;
    fetcherLooper->registerHandler(info.mFetcher);

    mFetcherInfos.add(uri, info);
    mFetcherLooper.push(fetcherLooper);

    return info.mFetcher;
}

ssize_t LiveSession::readFromSource(CFContext * cfc, uint8_t * data, size_t size) {
    int32_t ret = -1;
    bool wait_flag = false;
    uint32_t start_waittime_s = 0, waitSec = 0;
    int64_t read_seek_size = 0, read_seek_left_size = 0;
    char dummy[4096];
    do {
        if (mInterruptCallback(mParentThreadId)) {
            ALOGE("[%s:%d] interrupted !", __FUNCTION__, __LINE__);
            return UNKNOWN_ERROR;
        }
        if (read_seek_size && read_seek_left_size) {
            int32_t tmp_size = read_seek_left_size > sizeof(dummy) ? sizeof(dummy) : read_seek_left_size;
            ret = curl_fetch_read(cfc, dummy, tmp_size);
            if (ret > 0) {
                read_seek_left_size -= ret;
                if (!read_seek_left_size) {
                    read_seek_size = 0;
                    ALOGI("read seek complete !");
                }
                continue;
            }
        } else {
            ret = curl_fetch_read(cfc, (char *)data, size);
        }
        if (ret == C_ERROR_EAGAIN) {
            threadWaitTimeNs(10000000);
            continue;
        }
        if (ret >= 0 || ret == C_ERROR_UNKNOW) {
            break;
        }
        if (ret < C_ERROR_EAGAIN) {
            if (!retryCase(ret) || retryCase(ret) == 1 || retryCase(ret) == 2) {
                if (!wait_flag) {
                    start_waittime_s = ALooper::GetNowUs() / 1000000;
                    wait_flag = true;
                    if (!retryCase(ret)) {
                        waitSec = mFailureWaitSec;
                    } else {
                        waitSec = mAbnormalWaitSec;
                    }
                }
                ALOGI("source read met error! ret : %d", ret);
                if (ALooper::GetNowUs() / 1000000 - start_waittime_s <= waitSec) {
                    if ((cfc->filesize <= 0 || retryCase(ret) == 2) && !read_seek_size) { // try to do read seek in chunked mode.
                        read_seek_size = cfc->cwd->size;
                        ALOGI("need to do read seek : %lld", read_seek_size);
                    }
                    if (read_seek_size) {
                        read_seek_left_size = read_seek_size;
                    }
                    // too big! no need to do read seek.
                    if (read_seek_size > 100 * 1024 * 1024) {
                        ret = -ENETRESET;
                        break;
                    }
                    curl_fetch_seek(cfc, cfc->cwd->size, SEEK_SET);
                    threadWaitTimeNs(100000000);
                } else {
                    ret = -ENETRESET;
                    break;
                }
            } else {
                ret = -ENETRESET;
                break;
            }
        }
    } while (!mNeedExit);

    return ret;
}

int32_t LiveSession::retryCase(int32_t arg) {
    int ret = -1;
    switch (arg) {
        case CURLERROR(56 + C_ERROR_PERFORM_BASE_ERROR):  // recv failure
        case CURLERROR(18 + C_ERROR_PERFORM_BASE_ERROR):  // partial file
        case CURLERROR(28 + C_ERROR_PERFORM_BASE_ERROR):  // operation timeout
        case CURLERROR(C_ERROR_PERFORM_SELECT_ERROR):
            ret = 0;
            break;
        case CURLERROR(7 + C_ERROR_PERFORM_BASE_ERROR): // couldn't connect
        case CURLERROR(6 + C_ERROR_PERFORM_BASE_ERROR): // couldn't resolve host
            ret = 1;
            break;
        case CURLERROR(33 + C_ERROR_PERFORM_BASE_ERROR): // CURLE_RANGE_ERROR
            ret = 2;
            break;
        default:
            break;
    }
    return ret;
}

void LiveSession::threadWaitTimeNs(int64_t timeNs) {
    Mutex::Autolock autoLock(mWaitLock);
    mWaitCondition.waitRelative(mWaitLock, timeNs);
}

/*
 * Illustration of parameters:
 *
 * 0      `range_offset`
 * +------------+-------------------------------------------------------+--+--+
 * |            |                                 | next block to fetch |  |  |
 * |            | `source` handle => `out` buffer |                     |  |  |
 * | `url` file |<--------- buffer size --------->|<--- `block_size` -->|  |  |
 * |            |<----------- `range_length` / buffer capacity ----------->|  |
 * |<------------------------------ file_size ------------------------------->|
 *
 * Special parameter values:
 * - range_length == -1 means entire file
 * - block_size == 0 means entire range
 *
 */
ssize_t LiveSession::fetchFile(
        const char *url, sp<ABuffer> *out,
        int64_t range_offset, int64_t range_length,
        uint32_t block_size, /* download block size */
        CFContext ** cfc, /* to return and reuse source */
        String8 *actualUrl, bool isPlaylist) {

    if (mNeedExit || mInterruptCallback(mParentThreadId)) {
        return 0;
    }

    off64_t size;

    if (*cfc == NULL) {
        String8 headers;
        if (range_offset > 0 || range_length >= 0) {
            headers.append(StringPrintf("Range: bytes=%lld-%s\r\n", range_offset, range_length < 0 ? "" : StringPrintf("%lld", range_offset + range_length - 1).c_str()).c_str());
        }
        ssize_t i = mExtraHeaders.indexOfKey(String8("User-Agent"));
        if (i >= 0) {
            headers.append(StringPrintf("User-Agent: %s\r\n", mExtraHeaders.valueAt(i).string()).c_str());
        } else {
            headers.append(StringPrintf("User-Agent: %s\r\n", kHTTPUserAgentDefault.string()).c_str());
        }
        CFContext * temp_cfc = curl_fetch_init(url, headers.string(), 0);
        if (!temp_cfc) {
            ALOGE("curl fetch init failed!");
            return UNKNOWN_ERROR;
        }
        curl_fetch_register_interrupt_pid(temp_cfc, mInterruptCallback);
        curl_fetch_set_parent_pid(temp_cfc, mParentThreadId);
        if (curl_fetch_open(temp_cfc)) {
            ALOGE("curl fetch open failed! http code : %d", temp_cfc->http_code);
            curl_fetch_close(temp_cfc);
            temp_cfc = NULL;
            return ERROR_CANNOT_CONNECT;
        }
        *cfc = temp_cfc;
    }

    size = (*cfc)->filesize;
    if (isPlaylist && size > 1 * 1024 * 1024) { // assume not m3u8.
        sp<AMessage> notify = mNotify->dup();
        notify->setInt32("what", kWhatSourceReady);
        notify->setInt32("err", 1);
        notify->post();
        return ERROR_UNSUPPORTED;
    }
    if (size <= 0) {
        size = 1 * 1024 * 1024;
    }

    sp<ABuffer> buffer = *out != NULL ? *out : new ABuffer(size);
    if (*out == NULL) {
        buffer->setRange(0, 0);
    }

    ssize_t bytesRead = 0;
    // adjust range_length if only reading partial block
    if (block_size > 0 && (range_length == -1 || (int64_t)(buffer->size() + block_size) < range_length)) {
        range_length = buffer->size() + block_size;
    }

    size_t size_per_read = kSizePerRead;
    if (isPlaylist && mFirstSniff) {
        size_per_read = 100;
    }

    for (;;) {
        // no block when quit.
        if (mNeedExit || mInterruptCallback(mParentThreadId)) {
            break;
        }

        size_t bufferRemaining = buffer->capacity() - buffer->size();

        if (bufferRemaining == 0) {
            size_t bufferIncrement = buffer->size() / 2;
            if (bufferIncrement < 32768) {
                bufferIncrement = 32768;
            }
            bufferRemaining = bufferIncrement;

            ALOGV("increasing download buffer to %zu bytes",
                 buffer->size() + bufferRemaining);

            sp<ABuffer> copy = new ABuffer(buffer->size() + bufferRemaining);
            memcpy(copy->data(), buffer->data(), buffer->size());
            copy->setRange(0, buffer->size());

            buffer = copy;
        }

        size_t maxBytesToRead = (bufferRemaining < size_per_read) ? bufferRemaining : size_per_read;
        if (range_length >= 0) {
            int64_t bytesLeftInRange = range_length - buffer->size();
            if (bytesLeftInRange < (int64_t)maxBytesToRead) {
                maxBytesToRead = bytesLeftInRange;

                if (bytesLeftInRange == 0) {
                    break;
                }
            }
        }

        ssize_t n = readFromSource(*cfc, buffer->data() + buffer->size(), maxBytesToRead);

        if (n < 0) {
            ALOGE("HTTP source read failed, err : %d !\n", n);
            return n;
        }

        if (n == 0) {
            break;
        }

        if (isPlaylist && mFirstSniff) {
            StreamSniffer sniff(NULL);
            status_t err = UNKNOWN_ERROR;
            if (n >= 8) {
                ABitReader br(buffer->data() + buffer->size(), n);
                err = sniff.tryHLSParser(&br);
            }
            if (err != OK) {
                ALOGI("[%s:%d] hls sniff failed !", __FUNCTION__, __LINE__);
                sp<AMessage> notify = mNotify->dup();
                notify->setInt32("what", kWhatSourceReady);
                notify->setInt32("err", 1);
                notify->post();
                return ERROR_UNSUPPORTED;
            }
            mFirstSniff = false;
            size_per_read = kSizePerRead;
        }

        buffer->setRange(0, buffer->size() + (size_t)n);
        bytesRead += n;
    }

    *out = buffer;
    if (actualUrl != NULL) {
        if ((*cfc)->relocation) {
            *actualUrl = (*cfc)->relocation;
            ALOGI("actual url : %s", (*cfc)->relocation);
        }
        if (actualUrl->isEmpty()) {
            *actualUrl = url;
        }
    }

    if (!bytesRead && !isPlaylist) {
        double tmp_info = 0.0;
        int32_t err_ret = curl_fetch_get_info(*cfc, C_INFO_SPEED_DOWNLOAD, 0, (void *)&tmp_info);
        if (!err_ret) {
            mEstimatedBWbps = (int32_t)(tmp_info * 8);
        } else {
            mEstimatedBWbps = 0;
        }
        ALOGI("download speed : %d bps", mEstimatedBWbps);
    }

    return bytesRead;
}

sp<M3UParser> LiveSession::fetchPlaylist(
        const char *url, uint8_t *curPlaylistHash, bool *unchanged, status_t &err_ret, CFContext ** cfc) {
    ALOGI("fetchPlaylist '%s'", url);

    *unchanged = false;

    sp<ABuffer> buffer;
    String8 actualUrl;

    ssize_t err = fetchFile(url, &buffer, 0, -1, 0, cfc, &actualUrl, true);

    if (err <= 0) {
        err_ret = err;
        ALOGE("failed to fetch playlist, err : %d\n", err);
        return NULL;
    }

    // MD5 functionality is not available on the simulator, treat all
    // playlists as changed.

#if defined(HAVE_ANDROID_OS)
    if (!mLastPlayListURL.empty() && strcmp(mLastPlayListURL.c_str(), url)) {
        goto PASS_THROUGH;
    }
    uint8_t hash[16];

    MD5_CTX m;
    MD5_Init(&m);
    MD5_Update(&m, buffer->data(), buffer->size());

    MD5_Final(hash, &m);

    if (curPlaylistHash != NULL && !memcmp(hash, curPlaylistHash, 16)) {
        // playlist unchanged
        *unchanged = true;

        return NULL;
    }

    if (curPlaylistHash != NULL) {
        memcpy(curPlaylistHash, hash, sizeof(hash));
    }
#endif

PASS_THROUGH:
    mLastPlayListURL.setTo(url);
    sp<M3UParser> playlist =
        new M3UParser(actualUrl.string(), buffer->data(), buffer->size());

    if (playlist->initCheck() != OK) {
        ALOGE("failed to parse .m3u8 playlist");

        return NULL;
    }

    return playlist;
}

static double uniformRand() {
    return (double)rand() / RAND_MAX;
}

size_t LiveSession::getBandwidthIndex() {
    if (mBandwidthItems.size() == 0) {
        return 0;
    }

#if 1
    char value[PROPERTY_VALUE_MAX];
    ssize_t index = -1;
    if (property_get("media.httplive.bw-index", value, NULL)) {
        char *end;
        index = strtol(value, &end, 10);
        CHECK(end > value && *end == '\0');

        if (index >= 0 && (size_t)index >= mBandwidthItems.size()) {
            index = mBandwidthItems.size() - 1;
        }
    }

    if (index < 0) {
        int32_t bandwidthBps = mEstimatedBWbps;
        ALOGI("bandwidth estimated at %.2f kbps", bandwidthBps / 1024.0f);

        char value[PROPERTY_VALUE_MAX];
        if (property_get("media.httplive.max-bw", value, NULL)) {
            char *end;
            long maxBw = strtoul(value, &end, 10);
            if (end > value && *end == '\0') {
                if (maxBw > 0 && bandwidthBps > maxBw) {
                    ALOGV("bandwidth capped to %ld bps", maxBw);
                    bandwidthBps = maxBw;
                }
            }
        }

        // Pick the highest bandwidth stream below or equal to estimated bandwidth.

        index = mBandwidthItems.size() - 1;
        while (index > 0) {
            // consider only 80% of the available bandwidth, but if we are switching up,
            // be even more conservative (70%) to avoid overestimating and immediately
            // switching back.
            size_t adjustedBandwidthBps = bandwidthBps;
            if (index > mCurBandwidthIndex) {
                adjustedBandwidthBps = adjustedBandwidthBps * 7 / 10;
            } else {
                adjustedBandwidthBps = adjustedBandwidthBps * 8 / 10;
            }
            if (mBandwidthItems.itemAt(index).mBandwidth <= adjustedBandwidthBps) {
                break;
            }
            --index;
        }
    }
#elif 0
    // Change bandwidth at random()
    size_t index = uniformRand() * mBandwidthItems.size();
#elif 0
    // There's a 50% chance to stay on the current bandwidth and
    // a 50% chance to switch to the next higher bandwidth (wrapping around
    // to lowest)
    const size_t kMinIndex = 0;

    static ssize_t mCurBandwidthIndex = -1;

    size_t index;
    if (mCurBandwidthIndex < 0) {
        index = kMinIndex;
    } else if (uniformRand() < 0.5) {
        index = (size_t)mCurBandwidthIndex;
    } else {
        index = mCurBandwidthIndex + 1;
        if (index == mBandwidthItems.size()) {
            index = kMinIndex;
        }
    }
    mCurBandwidthIndex = index;
#elif 0
    // Pick the highest bandwidth stream below or equal to 1.2 Mbit/sec

    size_t index = mBandwidthItems.size() - 1;
    while (index > 0 && mBandwidthItems.itemAt(index).mBandwidth > 1200000) {
        --index;
    }
#elif 1
    char value[PROPERTY_VALUE_MAX];
    size_t index;
    if (property_get("media.httplive.bw-index", value, NULL)) {
        char *end;
        index = strtoul(value, &end, 10);
        CHECK(end > value && *end == '\0');

        if (index >= mBandwidthItems.size()) {
            index = mBandwidthItems.size() - 1;
        }
    } else {
        index = 0;
    }
#else
    size_t index = mBandwidthItems.size() - 1;  // Highest bandwidth stream
#endif

    CHECK_GE(index, 0);
    ALOGI("Got bandwidth index : %d, prev bandwidth index : %d\n", index, mCurBandwidthIndex);
    return index;
}

int64_t LiveSession::latestMediaSegmentStartTimeUs() {
    sp<AMessage> audioMeta = mPacketSources.valueFor(STREAMTYPE_AUDIO)->getLatestDequeuedMeta();
    int64_t minSegmentStartTimeUs = -1, videoSegmentStartTimeUs = -1;
    if (audioMeta != NULL) {
        audioMeta->findInt64("segmentStartTimeUs", &minSegmentStartTimeUs);
    }

    sp<AMessage> videoMeta = mPacketSources.valueFor(STREAMTYPE_VIDEO)->getLatestDequeuedMeta();
    if (videoMeta != NULL
            && videoMeta->findInt64("segmentStartTimeUs", &videoSegmentStartTimeUs)) {
        if (minSegmentStartTimeUs < 0 || videoSegmentStartTimeUs < minSegmentStartTimeUs) {
            minSegmentStartTimeUs = videoSegmentStartTimeUs;
        }

    }
    return minSegmentStartTimeUs;
}

int64_t LiveSession::getSegmentStartTimeUsAfterSeek(int64_t seekUs) {
    int64_t minStartTimeUs = -1, tempTimeUs = -1;
    int32_t number;
    for (size_t i = 0; i < mFetcherInfos.size(); i++) {
        number = mFetcherInfos.valueAt(i).mFetcher->getSeqNumberForTime(seekUs);
        tempTimeUs = mFetcherInfos.valueAt(i).mFetcher->getSegmentStartTimeUs(number);
        if (minStartTimeUs < 0) {
            minStartTimeUs = tempTimeUs;
        } else {
            minStartTimeUs = minStartTimeUs <= tempTimeUs ? minStartTimeUs : tempTimeUs;
        }
    }
    return minStartTimeUs;
}

status_t LiveSession::onSeek(const sp<AMessage> &msg) {
    int64_t timeUs;
    CHECK(msg->findInt64("timeUs", &timeUs));

    // clear discontinuity.
    for (size_t i = 0; i < kMaxStreams; ++i) {
        sp<AnotherPacketSource> discontinuityQueue  = mDiscontinuities.valueFor(indexToType(i));
        discontinuityQueue->clear();
    }

    if (!mReconfigurationInProgress) {
        changeConfiguration(timeUs, mCurBandwidthIndex);
        return OK;
    } else {
        return -EWOULDBLOCK;
    }
}

status_t LiveSession::getDuration(int64_t *durationUs) const {
    int64_t maxDurationUs = -1ll;
    for (size_t i = 0; i < mFetcherInfos.size(); ++i) {
        int64_t fetcherDurationUs = mFetcherInfos.valueAt(i).mDurationUs;

        if (fetcherDurationUs > maxDurationUs) {
            maxDurationUs = fetcherDurationUs;
        }
    }

    *durationUs = maxDurationUs;

    return OK;
}

bool LiveSession::isSeekable() const {
    int64_t durationUs;
    return getDuration(&durationUs) == OK && durationUs >= 0;
}

bool LiveSession::hasDynamicDuration() const {
    return false;
}

size_t LiveSession::getTrackCount() const {
    if (mPlaylist == NULL) {
        return 0;
    } else {
        return mPlaylist->getTrackCount();
    }
}

sp<AMessage> LiveSession::getTrackInfo(size_t trackIndex) const {
    if (mPlaylist == NULL) {
        return NULL;
    } else {
        return mPlaylist->getTrackInfo(trackIndex);
    }
}

status_t LiveSession::selectTrack(size_t index, bool select) {
    if (mPlaylist == NULL) {
        return INVALID_OPERATION;
    }

    ++mSubtitleGeneration;
    status_t err = mPlaylist->selectTrack(index, select);
    if (err == OK) {
        sp<AMessage> msg = new AMessage(kWhatChangeConfiguration, id());
        msg->setInt32("bandwidthIndex", mCurBandwidthIndex);
        msg->setInt32("pickTrack", select);
        msg->post();
    }
    return err;
}

ssize_t LiveSession::getSelectedTrack(media_track_type type) const {
    if (mPlaylist == NULL) {
        return -1;
    } else {
        return mPlaylist->getSelectedTrack(type);
    }
}

bool LiveSession::canSwitchUp() {
    // Allow upwards bandwidth switch when a stream has buffered at least 10 seconds.
    status_t err = OK;
    for (size_t i = 0; i < mPacketSources.size(); ++i) {
        sp<AnotherPacketSource> source = mPacketSources.valueAt(i);
        int64_t dur = source->getBufferedDurationUs(&err);
        if (err == OK && dur > 10000000) {
            return true;
        }
    }
    return false;
}

void LiveSession::changeConfiguration(
        int64_t timeUs, size_t bandwidthIndex, bool pickTrack) {
    // Protect mPacketSources from a swapPacketSource race condition through reconfiguration.
    // (changeConfiguration, onChangeConfiguration2, onChangeConfiguration3).
    cancelBandwidthSwitch();

    CHECK(!mReconfigurationInProgress);
    mReconfigurationInProgress = true;

    mCurBandwidthIndex = bandwidthIndex;

    ALOGV("changeConfiguration => timeUs:%" PRId64 " us, bwIndex:%zu, pickTrack:%d",
          timeUs, bandwidthIndex, pickTrack);

    CHECK_LT(bandwidthIndex, mBandwidthItems.size());
    const BandwidthItem &item = mBandwidthItems.itemAt(bandwidthIndex);

    uint32_t streamMask = 0; // streams that should be fetched by the new fetcher
    uint32_t resumeMask = 0; // streams that should be fetched by the original fetcher

    AString URIs[kMaxStreams];
    for (size_t i = 0; i < kMaxStreams; ++i) {
        if (mPlaylist->getTypeURI(item.mPlaylistIndex, mStreams[i].mType, &URIs[i])) {
            streamMask |= indexToType(i);
        }
    }

    // Step 1, stop and discard fetchers that are no longer needed.
    // Pause those that we'll reuse.
    for (size_t i = 0; i < mFetcherInfos.size(); ++i) {
        const AString &uri = mFetcherInfos.keyAt(i);

        bool discardFetcher = true;

        // If we're seeking all current fetchers are discarded.
        if (timeUs < 0ll) {
            // delay fetcher removal if not picking tracks
            discardFetcher = pickTrack;

            for (size_t j = 0; j < kMaxStreams; ++j) {
                StreamType type = indexToType(j);
                if ((streamMask & type) && uri == URIs[j]) {
                    resumeMask |= type;
                    streamMask &= ~type;
                    discardFetcher = false;
                }
            }
        }

        if (discardFetcher) {
            mFetcherInfos.valueAt(i).mFetcher->stopAsync();
        } else {
            mFetcherInfos.valueAt(i).mFetcher->pauseAsync();
            FetcherInfo info = mFetcherInfos.valueAt(i);
            info.mStatus = STATUS_PAUSED;
        }
    }

    sp<AMessage> msg;
    if (timeUs < 0ll) {
        // skip onChangeConfiguration2 (decoder destruction) if not seeking.
        msg = new AMessage(kWhatChangeConfiguration3, id());
    } else {
        msg = new AMessage(kWhatChangeConfiguration2, id());
    }
    msg->setInt32("streamMask", streamMask);
    msg->setInt32("resumeMask", resumeMask);
    msg->setInt32("pickTrack", pickTrack);
    msg->setInt64("timeUs", timeUs);
    for (size_t i = 0; i < kMaxStreams; ++i) {
        if ((streamMask | resumeMask) & indexToType(i)) {
            msg->setString(mStreams[i].uriKey().c_str(), URIs[i].c_str());
        }
    }

    // Every time a fetcher acknowledges the stopAsync or pauseAsync request
    // we'll decrement mContinuationCounter, once it reaches zero, i.e. all
    // fetchers have completed their asynchronous operation, we'll post
    // mContinuation, which then is handled below in onChangeConfiguration2.
    mContinuationCounter = mFetcherInfos.size();
    mContinuation = msg;

    if (mContinuationCounter == 0) {
        msg->post();

        if (mSeekReplyID != 0) {
            CHECK(mSeekReply != NULL);
            mSeekReply->setInt32("err", OK);
            mSeekReply->postReply(mSeekReplyID);
            mSeekReplyID = 0;
            mSeekReply.clear();
        }
    }
}

void LiveSession::onChangeConfiguration(const sp<AMessage> &msg) {
    if (!mReconfigurationInProgress) {
        int32_t pickTrack = 0, bandwidthIndex = mCurBandwidthIndex;
        msg->findInt32("pickTrack", &pickTrack);
        msg->findInt32("bandwidthIndex", &bandwidthIndex);
        changeConfiguration(-1ll /* timeUs */, bandwidthIndex, pickTrack);
    } else {
        msg->post(100000ll); // retry in 100 ms
    }
}

void LiveSession::onChangeConfiguration2(const sp<AMessage> &msg) {
    mContinuation.clear();

    // All fetchers are either suspended or have been removed now.

    uint32_t streamMask, resumeMask;
    CHECK(msg->findInt32("streamMask", (int32_t *)&streamMask));
    CHECK(msg->findInt32("resumeMask", (int32_t *)&resumeMask));

    // currently onChangeConfiguration2 is only called for seeking;
    // remove the following CHECK if using it else where.
    CHECK_EQ(resumeMask, 0);
    streamMask |= resumeMask;

    AString URIs[kMaxStreams];
    for (size_t i = 0; i < kMaxStreams; ++i) {
        if (streamMask & indexToType(i)) {
            const AString &uriKey = mStreams[i].uriKey();
            CHECK(msg->findString(uriKey.c_str(), &URIs[i]));
            ALOGV("%s = '%s'", uriKey.c_str(), URIs[i].c_str());
        }
    }

    // Determine which decoders to shutdown on the player side,
    // a decoder has to be shutdown if either
    // 1) its streamtype was active before but now longer isn't.
    // or
    // 2) its streamtype was already active and still is but the URI
    //    has changed.
    uint32_t changedMask = 0;
    for (size_t i = 0; i < kMaxStreams && i != kSubtitleIndex; ++i) {
        if (((mStreamMask & streamMask & indexToType(i))
                && !(URIs[i] == mStreams[i].mUri))
                || (mStreamMask & ~streamMask & indexToType(i))) {
            changedMask |= indexToType(i);
        }
    }

    if (changedMask == 0) {
        // If nothing changed as far as the audio/video decoders
        // are concerned we can proceed.
        onChangeConfiguration3(msg);
        return;
    }

    // Something changed, inform the player which will shutdown the
    // corresponding decoders and will post the reply once that's done.
    // Handling the reply will continue executing below in
    // onChangeConfiguration3.
    sp<AMessage> notify = mNotify->dup();
    notify->setInt32("what", kWhatStreamsChanged);
    notify->setInt32("changedMask", changedMask);

    msg->setWhat(kWhatChangeConfiguration3);
    msg->setTarget(id());

    notify->setMessage("reply", msg);
    notify->post();
}

void LiveSession::onChangeConfiguration3(const sp<AMessage> &msg) {
    mContinuation.clear();
    // All remaining fetchers are still suspended, the player has shutdown
    // any decoders that needed it.

    uint32_t streamMask, resumeMask;
    CHECK(msg->findInt32("streamMask", (int32_t *)&streamMask));
    CHECK(msg->findInt32("resumeMask", (int32_t *)&resumeMask));

    int64_t timeUs;
    int32_t pickTrack;
    bool switching = false;
    CHECK(msg->findInt64("timeUs", &timeUs));
    CHECK(msg->findInt32("pickTrack", &pickTrack));

    if (timeUs < 0ll) {
        if (!pickTrack) {
            switching = true;
        }
        mRealTimeBaseUs = ALooper::GetNowUs() - mLastDequeuedTimeUs;
    } else {
        mRealTimeBaseUs = ALooper::GetNowUs() - timeUs;
    }

    for (size_t i = 0; i < kMaxStreams; ++i) {
        if (streamMask & indexToType(i)) {
            if (switching) {
                CHECK(msg->findString(mStreams[i].uriKey().c_str(), &mStreams[i].mNewUri));
            } else {
                CHECK(msg->findString(mStreams[i].uriKey().c_str(), &mStreams[i].mUri));
            }
        }
    }

    mNewStreamMask = streamMask | resumeMask;
    if (switching) {
        mSwapMask = mStreamMask & ~resumeMask;
    }

    // Of all existing fetchers:
    // * Resume fetchers that are still needed and assign them original packet sources.
    // * Mark otherwise unneeded fetchers for removal.
    ALOGV("resuming fetchers for mask 0x%08x", resumeMask);
    for (size_t i = 0; i < mFetcherInfos.size(); ++i) {
        const AString &uri = mFetcherInfos.keyAt(i);

        sp<AnotherPacketSource> sources[kMaxStreams];
        for (size_t j = 0; j < kMaxStreams; ++j) {
            if ((resumeMask & indexToType(j)) && uri == mStreams[j].mUri) {
                sources[j] = mPacketSources.valueFor(indexToType(j));

                if (j != kSubtitleIndex) {
                    ALOGV("queueing dummy discontinuity for stream type %d", indexToType(j));
                    sp<AnotherPacketSource> discontinuityQueue;
                    discontinuityQueue = mDiscontinuities.valueFor(indexToType(j));
                    discontinuityQueue->queueDiscontinuity(
                            ATSParser::DISCONTINUITY_NONE,
                            NULL,
                            true);
                }
            }
        }

        FetcherInfo &info = mFetcherInfos.editValueAt(i);
        if (sources[kAudioIndex] != NULL || sources[kVideoIndex] != NULL
                || sources[kSubtitleIndex] != NULL) {
            info.mFetcher->startAsync(
                    sources[kAudioIndex], sources[kVideoIndex], sources[kSubtitleIndex]);
            info.mStatus = STATUS_ACTIVE;
        } else {
            info.mToBeRemoved = true;
        }
    }

    // streamMask now only contains the types that need a new fetcher created.

    if (streamMask != 0) {
        ALOGV("creating new fetchers for mask 0x%08x", streamMask);
    }

    // Find out when the original fetchers have buffered up to and start the new fetchers
    // at a later timestamp.
    for (size_t i = 0; i < kMaxStreams; i++) {
        if (!(indexToType(i) & streamMask)) {
            continue;
        }

        AString uri;
        uri = switching ? mStreams[i].mNewUri : mStreams[i].mUri;

        sp<PlaylistFetcher> fetcher = addFetcher(uri.c_str());
        CHECK(fetcher != NULL);

        int32_t latestSeq = -1;
        int64_t startTimeUs = -1;
        int64_t segmentStartTimeUs = -1ll;
        int32_t discontinuitySeq = -1;
        sp<AnotherPacketSource> sources[kMaxStreams];

        if (i == kSubtitleIndex) {
            segmentStartTimeUs = latestMediaSegmentStartTimeUs();
        }

        // TRICKY: looping from i as earlier streams are already removed from streamMask
        for (size_t j = i; j < kMaxStreams; ++j) {
            const AString &streamUri = switching ? mStreams[j].mNewUri : mStreams[j].mUri;
            if ((streamMask & indexToType(j)) && uri == streamUri) {
                sources[j] = mPacketSources.valueFor(indexToType(j));

                if (timeUs >= 0) {
                    sources[j]->clear();
                    startTimeUs = timeUs;

                    sp<AnotherPacketSource> discontinuityQueue;
                    sp<AMessage> extra = new AMessage;
                    extra->setInt64("timeUs", timeUs);
                    discontinuityQueue = mDiscontinuities.valueFor(indexToType(j));
                    discontinuityQueue->queueDiscontinuity(
                            ATSParser::DISCONTINUITY_TIME, extra, true);
                } else {
                    int32_t type;
                    int64_t srcSegmentStartTimeUs;
                    sp<AMessage> meta;
                    if (pickTrack) {
                        // selecting
                        meta = sources[j]->getLatestDequeuedMeta();
                    } else {
                        // adapting
                        meta = sources[j]->getLatestEnqueuedMeta();
                    }

                    if (meta != NULL && !meta->findInt32("discontinuity", &type)) {
                        int64_t tmpUs;
                        int64_t tmpSegmentUs;

                        CHECK(meta->findInt64("timeUs", &tmpUs));
                        CHECK(meta->findInt64("segmentStartTimeUs", &tmpSegmentUs));
                        if (startTimeUs < 0 || tmpSegmentUs < segmentStartTimeUs) {
                            startTimeUs = tmpUs;
                            segmentStartTimeUs = tmpSegmentUs;
                        } else if (tmpSegmentUs == segmentStartTimeUs && tmpUs < startTimeUs) {
                            startTimeUs = tmpUs;
                        }

                        int32_t seq;
                        CHECK(meta->findInt32("discontinuitySeq", &seq));
                        if (discontinuitySeq < 0 || seq < discontinuitySeq) {
                            discontinuitySeq = seq;
                        }
                    }

                    if (pickTrack) {
                        // selecting track, queue discontinuities before content
                        sources[j]->clear();
                        if (j == kSubtitleIndex) {
                            break;
                        }
                        sp<AnotherPacketSource> discontinuityQueue;
                        discontinuityQueue = mDiscontinuities.valueFor(indexToType(j));
                        discontinuityQueue->queueDiscontinuity(
                                ATSParser::DISCONTINUITY_FORMATCHANGE, NULL, true);
                    } else {
                        // adapting, queue discontinuities after resume
                        sources[j] = mPacketSources2.valueFor(indexToType(j));
                        sources[j]->clear();
                        uint32_t extraStreams = mNewStreamMask & (~mStreamMask);
                        if (extraStreams & indexToType(j)) {
                            sources[j]->queueAccessUnit(createFormatChangeBuffer(/*swap*/ false));
                        }
                    }
                }

                streamMask &= ~indexToType(j);
            }
        }

        fetcher->startAsync(
                sources[kAudioIndex],
                sources[kVideoIndex],
                sources[kSubtitleIndex],
                startTimeUs < 0 ? mLastSeekTimeUs : startTimeUs,
                segmentStartTimeUs,
                discontinuitySeq,
                switching);
    }

    // All fetchers have now been started, the configuration change
    // has completed.

    //cancelCheckBandwidthEvent();
    //scheduleCheckBandwidthEvent();

    ALOGV("XXX configuration change completed.");
    mReconfigurationInProgress = false;
    if (switching) {
        mSwitchInProgress = true;
    } else {
        mStreamMask = mNewStreamMask;
    }

    if (mDisconnectReplyID != 0) {
        finishDisconnect();
    }
}

void LiveSession::onSwapped(const sp<AMessage> &msg) {
    int32_t switchGeneration;
    CHECK(msg->findInt32("switchGeneration", &switchGeneration));
    if (switchGeneration != mSwitchGeneration) {
        return;
    }

    int32_t stream;
    CHECK(msg->findInt32("stream", &stream));

    ssize_t idx = typeToIndex(stream);
    CHECK(idx >= 0);
    if ((mNewStreamMask & stream) && mStreams[idx].mNewUri.empty()) {
        ALOGW("swapping stream type %d %s to empty stream", stream, mStreams[idx].mUri.c_str());
    }
    mStreams[idx].mUri = mStreams[idx].mNewUri;
    mStreams[idx].mNewUri.clear();

    mSwapMask &= ~stream;
    if (mSwapMask != 0) {
        return;
    }

    // Check if new variant contains extra streams.
    uint32_t extraStreams = mNewStreamMask & (~mStreamMask);
    while (extraStreams) {
        StreamType extraStream = (StreamType) (extraStreams & ~(extraStreams - 1));
        swapPacketSource(extraStream);
        extraStreams &= ~extraStream;

        idx = typeToIndex(extraStream);
        CHECK(idx >= 0);
        if (mStreams[idx].mNewUri.empty()) {
            ALOGW("swapping extra stream type %d %s to empty stream",
                    extraStream, mStreams[idx].mUri.c_str());
        }
        mStreams[idx].mUri = mStreams[idx].mNewUri;
        mStreams[idx].mNewUri.clear();
    }

    tryToFinishBandwidthSwitch();
}

void LiveSession::onCheckSwitchDown() {
    if (mSwitchDownMonitor == NULL) {
        return;
    }

    if (mSwitchInProgress || mReconfigurationInProgress) {
        ALOGV("Switch/Reconfig in progress, defer switch down");
        mSwitchDownMonitor->post(1000000ll);
        return;
    }

    for (size_t i = 0; i < kMaxStreams; ++i) {
        int32_t targetDuration;
        sp<AnotherPacketSource> packetSource = mPacketSources.valueFor(indexToType(i));
        sp<AMessage> meta = packetSource->getLatestDequeuedMeta();

        if (meta != NULL && meta->findInt32("targetDuration", &targetDuration) ) {
            int64_t bufferedDurationUs = packetSource->getEstimatedDurationUs();
            int64_t targetDurationUs = targetDuration * 1000000ll;

            if (bufferedDurationUs < targetDurationUs / 3) {
                (new AMessage(kWhatSwitchDown, id()))->post();
                break;
            }
        }
    }

    mSwitchDownMonitor->post(1000000ll);
}

void LiveSession::onSwitchDown() {
    if (mReconfigurationInProgress || mSwitchInProgress || mCurBandwidthIndex == 0) {
        return;
    }

    ssize_t bandwidthIndex = getBandwidthIndex();
    if (bandwidthIndex < mCurBandwidthIndex) {
        changeConfiguration(-1, bandwidthIndex, false);
        return;
    }

}

// Mark switch done when:
//   1. all old buffers are swapped out
void LiveSession::tryToFinishBandwidthSwitch() {
    if (!mSwitchInProgress) {
        return;
    }

    bool needToRemoveFetchers = false;
    for (size_t i = 0; i < mFetcherInfos.size(); ++i) {
        if (mFetcherInfos.valueAt(i).mToBeRemoved) {
            needToRemoveFetchers = true;
            break;
        }
    }

    if (!needToRemoveFetchers && mSwapMask == 0) {
        ALOGI("mSwitchInProgress = false");
        mStreamMask = mNewStreamMask;
        mSwitchInProgress = false;
    }
}

void LiveSession::scheduleCheckBandwidthEvent() {
    sp<AMessage> msg = new AMessage(kWhatCheckBandwidth, id());
    msg->setInt32("generation", mCheckBandwidthGeneration);
    msg->post(10000000ll);
}

void LiveSession::cancelCheckBandwidthEvent() {
    ++mCheckBandwidthGeneration;
}

void LiveSession::cancelBandwidthSwitch() {
    Mutex::Autolock lock(mSwapMutex);
    mSwitchGeneration++;
    mSwitchInProgress = false;
    mSwapMask = 0;

    for (size_t i = 0; i < mFetcherInfos.size(); ++i) {
        FetcherInfo& info = mFetcherInfos.editValueAt(i);
        if (info.mToBeRemoved) {
            info.mToBeRemoved = false;
        }
    }

    for (size_t i = 0; i < kMaxStreams; ++i) {
        if (!mStreams[i].mNewUri.empty()) {
            ssize_t j = mFetcherInfos.indexOfKey(mStreams[i].mNewUri);
            if (j < 0) {
                mStreams[i].mNewUri.clear();
                continue;
            }

            const FetcherInfo &info = mFetcherInfos.valueAt(j);
            info.mFetcher->stopAsync();
            mFetcherInfos.removeItemsAt(j);
            mStreams[i].mNewUri.clear();
        }
    }
}

bool LiveSession::canSwitchBandwidthTo(size_t bandwidthIndex) {
    if (mReconfigurationInProgress || mSwitchInProgress) {
        return false;
    }

    if (mCurBandwidthIndex < 0) {
        return true;
    }

    if (bandwidthIndex == (size_t)mCurBandwidthIndex) {
        return false;
    } else if (bandwidthIndex > (size_t)mCurBandwidthIndex) {
        return canSwitchUp();
    } else {
        return true;
    }
}

void LiveSession::onCheckBandwidth(const sp<AMessage> &msg) {
    size_t bandwidthIndex = getBandwidthIndex();
    if (canSwitchBandwidthTo(bandwidthIndex)) {
        changeConfiguration(-1ll /* timeUs */, bandwidthIndex);
    } else {
        // Come back and check again 10 seconds later in case there is nothing to do now.
        // If we DO change configuration, once that completes it'll schedule a new
        // check bandwidth event with an incremented mCheckBandwidthGeneration.
        msg->post(10000000ll);
    }
}

void LiveSession::reconfigFetcher(size_t bandwidthIndex) {
    mCurBandwidthIndex = bandwidthIndex;
    const BandwidthItem &item = mBandwidthItems.itemAt(bandwidthIndex);
    AString audioURI, videoURI, subURI;
    bool a, v, s;
    a = mPlaylist->getTypeURI(item.mPlaylistIndex, "audio", &audioURI);
    v = mPlaylist->getTypeURI(item.mPlaylistIndex, "video", &videoURI);
    s = mPlaylist->getTypeURI(item.mPlaylistIndex, "subtitles", &subURI);
    KeyedVector<AString, FetcherInfo> newFetcherInfos;

    // hls stream maybe split into independent tracks accord to new draft.
    // we need to handle the case which track varied.
    // process below is not required now, maybe useful afterwards.
    int32_t streamNum = 0;
    if (a && !audioURI.empty()) {
        streamNum++;
    }
    if ((v && !videoURI.empty()) && !(videoURI == audioURI)) {
        streamNum++;
    }
    if (s && !subURI.empty()) {
        streamNum++;
    }

    if (!streamNum) {
        return;
    }

    int32_t i;
    int32_t info_size = mFetcherInfos.size();
    uint32_t finalStreamMask = 0ul;
    bool changeFetcher = true;
    for (i = 0; i < info_size; ++i) {
        uint32_t streamMask = mFetcherInfos.valueAt(i).mFetcher->getStreamTypeMask();
        uint32_t newStreamMask = 0ul;
        AString newURI;
        if (changeFetcher) {
            if ((a && !audioURI.empty()) && (streamMask & STREAMTYPE_AUDIO)) {
                mFetcherInfos.valueAt(i).mFetcher->changeURI(audioURI);
                newURI = audioURI;
                newStreamMask |= STREAMTYPE_AUDIO;
                finalStreamMask |= STREAMTYPE_AUDIO;
            }
            if ((v && !videoURI.empty()) && (streamMask & STREAMTYPE_VIDEO)) {
                mFetcherInfos.valueAt(i).mFetcher->changeURI(videoURI);
                newURI = videoURI;
                newStreamMask |= STREAMTYPE_VIDEO;
                finalStreamMask |= STREAMTYPE_VIDEO;
            }
            if ((s && !subURI.empty()) && (streamMask & STREAMTYPE_SUBTITLES)) {
                mFetcherInfos.valueAt(i).mFetcher->changeURI(subURI);
                newURI = subURI;
                newStreamMask |= STREAMTYPE_SUBTITLES;
                finalStreamMask |= STREAMTYPE_SUBTITLES;
            }

            FetcherInfo info = mFetcherInfos.editValueAt(i);
            info.mFetcher->setStreamTypeMask(newStreamMask);
            if (info.mStatus == STATUS_PAUSED) {
                // TODO:  fetcher start logic.
            } else {
                if (newStreamMask == 0) {
                    // TODO: need to change packet source.
                }
            }
            newFetcherInfos.add(newURI, info);
            ALOGI("URI after reconfiguration : %s \n", newFetcherInfos.keyAt(i).c_str());
        } else {
            FetcherInfo info = mFetcherInfos.editValueAt(i);
            if (info.mStatus == STATUS_ACTIVE) {
                info.mFetcher->pauseAsync();
                info.mStatus == STATUS_PAUSED;
            }
            newFetcherInfos.add(newURI, info);
        }
        if (i == streamNum) {
            changeFetcher = false;
        }
    }

    if (i < streamNum) {  // need to add fetchers
        // TODO: fetcher add logic.
    }
    mFetcherInfos.clear();
    mFetcherInfos = newFetcherInfos;
}

void LiveSession::checkBandwidth(bool * needFetchPlaylist) {
    if (mReconfigurationInProgress) {
        return;
    }

    size_t bandwidthIndex = getBandwidthIndex();
    if (mCurBandwidthIndex < 0
            || bandwidthIndex != (size_t)mCurBandwidthIndex) {
        reconfigFetcher(bandwidthIndex);
        *needFetchPlaylist = true;
    }

    // Handling the kWhatCheckBandwidth even here does _not_ automatically
    // schedule another one on return, only an explicit call to
    // scheduleCheckBandwidthEvent will do that.
    // This ensures that only one configuration change is ongoing at any
    // one time, once that completes it'll schedule another check bandwidth
    // event.
}

void LiveSession::postPrepared(status_t err) {
    CHECK(mInPreparationPhase);

    sp<AMessage> notify = mNotify->dup();
    if (err == OK || err == ERROR_END_OF_STREAM) {
        notify->setInt32("what", kWhatPrepared);
    } else {
        notify->setInt32("what", kWhatPreparationFailed);
        notify->setInt32("err", err);
    }

    notify->post();

    mInPreparationPhase = false;

    //mSwitchDownMonitor = new AMessage(kWhatCheckSwitchDown, id());
    //mSwitchDownMonitor->post();
}

}  // namespace android

