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

#define LOG_NDEBUG 0
#define LOG_TAG "NU-AmPlaylistFetcher"
#include <utils/Log.h>
#include <utils/misc.h>

#include "AmPlaylistFetcher.h"

#include "AmLiveDataSource.h"
#include "AmHLSDataSource.h"
#include "AmLiveSession.h"
#include "AmM3UParser.h"

#include "include/avc_utils.h"
#include "include/HTTPBase.h"
#include "include/ID3.h"
#include "AmAnotherPacketSource.h"

#include <cutils/properties.h>
#include <media/IStreamSource.h>
#include <media/stagefright/foundation/ABitReader.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/hexdump.h>
#include <media/stagefright/FileSource.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/Utils.h>
#include <media/stagefright/MediaBuffer.h>
#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MediaExtractor.h>


#include <ctype.h>
#include <inttypes.h>
#include <openssl/aes.h>
#include <openssl/md5.h>

#include <curl/curl.h>
#include "curl_fetch.h"

namespace android {

// static
const int64_t AmPlaylistFetcher::kMinBufferedDurationUs = 10000000ll;
const int64_t AmPlaylistFetcher::kMaxMonitorDelayUs = 3000000ll;
// LCM of 188 (size of a TS packet) & 1k works well
const int32_t AmPlaylistFetcher::kDownloadBlockSize = 47 * 1024;
const int32_t AmPlaylistFetcher::kNumSkipFrames = 5;
// use 12 frames to calculate frame rate
const size_t  AmPlaylistFetcher::kFrameNum = 12;

const AString AmPlaylistFetcher::DumpPath = "/data/tmp/";

AmPlaylistFetcher::AmPlaylistFetcher(
        const sp<AMessage> &notify,
        const sp<AmLiveSession> &session,
        const sp<AmM3UParser> &playlist,
        const char *uri,
        int32_t subtitleGeneration)
    : mDumpMode(-1),
      mDumpHandle(NULL),
      mSegmentBytesPerSec(0),
      mFailureAnchorTimeUs(0),
      mOpenFailureRetryUs(0),
      mNotify(notify),
      mStartTimeUsNotify(notify->dup()),
      mSession(session),
      mURI(uri),
      mStreamTypeMask(0),
      mStartTimeUs(-1ll),
      mSeekedTimeUs(-1ll),
      mSegmentStartTimeUs(-1ll),
      mDiscontinuitySeq(-1ll),
      mStartTimeUsRelative(false),
      mLastPlaylistFetchTimeUs(-1ll),
      mPlaylist(playlist),
      mSeqNumber(-1),
      mDownloadedNum(0),
      mNumRetries(0),
      mRetryTimeOverS(-1),
      mRetryAnchorTimeUs(-1ll),
      mNeedSniff(true),
      mIsTs(false),
      mFirstRefresh(true),
      mFirstTypeProbe(true),
      mStartup(true),
      mSeeked(false),
      mAdaptive(false),
      mFetchingNotify(false),
      mPrepared(false),
      mPostPrepared(false),
      mNextPTSTimeUs(-1ll),
      mBuffering(false),
      mMonitorQueueGeneration(0),
      mSubtitleGeneration(subtitleGeneration),
      mRefreshState(INITIAL_MINIMUM_RELOAD_DELAY),
      mExtractor(NULL),
      mAudioTrack(NULL),
      mVideoTrack(NULL),
      mAudioSource(NULL),
      mVideoSource(NULL),
      mEnableFrameRate(false),
      mFirstPTSValid(false),
      mHasMetadata(false),
      mAbsoluteTimeAnchorUs(0ll),
      mVideoBuffer(new AmAnotherPacketSource(NULL)) {
    memset(mPlaylistHash, 0, sizeof(mPlaylistHash));
    mStartTimeUsNotify->setInt32("what", kWhatStartedAt);
    mStartTimeUsNotify->setInt32("streamMask", 0);

    char value[PROPERTY_VALUE_MAX];
    if (property_get("media.hls.dumpmode", value, NULL)) {
        mDumpMode = atoi(value);
    }
    if (property_get("media.hls.open_retry_s", value, "3600")) {
        mOpenFailureRetryUs = atoll(value) * 1000000;
    }
    if (property_get("media.hls.frame-rate", value, NULL)) {
        mEnableFrameRate = atoi(value);
    }
    if (property_get("media.hls.retry_timeover_s", value, "3600")) {
        mRetryTimeOverS = atoi(value);
    }
}

AmPlaylistFetcher::~AmPlaylistFetcher() {
    if (mDumpHandle) {
        fclose(mDumpHandle);
    }
}

int64_t AmPlaylistFetcher::getSegmentStartTimeUs(int32_t seqNumber) const {
    CHECK(mPlaylist != NULL);

    int32_t firstSeqNumberInPlaylist;
    if (mPlaylist->meta() == NULL || !mPlaylist->meta()->findInt32(
                "media-sequence", &firstSeqNumberInPlaylist)) {
        firstSeqNumberInPlaylist = 0;
    }

    int32_t lastSeqNumberInPlaylist =
        firstSeqNumberInPlaylist + (int32_t)mPlaylist->size() - 1;

    if (seqNumber < firstSeqNumberInPlaylist
        || seqNumber > lastSeqNumberInPlaylist) {
        return 0ll;
    }

    //CHECK_GE(seqNumber, firstSeqNumberInPlaylist);
    //CHECK_LE(seqNumber, lastSeqNumberInPlaylist);

    int64_t segmentStartUs = 0ll;
    for (int32_t index = 0;
            index < seqNumber - firstSeqNumberInPlaylist; ++index) {
        sp<AMessage> itemMeta;
        CHECK(mPlaylist->itemAt(
                    index, NULL /* uri */, &itemMeta));

        int64_t itemDurationUs;
        CHECK(itemMeta->findInt64("durationUs", &itemDurationUs));

        segmentStartUs += itemDurationUs;
    }

    return segmentStartUs;
}

int64_t AmPlaylistFetcher::getSeekedTimeUs() const {
    return mSeekedTimeUs;
}

int64_t AmPlaylistFetcher::delayUsToRefreshPlaylist() const {
    int64_t nowUs = ALooper::GetNowUs();

    if (mPlaylist == NULL) {
        CHECK_EQ((int)mRefreshState, (int)INITIAL_MINIMUM_RELOAD_DELAY);
        return 0ll;
    }

    if (mPlaylist->isComplete()) {
        return (~0llu >> 1);
    }

    int32_t targetDurationSecs;
    CHECK(mPlaylist->meta()->findInt32("target-duration", &targetDurationSecs));

    int64_t targetDurationUs = targetDurationSecs * 1000000ll;

    int64_t minPlaylistAgeUs;

    switch (mRefreshState) {
        case INITIAL_MINIMUM_RELOAD_DELAY:
        {
            size_t n = mPlaylist->size();
            if (n > 0) {
                sp<AMessage> itemMeta;
                CHECK(mPlaylist->itemAt(n - 1, NULL /* uri */, &itemMeta));

                int64_t itemDurationUs;
                CHECK(itemMeta->findInt64("durationUs", &itemDurationUs));

                minPlaylistAgeUs = itemDurationUs;
                break;
            }

            // fall through
        }

        case FIRST_UNCHANGED_RELOAD_ATTEMPT:
        {
            minPlaylistAgeUs = targetDurationUs / 2;
            break;
        }

        case SECOND_UNCHANGED_RELOAD_ATTEMPT:
        {
            minPlaylistAgeUs = (targetDurationUs * 3) / 2;
            break;
        }

        case THIRD_UNCHANGED_RELOAD_ATTEMPT:
        {
            minPlaylistAgeUs = targetDurationUs * 3;
            break;
        }

        default:
            TRESPASS();
            break;
    }

    int64_t delayUs = mLastPlaylistFetchTimeUs + minPlaylistAgeUs - nowUs;
    return delayUs > 0ll ? delayUs : 0ll;
}

status_t AmPlaylistFetcher::decryptBuffer(
        size_t playlistIndex, const sp<ABuffer> &buffer,
        bool first) {
    sp<AMessage> itemMeta;
    bool found = false;
    AString method;

    for (ssize_t i = playlistIndex; i >= 0; --i) {
        AString uri;
        CHECK(mPlaylist->itemAt(i, &uri, &itemMeta));

        if (itemMeta->findString("cipher-method", &method)) {
            found = true;
            break;
        }
    }

    if (!found) {
        method = "NONE";
    }
    buffer->meta()->setString("cipher-method", method.c_str());

    if (method == "NONE") {
        return OK;
    } else if (!(method == "AES-128")) {
        ALOGE("Unsupported cipher method '%s'", method.c_str());
        return ERROR_MALFORMED;
    }

    AString keyURI;
    if (!itemMeta->findString("cipher-uri", &keyURI)) {
        ALOGE("Missing key uri");
        return ERROR_MALFORMED;
    }

    ssize_t index = mAESKeyForURI.indexOfKey(keyURI);

    sp<ABuffer> key;
    if (index >= 0) {
        key = mAESKeyForURI.valueAt(index);
    } else {
        CFContext * cfc_handle = NULL;
        ssize_t err = mSession->fetchFile(keyURI.c_str(), &key, 0, -1, 0, &cfc_handle);
        if (cfc_handle) {
            curl_fetch_close(cfc_handle);
        }

        if (err == ERROR_CANNOT_CONNECT) {
            return err;
        }

        if (err < 0) {
            ALOGE("failed to fetch cipher key from '%s'.", keyURI.c_str());
            return ERROR_IO;
        } else if (key->size() != 16) {
            ALOGE("key file '%s' wasn't 16 bytes in size.", keyURI.c_str());
            return ERROR_MALFORMED;
        }

        mAESKeyForURI.add(keyURI, key);
    }

    AES_KEY aes_key;
    if (AES_set_decrypt_key(key->data(), 128, &aes_key) != 0) {
        ALOGE("failed to set AES decryption key.");
        return UNKNOWN_ERROR;
    }

    size_t n = buffer->size();
    if (!n) {
        return OK;
    }
    CHECK(n % 16 == 0);

    if (first) {
        // If decrypting the first block in a file, read the iv from the manifest
        // or derive the iv from the file's sequence number.

        AString iv;
        if (itemMeta->findString("cipher-iv", &iv)) {
            if ((!iv.startsWith("0x") && !iv.startsWith("0X"))
                    || iv.size() != 16 * 2 + 2) {
                ALOGE("malformed cipher IV '%s'.", iv.c_str());
                return ERROR_MALFORMED;
            }

            memset(mAESInitVec, 0, sizeof(mAESInitVec));
            for (size_t i = 0; i < 16; ++i) {
                char c1 = tolower(iv.c_str()[2 + 2 * i]);
                char c2 = tolower(iv.c_str()[3 + 2 * i]);
                if (!isxdigit(c1) || !isxdigit(c2)) {
                    ALOGE("malformed cipher IV '%s'.", iv.c_str());
                    return ERROR_MALFORMED;
                }
                uint8_t nibble1 = isdigit(c1) ? c1 - '0' : c1 - 'a' + 10;
                uint8_t nibble2 = isdigit(c2) ? c2 - '0' : c2 - 'a' + 10;

                mAESInitVec[i] = nibble1 << 4 | nibble2;
            }
        } else {
            memset(mAESInitVec, 0, sizeof(mAESInitVec));
            mAESInitVec[15] = mSeqNumber & 0xff;
            mAESInitVec[14] = (mSeqNumber >> 8) & 0xff;
            mAESInitVec[13] = (mSeqNumber >> 16) & 0xff;
            mAESInitVec[12] = (mSeqNumber >> 24) & 0xff;
        }
    }

    AES_cbc_encrypt(
            buffer->data(), buffer->data(), buffer->size(),
            &aes_key, mAESInitVec, AES_DECRYPT);

    return OK;
}

status_t AmPlaylistFetcher::checkDecryptPadding(const sp<ABuffer> &buffer) {
    status_t err;
    AString method;
    CHECK(buffer->meta()->findString("cipher-method", &method));
    if (method == "NONE") {
        return OK;
    }

    uint8_t padding = 0;
    if (buffer->size() > 0) {
        padding = buffer->data()[buffer->size() - 1];
    }

    if (padding > 16) {
        return ERROR_MALFORMED;
    }

    for (size_t i = buffer->size() - padding; i < padding; i++) {
        if (buffer->data()[i] != padding) {
            return ERROR_MALFORMED;
        }
    }

    buffer->setRange(buffer->offset(), buffer->size() - padding);
    return OK;
}

void AmPlaylistFetcher::postMonitorQueue(int64_t delayUs, int64_t minDelayUs) {
    int64_t maxDelayUs = delayUsToRefreshPlaylist();
    if (maxDelayUs < minDelayUs) {
        maxDelayUs = minDelayUs;
    }
    if (delayUs > maxDelayUs) {
        ALOGV("Need to refresh playlist in %" PRId64 , maxDelayUs);
        delayUs = maxDelayUs;
    }
    sp<AMessage> msg = new AMessage(kWhatMonitorQueue, this);
    msg->setInt32("generation", mMonitorQueueGeneration);
    msg->post(delayUs);
}

void AmPlaylistFetcher::cancelMonitorQueue() {
    ++mMonitorQueueGeneration;
}

void AmPlaylistFetcher::startAsync(
        const sp<AmAnotherPacketSource> &audioSource,
        const sp<AmAnotherPacketSource> &videoSource,
        const sp<AmAnotherPacketSource> &subtitleSource,
        const sp<AmAnotherPacketSource> &metadataSource,
        int64_t startTimeUs,
        int64_t segmentStartTimeUs,
        int32_t startDiscontinuitySeq,
        bool adaptive) {
    sp<AMessage> msg = new AMessage(kWhatStart, this);

    uint32_t streamTypeMask = 0ul;

    if (audioSource != NULL) {
        msg->setPointer("audioSource", audioSource.get());
        streamTypeMask |= AmLiveSession::STREAMTYPE_AUDIO;
    }

    if (videoSource != NULL) {
        msg->setPointer("videoSource", videoSource.get());
        streamTypeMask |= AmLiveSession::STREAMTYPE_VIDEO;
    }

    if (subtitleSource != NULL) {
        msg->setPointer("subtitleSource", subtitleSource.get());
        streamTypeMask |= AmLiveSession::STREAMTYPE_SUBTITLES;
    }

    if (metadataSource != NULL) {
        msg->setPointer("metadataSource", metadataSource.get());
        // metadataSource does not affect streamTypeMask.
    }

    msg->setInt32("streamTypeMask", streamTypeMask);
    msg->setInt64("startTimeUs", startTimeUs);
    msg->setInt64("segmentStartTimeUs", segmentStartTimeUs);
    msg->setInt32("startDiscontinuitySeq", startDiscontinuitySeq);
    msg->setInt32("adaptive", adaptive);
    msg->post();
}

void AmPlaylistFetcher::pauseAsync() {
    (new AMessage(kWhatPause, this))->post();
}

void AmPlaylistFetcher::stopAsync(bool clear) {
    ALOGI("[%s:%d] start !", __FUNCTION__, __LINE__);
    sp<AMessage> msg = new AMessage(kWhatStop, this);
    msg->setInt32("clear", clear);
    msg->post();
}

void AmPlaylistFetcher::seekAsync(int64_t seekTimeUs) {
    ALOGI("[%s:%d] start !", __FUNCTION__, __LINE__);
    sp<AMessage> msg = new AMessage(kWhatSeek, this);
    msg->setInt64("seekTimeUs", seekTimeUs);
    msg->post();
}

void AmPlaylistFetcher::startAfterSeekAsync() {
    ALOGI("[%s:%d] start !", __FUNCTION__, __LINE__);
    postMonitorQueue();
}

void AmPlaylistFetcher::changeURI(AString uri) {
    mURI = uri;
}

uint32_t AmPlaylistFetcher::getStreamTypeMask() {
    return mStreamTypeMask;
}

void AmPlaylistFetcher::setStreamTypeMask(uint32_t streamMask) {
    mStreamTypeMask = streamMask;
}

void AmPlaylistFetcher::resumeUntilAsync(const sp<AMessage> &params) {
    AMessage* msg = new AMessage(kWhatResumeUntil, this);
    msg->setMessage("params", params);
    msg->post();
}

void AmPlaylistFetcher::onMessageReceived(const sp<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatCodecSpecificData:
        {
            sp<ABuffer> buffer;
            msg->findBuffer("buffer", &buffer);
            sp<AMessage> notify = mNotify->dup();
            notify->setInt32("what", kWhatCodecSpecificData);
            notify->setBuffer("buffer", buffer);
            notify->post();
            break;
        }
        case kWhatStart:
        {
            status_t err = onStart(msg);

            sp<AMessage> notify = mNotify->dup();
            notify->setInt32("what", kWhatStarted);
            notify->setInt32("err", err);
            notify->post();
            break;
        }

        case kWhatPause:
        {
            onPause();

            sp<AMessage> notify = mNotify->dup();
            notify->setInt32("what", kWhatPaused);
            notify->setString("uri", mURI.c_str());
            notify->post();
            break;
        }

        case kWhatStop:
        {
            onStop(msg);

            sp<AMessage> notify = mNotify->dup();
            notify->setInt32("what", kWhatStopped);
            notify->setString("uri", mURI.c_str());
            notify->setPointer("looper", looper().get());
            notify->post();
            break;
        }

        case kWhatSeek:
        {
            onSeek(msg);
            sp<AMessage> notify = mNotify->dup();
            notify->setInt32("what", kWhatSeeked);
            notify->setInt64("seekTimeUs", mStartTimeUs);
            notify->post();
            break;
        }

        case kWhatMonitorQueue:
        case kWhatDownloadNext:
        {
            int32_t generation;
            CHECK(msg->findInt32("generation", &generation));

            if (generation != mMonitorQueueGeneration) {
                // Stale event
                break;
            }

            if (msg->what() == kWhatMonitorQueue) {
                onMonitorQueue();
            } else {
                onDownloadNext();
            }
            break;
        }

        case kWhatResumeUntil:
        {
            onResumeUntil(msg);
            break;
        }

        default:
            TRESPASS();
    }
}

status_t AmPlaylistFetcher::onStart(const sp<AMessage> &msg) {
    mPacketSources.clear();
    mDownloadedNum = 0;
    uint32_t streamTypeMask;
    CHECK(msg->findInt32("streamTypeMask", (int32_t *)&streamTypeMask));

    int64_t startTimeUs;
    int64_t segmentStartTimeUs;
    int32_t startDiscontinuitySeq;
    int32_t adaptive;
    CHECK(msg->findInt64("startTimeUs", &startTimeUs));
    CHECK(msg->findInt64("segmentStartTimeUs", &segmentStartTimeUs));
    CHECK(msg->findInt32("startDiscontinuitySeq", &startDiscontinuitySeq));
    CHECK(msg->findInt32("adaptive", &adaptive));

    if (streamTypeMask & AmLiveSession::STREAMTYPE_AUDIO) {
        void *ptr;
        CHECK(msg->findPointer("audioSource", &ptr));

        mPacketSources.add(
                AmLiveSession::STREAMTYPE_AUDIO,
                static_cast<AmAnotherPacketSource *>(ptr));
    }

    if (streamTypeMask & AmLiveSession::STREAMTYPE_VIDEO) {
        void *ptr;
        CHECK(msg->findPointer("videoSource", &ptr));

        mPacketSources.add(
                AmLiveSession::STREAMTYPE_VIDEO,
                static_cast<AmAnotherPacketSource *>(ptr));
    }

    if (streamTypeMask & AmLiveSession::STREAMTYPE_SUBTITLES) {
        void *ptr;
        CHECK(msg->findPointer("subtitleSource", &ptr));

        mPacketSources.add(
                AmLiveSession::STREAMTYPE_SUBTITLES,
                static_cast<AmAnotherPacketSource *>(ptr));
    }

    void *ptr;
    // metadataSource is not part of streamTypeMask
    if ((streamTypeMask & (AmLiveSession::STREAMTYPE_AUDIO | AmLiveSession::STREAMTYPE_VIDEO))
            && msg->findPointer("metadataSource", &ptr)) {
        mPacketSources.add(
                AmLiveSession::STREAMTYPE_METADATA,
                static_cast<AmAnotherPacketSource *>(ptr));
    }

    mStreamTypeMask = streamTypeMask;

    mSegmentStartTimeUs = segmentStartTimeUs;
    mDiscontinuitySeq = startDiscontinuitySeq;

    if (startTimeUs >= 0) {
        mStartTimeUs = startTimeUs;
        mSeqNumber = -1;
        mStartup = true;
        mPrepared = false;
        mAdaptive = adaptive;
    }

    postMonitorQueue();

    return OK;
}

void AmPlaylistFetcher::onPause() {
    cancelMonitorQueue();
}

void AmPlaylistFetcher::onStop(const sp<AMessage> &msg) {
    ALOGI("[%s:%d] start !", __FUNCTION__, __LINE__);
    cancelMonitorQueue();

    int32_t clear;
    CHECK(msg->findInt32("clear", &clear));
    if (clear) {
        for (size_t i = 0; i < mPacketSources.size(); i++) {
            sp<AmAnotherPacketSource> packetSource = mPacketSources.valueAt(i);
            packetSource->clear();
        }
    }

    mPacketSources.clear();
    mStreamTypeMask = 0;
}

void AmPlaylistFetcher::onSeek(const sp<AMessage> &msg) {
    ALOGI("[%s:%d] start !", __FUNCTION__, __LINE__);
    cancelMonitorQueue();
    for (size_t i = 0; i < mPacketSources.size(); i++) {
        sp<AmAnotherPacketSource> packetSource = mPacketSources.valueAt(i);
        packetSource->clear();
    }
    if (mTSParser != NULL) {
        for (size_t i = 0; i < mPacketSources.size(); i++) {
            const AmLiveSession::StreamType stream = mPacketSources.keyAt(i);
            AmATSParser::SourceType type;
            switch (stream) {
            case AmLiveSession::STREAMTYPE_VIDEO:
                type = AmATSParser::VIDEO;
                break;
            case AmLiveSession::STREAMTYPE_AUDIO:
                type = AmATSParser::AUDIO;
                break;
            case AmLiveSession::STREAMTYPE_SUBTITLES:
                type = AmATSParser::NONE;
                break;
            case AmLiveSession::STREAMTYPE_METADATA:
                type = AmATSParser::META;
                break;
            default:
                TRESPASS();
            }
            // TODO: maybe need to fix for timed ID3.
            if (type > AmATSParser::NONE) {
                sp<AmAnotherPacketSource> source = static_cast<AmAnotherPacketSource *>(mTSParser->getSource(type).get());
                if (source != NULL) {
                    source->clear();
                }
            }
        }
        sp<AMessage> extra = new AMessage;
        extra->setInt64(IStreamListener::kKeyMediaTimeUs, 0);
        mTSParser->signalDiscontinuity(AmATSParser::DISCONTINUITY_TIME, extra);
    }
    msg->findInt64("seekTimeUs", &mStartTimeUs);
    mSeekedTimeUs = mStartTimeUs;
    mSeqNumber = -1;
    mSeeked = true;
}

// Resume until we have reached the boundary timestamps listed in `msg`; when
// the remaining time is too short (within a resume threshold) stop immediately
// instead.
status_t AmPlaylistFetcher::onResumeUntil(const sp<AMessage> &msg) {
    sp<AMessage> params;
    CHECK(msg->findMessage("params", &params));

    bool stop = false;
    for (size_t i = 0; i < mPacketSources.size(); i++) {
        sp<AmAnotherPacketSource> packetSource = mPacketSources.valueAt(i);

        const char *stopKey;
        int streamType = mPacketSources.keyAt(i);
        switch (streamType) {
        case AmLiveSession::STREAMTYPE_VIDEO:
            stopKey = "timeUsVideo";
            break;

        case AmLiveSession::STREAMTYPE_AUDIO:
            stopKey = "timeUsAudio";
            break;

        case AmLiveSession::STREAMTYPE_SUBTITLES:
            stopKey = "timeUsSubtitle";
            break;

        default:
            TRESPASS();
        }

        // Don't resume if we would stop within a resume threshold.
        int32_t discontinuitySeq;
        int64_t latestTimeUs = 0, stopTimeUs = 0;
        sp<AMessage> latestMeta = packetSource->getLatestEnqueuedMeta();
        if (latestMeta != NULL
                && latestMeta->findInt32("discontinuitySeq", &discontinuitySeq)
                && discontinuitySeq == mDiscontinuitySeq
                && latestMeta->findInt64("timeUs", &latestTimeUs)
                && params->findInt64(stopKey, &stopTimeUs)
                && stopTimeUs - latestTimeUs < resumeThreshold(latestMeta)) {
            stop = true;
        }
    }

    if (stop) {
        for (size_t i = 0; i < mPacketSources.size(); i++) {
            mPacketSources.valueAt(i)->queueAccessUnit(mSession->createFormatChangeBuffer());
        }
        stopAsync(/* clear = */ false);
        return OK;
    }

    mStopParams = params;
    postMonitorQueue();

    return OK;
}

void AmPlaylistFetcher::notifyError(status_t err) {
    sp<AMessage> notify = mNotify->dup();
    notify->setInt32("what", kWhatError);
    notify->setInt32("err", err);
    notify->post();
}

void AmPlaylistFetcher::queueDiscontinuity(
        AmATSParser::DiscontinuityType type, const sp<AMessage> &extra) {
    for (size_t i = 0; i < mPacketSources.size(); ++i) {
        // do not discard buffer upon #EXT-X-DISCONTINUITY tag
        // (seek will discard buffer by abandoning old fetchers)
        mPacketSources.valueAt(i)->queueDiscontinuity(
                type, extra, false /* discard */);
    }
}

void AmPlaylistFetcher::onMonitorQueue() {
    bool downloadMore = false;
    //refreshPlaylist();

    int32_t targetDurationSecs;
    int64_t targetDurationUs = kMinBufferedDurationUs;
    if (mPlaylist != NULL) {
        if (mPlaylist->meta() == NULL || !mPlaylist->meta()->findInt32(
                "target-duration", &targetDurationSecs)) {
            ALOGE("Playlist is missing required EXT-X-TARGETDURATION tag");
            notifyError(ERROR_MALFORMED);
            return;
        }
        targetDurationUs = targetDurationSecs * 1000000ll;
    }

    // buffer at least 3 times the target duration, or up to 10 seconds
    int64_t durationToBufferUs = targetDurationUs * 3;
    if (durationToBufferUs > kMinBufferedDurationUs)  {
        durationToBufferUs = kMinBufferedDurationUs;
    }

    int64_t bufferedDurationUs = 0ll;
    int64_t bufferedDataSize = 0ll;
    int64_t audioBufferedDurationUs = -1;
    int64_t videoBufferedDurationUs = -1;
    status_t finalResult = NOT_ENOUGH_DATA;
    if (mStreamTypeMask == AmLiveSession::STREAMTYPE_SUBTITLES) {
        sp<AmAnotherPacketSource> packetSource =
            mPacketSources.valueFor(AmLiveSession::STREAMTYPE_SUBTITLES);

        bufferedDurationUs =
                packetSource->getBufferedDurationUs(&finalResult);
        finalResult = OK;
    } else {
        // Use max stream duration to prevent us from waiting on a non-existent stream;
        // when we cannot make out from the manifest what streams are included in a playlist
        // we might assume extra streams.
        for (size_t i = 0; i < mPacketSources.size(); ++i) {
            if ((mStreamTypeMask & mPacketSources.keyAt(i)) == 0) {
                continue;
            }

            bufferedDataSize += mPacketSources.valueAt(i)->getBufferedDataSize();

            int64_t bufferedStreamDurationUs =
                mPacketSources.valueAt(i)->getBufferedDurationUs(&finalResult);
            ALOGV("buffered %" PRId64 " for stream %d",
                    bufferedStreamDurationUs, mPacketSources.keyAt(i));
            if (mPacketSources.keyAt(i) == AmLiveSession::STREAMTYPE_AUDIO) {
                audioBufferedDurationUs = bufferedStreamDurationUs;
            } else if (mPacketSources.keyAt(i) == AmLiveSession::STREAMTYPE_VIDEO) {
                videoBufferedDurationUs = bufferedStreamDurationUs;
            }
            if (bufferedStreamDurationUs > bufferedDurationUs) {
                bufferedDurationUs = bufferedStreamDurationUs;
            }
        }
    }

    downloadMore = (bufferedDurationUs < durationToBufferUs);

    if (finalResult == OK && (downloadMore || !mPostPrepared || mBuffering )) {
        ALOGV("monitoring, buffered=%" PRId64 " < %" PRId64 "",
                bufferedDurationUs, durationToBufferUs);
        // delay the next download slightly; hopefully this gives other concurrent fetchers
        // a better chance to run.
        // onDownloadNext();
        sp<AMessage> msg = new AMessage(kWhatDownloadNext, this);
        msg->setInt32("generation", mMonitorQueueGeneration);
        msg->post(1000l);
    } else {
        // Nothing to do yet, try again in a second.

        sp<AMessage> msg = mNotify->dup();
        msg->setInt32("what", kWhatTemporarilyDoneFetching);
        msg->setString("uri", mURI.c_str());
        msg->post();
        mFetchingNotify = true;
        ALOGV("buffered=%" PRId64 " > %" PRId64 "", bufferedDurationUs, durationToBufferUs);
        postMonitorQueue((bufferedDurationUs / 2) > kMinBufferedDurationUs
            ? (kMinBufferedDurationUs / 2) : (bufferedDurationUs / 2), 1000000ll); // maybe pts wrong.
    }
}

status_t AmPlaylistFetcher::refreshPlaylist() {
    bool needRefresh = false;
    if (mSession->mBandwidthItems.size() > 1 && mDownloadedNum > 1) {
        mSession->checkBandwidth(&needRefresh);
    }
    if (needRefresh || delayUsToRefreshPlaylist() <= 0) {
        if (mPlaylist != NULL && mFirstRefresh) {
            mFirstRefresh = false;
            goto END_OF_REFRESH;
        }
        mFirstRefresh = false;
        bool unchanged;
        status_t err = OK;
        CFContext * cfc_handle = NULL;
        sp<AmM3UParser> playlist = mSession->fetchPlaylist(
                mURI.c_str(), mPlaylistHash, &unchanged, err, &cfc_handle);
        int httpCode = 0;
        if (cfc_handle) {
            httpCode = -cfc_handle->http_code;
            curl_fetch_close(cfc_handle);
        }

        // need to retry
        if (err == ERROR_CANNOT_CONNECT) {
            if (!mFailureAnchorTimeUs) {
                mFailureAnchorTimeUs = ALooper::GetNowUs();
                return err;
            } else {
                if (ALooper::GetNowUs() - mFailureAnchorTimeUs >= mOpenFailureRetryUs) {
                    ALOGI("[%s:%d] open failure retry time exceed %lld us", __FUNCTION__, __LINE__, mOpenFailureRetryUs);
                    return httpCode;
                } else {
                    return err;
                }
            }
        } else {
            mFailureAnchorTimeUs = 0;
        }

        if (playlist == NULL) {
            if (unchanged) {
                // We succeeded in fetching the playlist, but it was
                // unchanged from the last time we tried.

                if (mRefreshState != THIRD_UNCHANGED_RELOAD_ATTEMPT) {
                    mRefreshState = (RefreshState)(mRefreshState + 1);
                }
            } else {
                ALOGE("failed to load playlist at url '%s'", uriDebugString(mURI).c_str());
                return ERROR_IO;
            }
        } else {
            mRefreshState = INITIAL_MINIMUM_RELOAD_DELAY;
            mPlaylist = playlist;
        }

        mLastPlaylistFetchTimeUs = ALooper::GetNowUs();
    }

END_OF_REFRESH:
    if (mPlaylist != NULL && (mPlaylist->isComplete() || mPlaylist->isEvent())) {
        updateDuration();
    }

    return OK;
}

// TODO: probe ts, maybe need to change.
// static
bool AmPlaylistFetcher::bufferStartsWithTsSyncByte(const sp<ABuffer>& buffer) {
    return buffer->size() > 0 && buffer->data()[0] == 0x47;
}

void AmPlaylistFetcher::onDownloadNext() {
    mDownloadedNum++;
    status_t err = refreshPlaylist();
    int32_t firstSeqNumberInPlaylist = 0;
    int32_t lastSeqNumberInPlaylist = 0;
    bool discontinuity = false;

    if (mPlaylist != NULL) {
        if (mPlaylist->meta() != NULL) {
            mPlaylist->meta()->findInt32("media-sequence", &firstSeqNumberInPlaylist);
        }

        lastSeqNumberInPlaylist =
                firstSeqNumberInPlaylist + (int32_t)mPlaylist->size() - 1;

        if (mDiscontinuitySeq < 0) {
            mDiscontinuitySeq = mPlaylist->getDiscontinuitySeq();
        }
    }

    if (mPlaylist != NULL && mSeqNumber < 0) {
        CHECK_GE(mStartTimeUs, 0ll);

        if (mSegmentStartTimeUs < 0) {
            if (!mPlaylist->isComplete() && !mPlaylist->isEvent()) {
                // If this is a live session, start 3 segments from the end on connect
                mSeqNumber = lastSeqNumberInPlaylist - 3;
                if (mSeqNumber < firstSeqNumberInPlaylist) {
                    mSeqNumber = firstSeqNumberInPlaylist;
                }
            } else {
                // When seeking mSegmentStartTimeUs is unavailable (< 0), we
                // use mStartTimeUs (client supplied timestamp) to determine both start segment
                // and relative position inside a segment
                mSeqNumber = getSeqNumberForTime(mStartTimeUs);
                mStartTimeUs -= getSegmentStartTimeUs(mSeqNumber);
            }
            mStartTimeUsRelative = true;
            ALOGV("Initial sequence number for time %" PRId64 " is %d from (%d .. %d)",
                    mStartTimeUs, mSeqNumber, firstSeqNumberInPlaylist,
                    lastSeqNumberInPlaylist);
        } else {
            // When adapting or track switching, mSegmentStartTimeUs (relative
            // to media time 0) is used to determine the start segment; mStartTimeUs (absolute
            // timestamps coming from the media container) is used to determine the position
            // inside a segments.
            mSeqNumber = getSeqNumberForTime(mSegmentStartTimeUs);
            if (mAdaptive) {
                // avoid double fetch/decode
                mSeqNumber += 1;
            }
            ssize_t minSeq = getSeqNumberForDiscontinuity(mDiscontinuitySeq);
            if (mSeqNumber < minSeq) {
                mSeqNumber = minSeq;
            }

            if (mSeqNumber < firstSeqNumberInPlaylist) {
                mSeqNumber = firstSeqNumberInPlaylist;
            }

            if (mSeqNumber > lastSeqNumberInPlaylist) {
                mSeqNumber = lastSeqNumberInPlaylist;
            }
            ALOGV("Initial sequence number for live event %d from (%d .. %d)",
                    mSeqNumber, firstSeqNumberInPlaylist,
                    lastSeqNumberInPlaylist);
        }
    }

    // if mPlaylist is NULL then err must be non-OK; but the other way around might not be true
    if (mSeqNumber < firstSeqNumberInPlaylist
            || mSeqNumber > lastSeqNumberInPlaylist
            || err != OK) {
        if (mRetryAnchorTimeUs < 0) {
            mRetryAnchorTimeUs = ALooper::GetNowUs();
        }
        if ((err != OK || !mPlaylist->isComplete())
            && (ALooper::GetNowUs() - mRetryAnchorTimeUs) / 1000000 <= mRetryTimeOverS) {
            ++mNumRetries;
	        if (err == ERROR_CANNOT_CONNECT) {
                mNumRetries = 0;
                mRetryAnchorTimeUs = ALooper::GetNowUs();
            }

            if (mSeqNumber > lastSeqNumberInPlaylist || err != OK) {
                // make sure we reach this retry logic on refresh failures
                // by adding an err != OK clause to all enclosing if's.

                // refresh in increasing fraction (1/2, 1/3, ...) of the
                // playlist's target duration or 3 seconds, whichever is less
                int64_t delayUs = kMaxMonitorDelayUs;
                if (mPlaylist != NULL && mPlaylist->meta() != NULL) {
                    int32_t targetDurationSecs;
                    CHECK(mPlaylist->meta()->findInt32("target-duration", &targetDurationSecs));
                    delayUs = mPlaylist->size() * targetDurationSecs *
                            1000000ll / (1 + mNumRetries);
                }
                if (delayUs > kMaxMonitorDelayUs) {
                    delayUs = kMaxMonitorDelayUs;
                }
                ALOGV("sequence number high: %d from (%d .. %d), "
                      "monitor in %" PRId64 ", nowUs (%" PRId64 "), anchorUs (%" PRId64 ")",
                        mSeqNumber, firstSeqNumberInPlaylist,
                        lastSeqNumberInPlaylist, delayUs,
                        ALooper::GetNowUs(), mRetryAnchorTimeUs);
                postMonitorQueue(delayUs, 100 * 1000);
                return;
            }

            if (err != OK) {
                notifyError(err);
                return;
            }

            // we've missed the boat, let's start 3 segments prior to the latest sequence
            // number available and signal a discontinuity.

            ALOGI("We've missed the boat, restarting playback."
                  "  mStartup=%d, was  looking for %d in %d-%d",
                    mStartup, mSeqNumber, firstSeqNumberInPlaylist,
                    lastSeqNumberInPlaylist);
            if (mStopParams != NULL) {
                // we should have kept on fetching until we hit the boundaries in mStopParams,
                // but since the segments we are supposed to fetch have already rolled off
                // the playlist, i.e. we have already missed the boat, we inevitably have to
                // skip.
                for (size_t i = 0; i < mPacketSources.size(); i++) {
                    sp<ABuffer> formatChange = mSession->createFormatChangeBuffer();
                    mPacketSources.valueAt(i)->queueAccessUnit(formatChange);
                }
                stopAsync(/* clear = */ false);
                return;
            }
            if (mSeqNumber < firstSeqNumberInPlaylist) {
                mSeqNumber = firstSeqNumberInPlaylist;
            }
            discontinuity = true;

            // fall through
        } else {
            ALOGE("Cannot find sequence number %d in playlist "
                 "(contains %d - %d)",
                 mSeqNumber, firstSeqNumberInPlaylist,
                 lastSeqNumberInPlaylist);

            notifyError(ERROR_END_OF_STREAM);
            return;
        }
    }

    mNumRetries = 0;
    mRetryAnchorTimeUs = -1ll;

    AString uri;
    sp<AMessage> itemMeta;
    CHECK(mPlaylist->itemAt(
                mSeqNumber - firstSeqNumberInPlaylist,
                &uri,
                &itemMeta));

#if 0
    int32_t val;
    if (itemMeta->findInt32("discontinuity", &val) && val != 0) {
        mDiscontinuitySeq++;
        discontinuity = true;
    }
#endif
    int64_t item_durationUs = 0;
    itemMeta->findInt64("durationUs", &item_durationUs);

    int64_t range_offset, range_length;
    if (!itemMeta->findInt64("range-offset", &range_offset)
            || !itemMeta->findInt64("range-length", &range_length)) {
        range_offset = 0;
        range_length = -1;
    }

    ALOGV("fetching segment %d from (%d .. %d)",
          mSeqNumber, firstSeqNumberInPlaylist, lastSeqNumberInPlaylist);

    ALOGV("fetching '%s'", uri.c_str());

    sp<DataSource> source;
    sp<ABuffer> buffer, tsBuffer;
    // decrypt a junk buffer to prefetch key; since a session uses only one http connection,
    // this avoids interleaved connections to the key and segment file.
    {
        sp<ABuffer> junk = new ABuffer(16);
        junk->setRange(0, 16);
        status_t err = decryptBuffer(mSeqNumber - firstSeqNumberInPlaylist, junk,
                true /* first */);
        if (err != OK) {
            notifyError(err);
            return;
        }
    }

    // block-wise download
    bool startup = mStartup;
    ssize_t bytesRead, total_size = 0;
    CFContext * cfc_handle = NULL;

    FILE * dumpHandle = NULL;
    if (mDumpMode == 1 && !mDumpHandle) {
        AString dumppath = DumpPath;
        if ((mStreamTypeMask & AmLiveSession::STREAMTYPE_AUDIO)
            && (mStreamTypeMask & AmLiveSession::STREAMTYPE_VIDEO)) {
            dumppath.append("nuplayer_hls_dump.dat");
        } else if (mStreamTypeMask & AmLiveSession::STREAMTYPE_AUDIO) {
            dumppath.append("nuplayer_hls_audio_dump.dat");
        } else if (mStreamTypeMask & AmLiveSession::STREAMTYPE_VIDEO) {
            dumppath.append("nuplayer_hls_video_dump.dat");
        } else {
            dumppath.append("nuplayer_hls_subtitle_dump.dat");
        }
        mDumpHandle = fopen(dumppath.c_str(), "ab+");
    }
    if (mDumpMode == 2) {
        char dumpFile[256] = {'\0'};
        if ((mStreamTypeMask & AmLiveSession::STREAMTYPE_AUDIO)
            && (mStreamTypeMask & AmLiveSession::STREAMTYPE_VIDEO)) {
            snprintf(dumpFile, sizeof(dumpFile), "%sdump_%d.ts", DumpPath.c_str(), mSeqNumber);
        } else if (mStreamTypeMask & AmLiveSession::STREAMTYPE_AUDIO) {
            snprintf(dumpFile, sizeof(dumpFile), "%sdump_audio_%d.ts", DumpPath.c_str(), mSeqNumber);
        } else if (mStreamTypeMask & AmLiveSession::STREAMTYPE_VIDEO) {
            snprintf(dumpFile, sizeof(dumpFile), "%sdump_video_%d.ts", DumpPath.c_str(), mSeqNumber);
        } else {
            snprintf(dumpFile, sizeof(dumpFile), "%sdump_subtitle_%d.ts", DumpPath.c_str(), mSeqNumber);
        }
        dumpHandle = fopen(dumpFile, "ab+");
    }

    // seek by byte, maybe need to modify.
    // just ts.
    if (mSeeked && mIsTs && mStartTimeUs > 0 && (mStreamTypeMask != AmLiveSession::STREAMTYPE_SUBTITLES)) {
        int64_t length = 0;
        CFContext * header_handle = curl_fetch_init(uri.c_str(),
            "User-Agent: Mozilla/5.0 (Linux; Android 5.1.1) AppleWebKit/537.36 (KHTML, like Gecko) Version/4.0 Chrome/39.0.0.0 Safari/537.36\r\n",
            C_FLAGS_NO_BODY);
        if (!header_handle) {
            ALOGE("open failed!");
            goto FETCH;
        }
        curl_fetch_register_interrupt_pid(header_handle, mSession->mInterruptCallback);
        curl_fetch_set_parent_pid(header_handle, mSession->mParentThreadId);
        if (curl_fetch_open(header_handle)) {
            ALOGE("fetch open failed!");
            curl_fetch_close(header_handle);
            header_handle = NULL;
            goto FETCH;
        }
        length = header_handle->filesize;
        curl_fetch_close(header_handle);
        header_handle = NULL;

        if (length > 0) {
            range_offset = (int64_t)(length * (mStartTimeUs / (double)(item_durationUs)));
            ALOGI("seek to seq num(%d), offset(%lld), length(%lld)", mSeqNumber, range_offset, length);
        } else {
            mSeekedTimeUs = getSegmentStartTimeUs(mSeqNumber);
            ALOGI("not support range, cannot seek by byte!");
        }
    }

FETCH:

    do {
        bytesRead = mSession->fetchFile(
                uri.c_str(), &buffer, range_offset, range_length, kDownloadBlockSize, &cfc_handle);

        if (bytesRead > 0 && mDumpMode > 0) {
            if (mDumpMode == 1 && mDumpHandle) {
                fwrite(buffer->data() + (buffer->size() - bytesRead), 1, bytesRead, mDumpHandle);
                fflush(mDumpHandle);
            } else if (mDumpMode == 2 && dumpHandle) {
                fwrite(buffer->data() + (buffer->size() - bytesRead), 1, bytesRead, dumpHandle);
                fflush(dumpHandle);
            }
        }

        if (bytesRead > 0) {
            total_size += bytesRead;
            if (mSeeked && mIsTs && mStartTimeUs > 0 && (mStreamTypeMask != AmLiveSession::STREAMTYPE_SUBTITLES)) {
                if (cfc_handle->http_code != 206) {
                    mSeekedTimeUs = getSegmentStartTimeUs(mSeqNumber);
                    ALOGI("HTTP response (%d), cannot seek by byte!", cfc_handle->http_code);
                }
            }
        }

        mSeeked = false;

        // need to retry
        if (bytesRead == ERROR_CANNOT_CONNECT) {
            if (!mFailureAnchorTimeUs) {
                mFailureAnchorTimeUs = ALooper::GetNowUs();
            } else {
                if (ALooper::GetNowUs() - mFailureAnchorTimeUs >= mOpenFailureRetryUs) {
                    ALOGI("[%s:%d] open failure retry time exceed %lld us", __FUNCTION__, __LINE__, mOpenFailureRetryUs);
                    status_t err = bytesRead;
                    notifyError(-cfc_handle->http_code);
                    goto FAIL;
                }
            }
            postMonitorQueue(100 * 1000, 100 * 1000);
            goto FAIL;
        } else {
            mFailureAnchorTimeUs = 0;
        }

        // skip to next segment.
        if (bytesRead == -ENETRESET) {
            ++mSeqNumber;
            ALOGE("fetch file met error! skip to next segment : %d", mSeqNumber);
            queueDiscontinuity(AmATSParser::DISCONTINUITY_DATA_CORRUPTION, NULL);
            postMonitorQueue();
            goto FAIL;
        }

        if (bytesRead < 0) {
            status_t err = bytesRead;
            ALOGE("failed to fetch .ts segment at url '%s'", uri.c_str());
            notifyError(err);
            goto FAIL;
        }

        //CHECK(buffer != NULL);
        if (buffer == NULL) {  // maybe interrupt play
            status_t err = bytesRead;
            notifyError(err);
            postMonitorQueue();
            goto FAIL;
        }

        size_t size = buffer->size();
        // Set decryption range.
        buffer->setRange(size - bytesRead, bytesRead);
        status_t err = decryptBuffer(mSeqNumber - firstSeqNumberInPlaylist, buffer,
                buffer->offset() == 0 /* first */);
        // Unset decryption range.
        buffer->setRange(0, size);

        // need to retry
        if (err == ERROR_CANNOT_CONNECT) {
            if (!mFailureAnchorTimeUs) {
                mFailureAnchorTimeUs = ALooper::GetNowUs();
            } else {
                if (ALooper::GetNowUs() - mFailureAnchorTimeUs >= mOpenFailureRetryUs) {
                    ALOGI("[%s:%d] open failure retry time exceed %lld us", __FUNCTION__, __LINE__, mOpenFailureRetryUs);
                    notifyError(err);
                    goto FAIL;
                }
            }
            postMonitorQueue(100 * 1000, 100 * 1000);
            goto FAIL;
        } else {
            mFailureAnchorTimeUs = 0;
        }

        if (err != OK) {
            ALOGE("decryptBuffer failed w/ error %d", err);

            notifyError(err);
            goto FAIL;
        }

#if 0
        if (startup || discontinuity) {
            // Signal discontinuity.

            if (mPlaylist->isComplete() || mPlaylist->isEvent()) {
                // If this was a live event this made no sense since
                // we don't have access to all the segment before the current
                // one.
                mNextPTSTimeUs = getSegmentStartTimeUs(mSeqNumber);
                ALOGI("segment start time : %lld us on %s", mNextPTSTimeUs, startup ? "startup" : "discontinuity");
            }

            // do not handle time discontinuity here.
            // maybe need to handle other type discontinuity.
            if (discontinuity) {
                ALOGI("queueing discontinuity (explicit=%d)", discontinuity);

                // do not queue discontinuity, prevent flushing decoder.
                queueDiscontinuity(
                        AmATSParser::DISCONTINUITY_TIME,
                        NULL /* extra */);

                discontinuity = false;
            }

            startup = false;
        }
#endif

        if (mNeedSniff) { // just probe in first buffer.
            mIsTs = bufferStartsWithTsSyncByte(buffer);
            if (!mIsTs && !bufferStartsWithWebVTTMagicSequence(buffer)) {
                sniff(buffer);
            }
        }
        mNeedSniff = false;

        err = OK;
        if (mIsTs) {
            // Incremental extraction is only supported for MPEG2 transport streams.
            if (tsBuffer == NULL) {
                tsBuffer = new ABuffer(buffer->data(), buffer->capacity());
                tsBuffer->setRange(0, 0);
            } else if (tsBuffer->capacity() != buffer->capacity()) {
                size_t tsOff = tsBuffer->offset(), tsSize = tsBuffer->size();
                tsBuffer = new ABuffer(buffer->data(), buffer->capacity());
                tsBuffer->setRange(tsOff, tsSize);
            }
            tsBuffer->setRange(tsBuffer->offset(), tsBuffer->size() + bytesRead);

            err = extractAndQueueAccessUnitsFromTs(tsBuffer);
        } else if (mExtractor != NULL) {
            err = extractAndQueueAccessUnitsFromNonTs();
        }

        if (err == -EAGAIN) {
            // starting sequence number too low/high
            if (mTSParser != NULL) {
                mTSParser.clear();
            } else if (mExtractor != NULL) {
                mExtractor.clear();
            }
            for (size_t i = 0; i < mPacketSources.size(); i++) {
                sp<AmAnotherPacketSource> packetSource = mPacketSources.valueAt(i);
                packetSource->clear();
            }
            postMonitorQueue();
            goto FAIL;
        } else if (err == ERROR_MALFORMED) {
            // try to reset ts parser.
            if (mTSParser != NULL) {
                mTSParser.clear();
            } else if (mExtractor != NULL) {
                mExtractor.clear();
            }
            for (size_t i = 0; i < mPacketSources.size(); i++) {
                sp<AmAnotherPacketSource> packetSource = mPacketSources.valueAt(i);
                packetSource->clear();
            }
            ++mSeqNumber;
            ALOGE("ts parser met error, need to reset!");
            queueDiscontinuity(AmATSParser::DISCONTINUITY_DATA_CORRUPTION, NULL);
            postMonitorQueue();
            goto FAIL;
        } else if (err == ERROR_OUT_OF_RANGE) {
            // reached stopping point
            stopAsync(/* clear = */ false);
            goto FAIL;
        } else if (err != OK) {
            notifyError(err);
            ALOGE("MPEG2TS extractor notify error : %d !\n", err);
            goto FAIL;
        }

    } while (bytesRead != 0);

    if (dumpHandle) {
        fclose(dumpHandle);
    }
    if (cfc_handle) {
        curl_fetch_close(cfc_handle);
    }

    if (total_size && item_durationUs) {
        mSegmentBytesPerSec = total_size / (float)(item_durationUs / 1E6); // just an approximate value.
        ALOGI("segment duration : %lld us, size : %d bytes, bytes per second : %lld", item_durationUs, total_size, mSegmentBytesPerSec);
    }

    if (mPlaylist->isComplete() && mSeqNumber == lastSeqNumberInPlaylist && !total_size) {
        ALOGE("Last segment is empty, need to notify EOS!");
        notifyError(ERROR_END_OF_STREAM);
        return;
    }

    if (mFirstTypeProbe) { // just once
        if (mIsTs) {
            // If we don't see a stream in the program table after fetching a full ts segment
            // mark it as nonexistent.
            AmATSParser::SourceType srcTypes[] =
                { AmATSParser::VIDEO, AmATSParser::AUDIO };
            AmLiveSession::StreamType streamTypes[] =
                { AmLiveSession::STREAMTYPE_VIDEO, AmLiveSession::STREAMTYPE_AUDIO };
            const size_t kNumTypes = NELEM(srcTypes);

            for (size_t i = 0; i < kNumTypes; i++) {
                AmATSParser::SourceType srcType = srcTypes[i];
                AmLiveSession::StreamType streamType = streamTypes[i];

                sp<AmAnotherPacketSource> source =
                    static_cast<AmAnotherPacketSource *>(
                        mTSParser->getSource(srcType).get());

                if (!mTSParser->hasSource(srcType)) {
                    ALOGW("MPEG2 Transport stream does not contain %s data.",
                          srcType == AmATSParser::VIDEO ? "video" : "audio");

                    mStreamTypeMask &= ~streamType;
                    //mPacketSources.removeItem(streamType);
                    if (mPacketSources.indexOfKey(streamType) >= 0) {
                        (mPacketSources.valueFor(streamType))->setValid(false);
                    }
                }
            }

        } else if (mExtractor != NULL) {
            // If we don't see a stream after fetching a full segment
            // mark it as nonexistent.
            if (mVideoSource == NULL) {
                mStreamTypeMask &= ~AmLiveSession::STREAMTYPE_VIDEO;
                mPacketSources.removeItem(AmLiveSession::STREAMTYPE_VIDEO);
            }
            if (mAudioSource == NULL) {
                mStreamTypeMask &= ~AmLiveSession::STREAMTYPE_AUDIO;
                mPacketSources.removeItem(AmLiveSession::STREAMTYPE_AUDIO);
            }
        }
        mFirstTypeProbe = false;
    }

    if (checkDecryptPadding(buffer) != OK) {
        ALOGE("Incorrect padding bytes after decryption.");
        notifyError(ERROR_MALFORMED);
        return;
    }

    err = OK;
    if (tsBuffer != NULL) {
        AString method;
        CHECK(buffer->meta()->findString("cipher-method", &method));
        if (((tsBuffer->size() > 0 && method == "NONE")
                || tsBuffer->size() > 16) && mExtractor == NULL) {
            ALOGE("MPEG2 transport stream is not an even multiple of 188 "
                    "bytes in length.");
        }
    }

    // bulk extract non-ts files
    if (tsBuffer == NULL && mExtractor == NULL) {
        err = extractAndQueueAccessUnits(buffer, itemMeta);
        if (err == -EAGAIN) {
            // starting sequence number too low/high
            postMonitorQueue();
            return;
        } else if (err == ERROR_OUT_OF_RANGE) {
            // reached stopping point
            stopAsync(/* clear = */false);
            return;
        }
    }

    if (err != OK) {
        notifyError(err);
        return;
    }

    ++mSeqNumber;

    postMonitorQueue();

    if (mExtractor != NULL) {
        mExtractor.clear();
        mExtractor = NULL;
    }

    return;

FAIL:
    if (dumpHandle) {
        fclose(dumpHandle);
    }
    if (cfc_handle) {
        curl_fetch_close(cfc_handle);
    }
}

int32_t AmPlaylistFetcher::getSeqNumberWithAnchorTime(int64_t anchorTimeUs) const {
    int32_t firstSeqNumberInPlaylist, lastSeqNumberInPlaylist;
    if (mPlaylist->meta() == NULL
            || !mPlaylist->meta()->findInt32("media-sequence", &firstSeqNumberInPlaylist)) {
        firstSeqNumberInPlaylist = 0;
    }
    lastSeqNumberInPlaylist = firstSeqNumberInPlaylist + mPlaylist->size() - 1;

    int32_t index = mSeqNumber - firstSeqNumberInPlaylist - 1;
    while (index >= 0 && anchorTimeUs > mStartTimeUs) {
        sp<AMessage> itemMeta;
        CHECK(mPlaylist->itemAt(index, NULL /* uri */, &itemMeta));

        int64_t itemDurationUs;
        CHECK(itemMeta->findInt64("durationUs", &itemDurationUs));

        anchorTimeUs -= itemDurationUs;
        --index;
    }

    int32_t newSeqNumber = firstSeqNumberInPlaylist + index + 1;
    if (newSeqNumber <= lastSeqNumberInPlaylist) {
        return newSeqNumber;
    } else {
        return lastSeqNumberInPlaylist;
    }
}

int32_t AmPlaylistFetcher::getSeqNumberForDiscontinuity(size_t discontinuitySeq) const {
    int32_t firstSeqNumberInPlaylist;
    if (mPlaylist->meta() == NULL
            || !mPlaylist->meta()->findInt32("media-sequence", &firstSeqNumberInPlaylist)) {
        firstSeqNumberInPlaylist = 0;
    }

    size_t curDiscontinuitySeq = mPlaylist->getDiscontinuitySeq();
    if (discontinuitySeq < curDiscontinuitySeq) {
        return firstSeqNumberInPlaylist <= 0 ? 0 : (firstSeqNumberInPlaylist - 1);
    }

    size_t index = 0;
    while (index < mPlaylist->size()) {
        sp<AMessage> itemMeta;
        CHECK(mPlaylist->itemAt( index, NULL /* uri */, &itemMeta));

        int64_t discontinuity;
        if (itemMeta->findInt64("discontinuity", &discontinuity)) {
            curDiscontinuitySeq++;
        }

        if (curDiscontinuitySeq == discontinuitySeq) {
            return firstSeqNumberInPlaylist + index;
        }

        ++index;
    }

    return firstSeqNumberInPlaylist + mPlaylist->size();
}

int32_t AmPlaylistFetcher::getSeqNumberForTime(int64_t timeUs) const {
    int32_t firstSeqNumberInPlaylist;
    if (mPlaylist->meta() == NULL || !mPlaylist->meta()->findInt32(
                "media-sequence", &firstSeqNumberInPlaylist)) {
        firstSeqNumberInPlaylist = 0;
    }

    size_t index = 0;
    int64_t segmentStartUs = 0;
    while (index < mPlaylist->size()) {
        sp<AMessage> itemMeta;
        CHECK(mPlaylist->itemAt(
                    index, NULL /* uri */, &itemMeta));

        int64_t itemDurationUs;
        CHECK(itemMeta->findInt64("durationUs", &itemDurationUs));

        if (timeUs < segmentStartUs + itemDurationUs) {
            break;
        }

        segmentStartUs += itemDurationUs;
        ++index;
    }

    if (index >= mPlaylist->size()) {
        index = mPlaylist->size() - 1;
    }

    return firstSeqNumberInPlaylist + index;
}

const sp<ABuffer> &AmPlaylistFetcher::setAccessUnitProperties(
        const sp<ABuffer> &accessUnit, const sp<AmAnotherPacketSource> &source, bool discard) {
    sp<MetaData> format = source->getFormat();
    if (format != NULL) {
        // for simplicity, store a reference to the format in each unit
        accessUnit->meta()->setObject("format", format);
    }

    if (discard) {
        accessUnit->meta()->setInt32("discard", discard);
    }

    int32_t targetDurationSecs;
    if (mPlaylist->meta()->findInt32("target-duration", &targetDurationSecs)) {
        accessUnit->meta()->setInt32("targetDuration", targetDurationSecs);
    }

    accessUnit->meta()->setInt32("discontinuitySeq", mDiscontinuitySeq);
    accessUnit->meta()->setInt64("segmentStartTimeUs", getSegmentStartTimeUs(mSeqNumber));
    return accessUnit;
}

static status_t int64cmp(const int64_t *a, const int64_t *b )
{
    return (status_t)(*a - *b);
}

size_t AmPlaylistFetcher::resyncTs(const uint8_t * data, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        if (size - offset == 188 && data[offset] == 0x47) {
            ALOGI("[%s:%d] found ts sync byte!", __FUNCTION__, __LINE__);
            return offset;
        } else if (size - offset > 188 && data[offset] == 0x47 && data[offset + 188] == 0x47) {
            ALOGI("[%s:%d] found ts sync byte!", __FUNCTION__, __LINE__);
            return offset;
        }
        offset++;
    }
    return offset;
}

status_t AmPlaylistFetcher::extractAndQueueAccessUnitsFromTs(const sp<ABuffer> &buffer) {
    if (mTSParser == NULL) {
        // Use TS_TIMESTAMPS_ARE_ABSOLUTE so pts carry over between fetchers.
        mTSParser = new AmATSParser(AmATSParser::TS_TIMESTAMPS_ARE_ABSOLUTE);
    }

    if (mNextPTSTimeUs >= 0ll) {
        sp<AMessage> extra = new AMessage;
        // Since we are using absolute timestamps, signal an offset of 0 to prevent
        // AmATSParser from skewing the timestamps of access units.
        extra->setInt64(IStreamListener::kKeyMediaTimeUs, 0);

        mTSParser->signalDiscontinuity(
                AmATSParser::DISCONTINUITY_TIME, extra);

        mAbsoluteTimeAnchorUs = mNextPTSTimeUs;
        mNextPTSTimeUs = -1ll;
        mFirstPTSValid = false;
    }

    size_t offset = 0;
    while (offset + 188 <= buffer->size()) {
        status_t err = mTSParser->feedTSPacket(buffer->data() + offset, 188);

        if (err == BAD_VALUE) {
            size_t sync_offset = resyncTs(buffer->data() + offset, buffer->size() - offset);
            offset += sync_offset;
            continue;
        } else if (err != OK) {
            return err;
        }

        offset += 188;
    }
    // setRange to indicate consumed bytes.
    buffer->setRange(buffer->offset() + offset, buffer->size() - offset);

    return queueAccessUnits();
}

status_t AmPlaylistFetcher::extractAndQueueAccessUnitsFromNonTs() {
    if (mNextPTSTimeUs >= 0ll) {
        sp<AMessage> extra = new AMessage;
        // Since we are using absolute timestamps, signal an offset of 0 to prevent
        // ATSParser from skewing the timestamps of access units.
        extra->setInt64(IStreamListener::kKeyMediaTimeUs, 0);

        mAbsoluteTimeAnchorUs = mNextPTSTimeUs;
        mNextPTSTimeUs = -1ll;
        mFirstPTSValid = false;
    }

    readFromNonTsFile();

    return queueAccessUnits();
}

status_t AmPlaylistFetcher::queueAccessUnits() {

    status_t err = OK;
    size_t source_count = 0;
    for (size_t i = mPacketSources.size(); i-- > 0;) {
        sp<AmAnotherPacketSource> packetSource = mPacketSources.valueAt(i);

        const char *key;
        AmATSParser::SourceType type;
        const AmLiveSession::StreamType stream = mPacketSources.keyAt(i);
        sp<AmAnotherPacketSource> source;
        switch (stream) {
            case AmLiveSession::STREAMTYPE_VIDEO:
                type = AmATSParser::VIDEO;
                key = "timeUsVideo";
                source = mVideoSource;
                break;

            case AmLiveSession::STREAMTYPE_AUDIO:
                type = AmATSParser::AUDIO;
                key = "timeUsAudio";
                source = mAudioSource;
                break;

            case AmLiveSession::STREAMTYPE_METADATA:
                type = AmATSParser::META;
                key = "timeUsMetadata";
                break;

            case AmLiveSession::STREAMTYPE_SUBTITLES:
            {
                ALOGE("MPEG2 Transport streams do not contain subtitles.");
                return ERROR_MALFORMED;
                break;
            }

            default:
                TRESPASS();
        }

        if (mExtractor == NULL) {
            source = static_cast<AmAnotherPacketSource *>(mTSParser->getSource(type).get());
            if (stream == AmLiveSession::STREAMTYPE_METADATA && mTSParser->hasSource(type) && !mHasMetadata) {
                ALOGI("[timed_id3] metadata detected!");
                mHasMetadata = true;
                sp<AMessage> notify = mNotify->dup();
                notify->setInt32("what", kWhatMetadataDetected);
                notify->post();
            }
        }

        if (stream == AmLiveSession::STREAMTYPE_METADATA) {
            source_count++;
        }

        if (source == NULL) {
            continue;
        }

        if (packetSource->getFormat() == NULL && source->getFormat() != NULL) {
            packetSource->setFormat(source->getFormat());
        }

        if (source->getFormat() != NULL && stream != AmLiveSession::STREAMTYPE_METADATA) {
            source_count++;
        }

        int64_t timeUs;
        sp<ABuffer> accessUnit;
        status_t finalResult;
        while (source->hasBufferAvailable(&finalResult)
                && source->dequeueAccessUnit(&accessUnit) == OK) {

            CHECK(accessUnit->meta()->findInt64("timeUs", &timeUs));

            if (mEnableFrameRate && mSession->getFrameRate() < 0.0 && type == AmATSParser::VIDEO && mVecTimeUs.size() < kFrameNum) {
                mVecTimeUs.push(timeUs);
            }

            if (mStartup) {
                if (!mFirstPTSValid) {
                    mFirstTimeUs = timeUs;
                    mFirstPTSValid = true;
                }
                if (mStartTimeUsRelative) {
                    timeUs -= mFirstTimeUs;
                    if (timeUs < 0) {
                        timeUs = 0;
                    }
                }

#if 0
                if (timeUs < mStartTimeUs) {
                    // buffer up to the closest preceding IDR frame
                    ALOGV("timeUs %" PRId64 " us < mStartTimeUs %" PRId64 " us",
                            timeUs, mStartTimeUs);
                    const char *mime;
                    sp<MetaData> format  = source->getFormat();
                    bool isAvc = false;
                    if (format != NULL && format->findCString(kKeyMIMEType, &mime)
                            && !strcasecmp(mime, MEDIA_MIMETYPE_VIDEO_AVC)) {
                        isAvc = true;
                    }
                    if (isAvc && IsIDR(accessUnit)) {
                        mVideoBuffer->clear();
                    }
                    if (isAvc) {
                        mVideoBuffer->queueAccessUnit(accessUnit);
                    }

                    continue;
                }
#endif
            }

            CHECK(accessUnit->meta()->findInt64("timeUs", &timeUs));
            if (mStartTimeUsNotify != NULL && timeUs > mStartTimeUs) {
                int32_t firstSeqNumberInPlaylist;
                if (mPlaylist->meta() == NULL || !mPlaylist->meta()->findInt32(
                            "media-sequence", &firstSeqNumberInPlaylist)) {
                    firstSeqNumberInPlaylist = 0;
                }

                int32_t targetDurationSecs;
                CHECK(mPlaylist->meta()->findInt32("target-duration", &targetDurationSecs));
                int64_t targetDurationUs = targetDurationSecs * 1000000ll;
                // mStartup
                //   mStartup is true until we have queued a packet for all the streams
                //   we are fetching. We queue packets whose timestamps are greater than
                //   mStartTimeUs.
                // mSegmentStartTimeUs >= 0
                //   mSegmentStartTimeUs is non-negative when adapting or switching tracks
                // mSeqNumber > firstSeqNumberInPlaylist
                //   don't decrement mSeqNumber if it already points to the 1st segment
                // timeUs - mStartTimeUs > targetDurationUs:
                //   This and the 2 above conditions should only happen when adapting in a live
                //   stream; the old fetcher has already fetched to mStartTimeUs; the new fetcher
                //   would start fetching after timeUs, which should be greater than mStartTimeUs;
                //   the old fetcher would then continue fetching data until timeUs. We don't want
                //   timeUs to be too far ahead of mStartTimeUs because we want the old fetcher to
                //   stop as early as possible. The definition of being "too far ahead" is
                //   arbitrary; here we use targetDurationUs as threshold.
                if (mStartup && mSegmentStartTimeUs >= 0
                        && mSeqNumber > firstSeqNumberInPlaylist
                        && timeUs - mStartTimeUs > targetDurationUs) {
                    // we just guessed a starting timestamp that is too high when adapting in a
                    // live stream; re-adjust based on the actual timestamp extracted from the
                    // media segment; if we didn't move backward after the re-adjustment
                    // (newSeqNumber), start at least 1 segment prior.
                    int32_t newSeqNumber = getSeqNumberWithAnchorTime(timeUs);
                    if (newSeqNumber >= mSeqNumber) {
                        --mSeqNumber;
                    } else {
                        mSeqNumber = newSeqNumber;
                    }
                    mStartTimeUsNotify = mNotify->dup();
                    mStartTimeUsNotify->setInt32("what", kWhatStartedAt);
                    return -EAGAIN;
                }

                int32_t seq;
                if (!mStartTimeUsNotify->findInt32("discontinuitySeq", &seq)) {
                    mStartTimeUsNotify->setInt32("discontinuitySeq", mDiscontinuitySeq);
                }
                int64_t startTimeUs;
                if (!mStartTimeUsNotify->findInt64(key, &startTimeUs)) {
                    mStartTimeUsNotify->setInt64(key, timeUs);

                    uint32_t streamMask = 0;
                    mStartTimeUsNotify->findInt32("streamMask", (int32_t *) &streamMask);
                    streamMask |= mPacketSources.keyAt(i);
                    mStartTimeUsNotify->setInt32("streamMask", streamMask);

                    if (streamMask == mStreamTypeMask) {
                        mStartup = false;
                        mStartTimeUsNotify->post();
                        mStartTimeUsNotify.clear();
                    }
                }
            }

            if (mStopParams != NULL) {
                // Queue discontinuity in original stream.
                int32_t discontinuitySeq;
                int64_t stopTimeUs;
                if (!mStopParams->findInt32("discontinuitySeq", &discontinuitySeq)
                        || discontinuitySeq > mDiscontinuitySeq
                        || !mStopParams->findInt64(key, &stopTimeUs)
                        || (discontinuitySeq == mDiscontinuitySeq
                                && timeUs >= stopTimeUs)) {
                    packetSource->queueAccessUnit(mSession->createFormatChangeBuffer());
                    mStreamTypeMask &= ~stream;
                    mPacketSources.removeItemsAt(i);
                    break;
                }
            }

            // Note that we do NOT dequeue any discontinuities except for format change.
            if (stream == AmLiveSession::STREAMTYPE_VIDEO) {
                const bool discard = true;
                status_t status;
                while (mVideoBuffer->hasBufferAvailable(&status)) {
                    sp<ABuffer> videoBuffer;
                    mVideoBuffer->dequeueAccessUnit(&videoBuffer);
                    setAccessUnitProperties(videoBuffer, source, discard);
                    packetSource->queueAccessUnit(videoBuffer);
                }
            }

            // send AmLiveSession the CodecSpecificData
            if (source->getFormat() != NULL) {
                const char *mime;
                source->getFormat()->findCString(kKeyMIMEType, &mime);
                if (type == AmATSParser::VIDEO && !strcasecmp(MEDIA_MIMETYPE_VIDEO_HEVC, mime)) {
                    int key = 0;
                    accessUnit->meta()->findInt32("iskeyframe", &key);
                    if (key == 1) {
                        sp<AMessage> msg = new AMessage(kWhatCodecSpecificData, this);
                        msg->setBuffer("buffer", accessUnit);
                        msg->post();
                    }
                }
            }

            setAccessUnitProperties(accessUnit, source);
            packetSource->queueAccessUnit(accessUnit);
        }


        if (mEnableFrameRate && mSession->getFrameRate() < 0.0 && type == AmATSParser::VIDEO && mVecTimeUs.size() >= kFrameNum) {
            mVecTimeUs.sort(int64cmp);
            int64_t durations = 0;
            size_t size = mVecTimeUs.size() / 2;

            for (size_t n = 1; n <= size; n++) {
                durations += (mVecTimeUs[n] - mVecTimeUs[n-1]);
            }

            mSession->setFrameRate(1000000.0 * size / durations);
        }

        if (err != OK) {
            break;
        }
    }

    if (!mPostPrepared) {
        size_t valid_source_count = 0;
        for (size_t i = 0; i < mPacketSources.size(); i++) {
            if (mPacketSources.valueAt(i)->getValid()) {
                valid_source_count++;
            }
        }
        if (source_count == valid_source_count) {
            ALOGI("packet source prepared!\n");
            mPostPrepared = true;
            sp<AMessage> msg = mNotify->dup();
            msg->setInt32("what", kWhatTemporarilyDoneFetching);
            msg->setString("uri", mURI.c_str());
            msg->post();
        }
    }

    if (err != OK) {
        for (size_t i = mPacketSources.size(); i-- > 0;) {
            sp<AmAnotherPacketSource> packetSource = mPacketSources.valueAt(i);
            packetSource->clear();
        }
        return err;
    }

    if (!mStreamTypeMask) {
        // Signal gap is filled between original and new stream.
        ALOGV("ERROR OUT OF RANGE");
        return ERROR_OUT_OF_RANGE;
    }

    return err;
}

void AmPlaylistFetcher::sniff(const sp<ABuffer> &buffer) {
    sp<DataSource> dataSource = new HLSDataSource(buffer);
    //mExtractor = MediaExtractor::Create(dataSource);
    if (mExtractor == NULL) {
        ALOGI("lzhnpng: sniff error");
        return;
    }

    size_t numtracks = mExtractor->countTracks();

    if (numtracks == 0) {
        return;
    }

/*
    for (size_t i = 0; i < numtracks; ++i) {
        const char *mime;
        sp<MetaData> meta = mExtractor->getTrackMetaData(i);
        sp<MediaSource> track = mExtractor->getTrack(i);
        if (track->start(meta.get()) != OK) {
            track.clear();
            continue;
        }
        CHECK(meta->findCString(kKeyMIMEType, &mime));
        if (!strncasecmp(mime, "audio/", 6)) {
            mAudioTrack = track;
            mAudioSource = new AmAnotherPacketSource(track->getFormat());
        } else if (!strncasecmp(mime, "video/", 6)) {
            mVideoTrack = track;
            mVideoSource = new AmAnotherPacketSource(track->getFormat());
        }
    }
    */
}

void AmPlaylistFetcher::readFromNonTsFile() {
    MediaSource::ReadOptions options;
    MediaBuffer *mediaBuffer;
    sp<ABuffer> accessUnit;
    if (mAudioTrack != NULL) {
        while (mAudioTrack->read(&mediaBuffer, &options) == OK) {
            if (mediaBuffer != NULL) {
                accessUnit = mediaBufferToABuffer(mediaBuffer);
                mAudioSource->queueAccessUnit(accessUnit);
                mediaBuffer->release();
                mediaBuffer = NULL;
            }
        }
    }
    if (mVideoSource != NULL) {
        while (mVideoTrack->read(&mediaBuffer, &options) == OK) {
            if (mediaBuffer != NULL) {
                accessUnit = mediaBufferToABuffer(mediaBuffer);
                mVideoSource->queueAccessUnit(accessUnit);
                mediaBuffer->release();
                mediaBuffer = NULL;
            }
        }
    }
}

sp<ABuffer> AmPlaylistFetcher::mediaBufferToABuffer(MediaBuffer* mediaBuffer) {
    sp<ABuffer> abuffer = new ABuffer(mediaBuffer->range_length());

    memcpy(abuffer->data(),
        (const uint8_t *)mediaBuffer->data() + mediaBuffer->range_offset(),
        mediaBuffer->range_length());

    sp<AMessage> meta = abuffer->meta();
    int64_t timeUs;
    CHECK(mediaBuffer->meta_data()->findInt64(kKeyTime, &timeUs));
    timeUs += getSegmentStartTimeUs(mSeqNumber);
    meta->setInt64("timeUs", timeUs);

    int64_t durationUs;
    if (mediaBuffer->meta_data()->findInt64(kKeyDuration, &durationUs)) {
        meta->setInt64("durationUs", durationUs);
    }

    return abuffer;
}

/* static */
bool AmPlaylistFetcher::bufferStartsWithWebVTTMagicSequence(
        const sp<ABuffer> &buffer) {
    size_t pos = 0;

    // skip possible BOM
    if (buffer->size() >= pos + 3 &&
            !memcmp("\xef\xbb\xbf", buffer->data() + pos, 3)) {
        pos += 3;
    }

    // accept WEBVTT followed by SPACE, TAB or (CR) LF
    if (buffer->size() < pos + 6 ||
            memcmp("WEBVTT", buffer->data() + pos, 6)) {
        return false;
    }
    pos += 6;

    if (buffer->size() == pos) {
        return true;
    }

    uint8_t sep = buffer->data()[pos];
    return sep == ' ' || sep == '\t' || sep == '\n' || sep == '\r';
}

status_t AmPlaylistFetcher::extractAndQueueAccessUnits(
        const sp<ABuffer> &buffer, const sp<AMessage> &itemMeta) {
    if (bufferStartsWithWebVTTMagicSequence(buffer)) {
        if (mStreamTypeMask != AmLiveSession::STREAMTYPE_SUBTITLES) {
            ALOGE("This stream only contains subtitles.");
            return ERROR_MALFORMED;
        }

        const sp<AmAnotherPacketSource> packetSource =
            mPacketSources.valueFor(AmLiveSession::STREAMTYPE_SUBTITLES);

        int64_t durationUs;
        CHECK(itemMeta->findInt64("durationUs", &durationUs));
        buffer->meta()->setInt64("timeUs", getSegmentStartTimeUs(mSeqNumber));
        buffer->meta()->setInt64("durationUs", durationUs);
        buffer->meta()->setInt64("segmentStartTimeUs", getSegmentStartTimeUs(mSeqNumber));
        buffer->meta()->setInt32("discontinuitySeq", mDiscontinuitySeq);
        buffer->meta()->setInt32("subtitleGeneration", mSubtitleGeneration);

        packetSource->queueAccessUnit(buffer);
        return OK;
    }

    if (mNextPTSTimeUs >= 0ll) {
        mFirstPTSValid = false;
        mAbsoluteTimeAnchorUs = mNextPTSTimeUs;
        mNextPTSTimeUs = -1ll;
    }

    // This better be an ISO 13818-7 (AAC) or ISO 13818-1 (MPEG) audio
    // stream prefixed by an ID3 tag.

    bool firstID3Tag = true;
    uint64_t PTS = 0;

    for (;;) {
        // Make sure to skip all ID3 tags preceding the audio data.
        // At least one must be present to provide the PTS timestamp.

        ID3 id3(buffer->data(), buffer->size(), true /* ignoreV1 */);
        if (!id3.isValid()) {
            if (firstID3Tag) {
                ALOGE("Unable to parse ID3 tag.");
                return ERROR_UNSUPPORTED; // to notify service to create new player.
            } else {
                break;
            }
        }

        if (firstID3Tag) {
            bool found = false;

            ID3::Iterator it(id3, "PRIV");
            while (!it.done()) {
                size_t length;
                const uint8_t *data = it.getData(&length);

                static const char *kMatchName =
                    "com.apple.streaming.transportStreamTimestamp";
                static const size_t kMatchNameLen = strlen(kMatchName);

                if (length == kMatchNameLen + 1 + 8
                        && !strncmp((const char *)data, kMatchName, kMatchNameLen)) {
                    found = true;
                    PTS = U64_AT(&data[kMatchNameLen + 1]);
                }

                it.next();
            }

            if (!found) {
                ALOGE("Unable to extract transportStreamTimestamp from ID3 tag.");
                return ERROR_MALFORMED;
            }
        }

        // skip the ID3 tag
        buffer->setRange(
                buffer->offset() + id3.rawSize(), buffer->size() - id3.rawSize());

        firstID3Tag = false;
    }

    if (mStreamTypeMask != AmLiveSession::STREAMTYPE_AUDIO) {
        ALOGW("This stream only contains audio data!");

        mStreamTypeMask &= AmLiveSession::STREAMTYPE_AUDIO;

        if (mStreamTypeMask == 0) {
            return OK;
        }
    }

    sp<AmAnotherPacketSource> packetSource =
        mPacketSources.valueFor(AmLiveSession::STREAMTYPE_AUDIO);

    if (packetSource->getFormat() == NULL && buffer->size() >= 7) {
        ABitReader bits(buffer->data(), buffer->size());

        // adts_fixed_header

        CHECK_EQ(bits.getBits(12), 0xfffu);
        bits.skipBits(3);  // ID, layer
        bool protection_absent = bits.getBits(1) != 0;

        unsigned profile = bits.getBits(2);
        CHECK_NE(profile, 3u);
        unsigned sampling_freq_index = bits.getBits(4);
        bits.getBits(1);  // private_bit
        unsigned channel_configuration = bits.getBits(3);
        CHECK_NE(channel_configuration, 0u);
        bits.skipBits(2);  // original_copy, home

        sp<MetaData> meta = MakeAACCodecSpecificData(
                profile, sampling_freq_index, channel_configuration);

        meta->setInt32(kKeyIsADTS, true);

        int32_t sampleRate;
        int32_t numChannels;
        CHECK(meta->findInt32(kKeySampleRate, &sampleRate));
        CHECK(meta->findInt32(kKeyChannelCount, &numChannels));
        ALOGI("[ID3] found AAC codec config (%d Hz, %d channels)", sampleRate, numChannels);

        packetSource->setFormat(meta);
    }

    int64_t numSamples = 0ll;
    int32_t sampleRate;
    CHECK(packetSource->getFormat()->findInt32(kKeySampleRate, &sampleRate));

    if (!mPostPrepared) {
        ALOGI("[ID3] source prepared!");
        mPostPrepared = true;
        sp<AMessage> msg = mNotify->dup();
        msg->setInt32("what", kWhatTemporarilyDoneFetching);
        msg->setString("uri", mURI.c_str());
        msg->post();
    }

    int64_t timeUs = (PTS * 100ll) / 9ll;
    if (!mFirstPTSValid) {
        mFirstPTSValid = true;
        mFirstTimeUs = timeUs;
    }

    size_t offset = 0;
    while (offset < buffer->size()) {
        const uint8_t *adtsHeader = buffer->data() + offset;
        CHECK_LT(offset + 5, buffer->size());

        unsigned aac_frame_length =
            ((adtsHeader[3] & 3) << 11)
            | (adtsHeader[4] << 3)
            | (adtsHeader[5] >> 5);

        if (aac_frame_length == 0) {
            const uint8_t *id3Header = adtsHeader;
            if (!memcmp(id3Header, "ID3", 3)) {
                ID3 id3(id3Header, buffer->size() - offset, true);
                if (id3.isValid()) {
                    offset += id3.rawSize();
                    continue;
                };
            }
            return ERROR_MALFORMED;
        }

        CHECK_LE(offset + aac_frame_length, buffer->size());

        int64_t unitTimeUs = timeUs + numSamples * 1000000ll / sampleRate;
        offset += aac_frame_length;

        // Each AAC frame encodes 1024 samples.
        numSamples += 1024;

        if (mStartup) {
            int64_t startTimeUs = unitTimeUs;
            if (mStartTimeUsRelative) {
                startTimeUs -= mFirstTimeUs;
                if (startTimeUs  < 0) {
                    startTimeUs = 0;
                }
            }
            if (startTimeUs < mStartTimeUs) {
                continue;
            }

            if (mStartTimeUsNotify != NULL) {
                int32_t targetDurationSecs;
                CHECK(mPlaylist->meta()->findInt32("target-duration", &targetDurationSecs));
                int64_t targetDurationUs = targetDurationSecs * 1000000ll;

                // Duplicated logic from how we handle .ts playlists.
                if (mStartup && mSegmentStartTimeUs >= 0
                        && timeUs - mStartTimeUs > targetDurationUs) {
                    int32_t newSeqNumber = getSeqNumberWithAnchorTime(timeUs);
                    if (newSeqNumber >= mSeqNumber) {
                        --mSeqNumber;
                    } else {
                        mSeqNumber = newSeqNumber;
                    }
                    return -EAGAIN;
                }

                mStartTimeUsNotify->setInt64("timeUsAudio", timeUs);
                mStartTimeUsNotify->setInt32("discontinuitySeq", mDiscontinuitySeq);
                mStartTimeUsNotify->setInt32("streamMask", AmLiveSession::STREAMTYPE_AUDIO);
                mStartTimeUsNotify->post();
                mStartTimeUsNotify.clear();
                mStartup = false;
            }
        }

        if (mStopParams != NULL) {
            // Queue discontinuity in original stream.
            int32_t discontinuitySeq;
            int64_t stopTimeUs;
            if (!mStopParams->findInt32("discontinuitySeq", &discontinuitySeq)
                    || discontinuitySeq > mDiscontinuitySeq
                    || !mStopParams->findInt64("timeUsAudio", &stopTimeUs)
                    || (discontinuitySeq == mDiscontinuitySeq && unitTimeUs >= stopTimeUs)) {
                packetSource->queueAccessUnit(mSession->createFormatChangeBuffer());
                mStreamTypeMask = 0;
                mPacketSources.clear();
                return ERROR_OUT_OF_RANGE;
            }
        }

        sp<ABuffer> unit = new ABuffer(aac_frame_length);
        memcpy(unit->data(), adtsHeader, aac_frame_length);

        unit->meta()->setInt64("timeUs", unitTimeUs);
        setAccessUnitProperties(unit, packetSource);
        packetSource->queueAccessUnit(unit);
    }

    return OK;
}

void AmPlaylistFetcher::updateDuration() {
    int64_t durationUs = 0ll;
    for (size_t index = 0; index < mPlaylist->size(); ++index) {
        sp<AMessage> itemMeta;
        CHECK(mPlaylist->itemAt(
                    index, NULL /* uri */, &itemMeta));

        int64_t itemDurationUs;
        CHECK(itemMeta->findInt64("durationUs", &itemDurationUs));

        durationUs += itemDurationUs;
    }

    sp<AMessage> msg = mNotify->dup();
    msg->setInt32("what", kWhatDurationUpdate);
    msg->setInt64("durationUs", durationUs);
    msg->post();
}

int64_t AmPlaylistFetcher::resumeThreshold(const sp<AMessage> &msg) {
    int64_t durationUs, threshold;
    if (msg->findInt64("durationUs", &durationUs) && durationUs > 0) {
        return kNumSkipFrames * durationUs;
    }

    sp<RefBase> obj;
    msg->findObject("format", &obj);
    MetaData *format = static_cast<MetaData *>(obj.get());

    const char *mime;
    CHECK(format->findCString(kKeyMIMEType, &mime));
    bool audio = !strncasecmp(mime, "audio/", 6);
    if (audio) {
        // Assumes 1000 samples per frame.
        int32_t sampleRate;
        CHECK(format->findInt32(kKeySampleRate, &sampleRate));
        return kNumSkipFrames  /* frames */ * 1000 /* samples */
                * (1000000 / sampleRate) /* sample duration (us) */;
    } else {
        int32_t frameRate;
        if (format->findInt32(kKeyFrameRate, &frameRate) && frameRate > 0) {
            return kNumSkipFrames * (1000000 / frameRate);
        }
    }

    return 500000ll;
}

void AmPlaylistFetcher::setBufferingStatus(bool buffing) {
    mBuffering =  buffing;
}


}  // namespace android
