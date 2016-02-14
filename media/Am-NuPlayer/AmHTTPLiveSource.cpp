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
#define LOG_TAG "NU-HTTPLiveSource"
#include <utils/Log.h>

#include "AmHTTPLiveSource.h"

#include "AmAnotherPacketSource.h"
#include "AmLiveDataSource.h"
#include "AmLiveSession.h"

#include <media/IMediaHTTPService.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MetaData.h>

namespace android {

AmNuPlayer::HTTPLiveSource::HTTPLiveSource(
        const sp<AMessage> &notify,
        const sp<IMediaHTTPService> &httpService,
        const char *url,
        const KeyedVector<String8, String8> *headers,
        interruptcallback pfunc)
    : Source(notify),
      mHTTPService(httpService),
      mURL(url),
      mBuffering(false),
      mFlags(0),
      mFinalResult(OK),
      mOffset(0),
      mFetchSubtitleDataGeneration(0),
      mHasSub(false),
      mInterruptCallback(pfunc) {
    if (headers) {
        mExtraHeaders = *headers;

        ssize_t index =
            mExtraHeaders.indexOfKey(String8("x-hide-urls-from-log"));

        if (index >= 0) {
            mFlags |= kFlagIncognito;

            mExtraHeaders.removeItemsAt(index);
        }
    }
}

AmNuPlayer::HTTPLiveSource::~HTTPLiveSource() {
    ALOGI("[%s:%d] start !", __FUNCTION__, __LINE__);
    if (mLiveSession != NULL) {
        mLiveSession->disconnect();

        mLiveLooper->unregisterHandler(mLiveSession->id());
        //mLiveLooper->unregisterHandler(this);
        mLiveLooper->stop();

        mLiveSession.clear();
        mLiveLooper.clear();
    }
    ALOGI("[%s:%d] end !", __FUNCTION__, __LINE__);
}

void AmNuPlayer::HTTPLiveSource::prepareAsync() {
    if (mLiveLooper == NULL) {
        mLiveLooper = new ALooper;
        mLiveLooper->setName("http live");
        mLiveLooper->start();

        mLiveLooper->registerHandler(this);
    }

    sp<AMessage> notify = new AMessage(kWhatSessionNotify, this);

    mLiveSession = new AmLiveSession(
            notify,
            (mFlags & kFlagIncognito) ? AmLiveSession::kFlagIncognito : 0,
            mHTTPService,
            mInterruptCallback);

    mLiveSession->setParentThreadId(mParentThreadId);

    mLiveLooper->registerHandler(mLiveSession);

    mLiveSession->connectAsync(
            mURL.c_str(), mExtraHeaders.isEmpty() ? NULL : &mExtraHeaders);

}

void AmNuPlayer::HTTPLiveSource::start() {
}

void AmNuPlayer::HTTPLiveSource::setParentThreadId(android_thread_id_t thread_id) {
    mParentThreadId = thread_id;
}

sp<AMessage> AmNuPlayer::HTTPLiveSource::getFormat(bool audio) {
    if (mLiveSession == NULL) {
        return NULL;
    }

    sp<AMessage> format;
    status_t err = mLiveSession->getStreamFormat(
            audio ? AmLiveSession::STREAMTYPE_AUDIO
                  : AmLiveSession::STREAMTYPE_VIDEO,
            &format);

    if (err != OK) {
        return NULL;
    }

    return format;
}

status_t AmNuPlayer::HTTPLiveSource::feedMoreTSData() {
    return OK;
}

status_t AmNuPlayer::HTTPLiveSource::dequeueAccessUnit(
        bool audio, sp<ABuffer> *accessUnit) {
    if (mBuffering) {
        if (!mLiveSession->haveSufficientDataOnAVTracks()) {
            return -EWOULDBLOCK;
        }
        mBuffering = false;
        sp<AMessage> notify = dupNotify();
        notify->setInt32("what", kWhatBufferingEnd);
        notify->post();
        notify->setInt32("what", kWhatResumeOnBufferingEnd);
        notify->post();
        ALOGI("HTTPLiveSource buffering end!\n");
    }

    bool needBuffering = false;
    status_t finalResult = mLiveSession->hasBufferAvailable(audio, &needBuffering);
    if (needBuffering) {
        mBuffering = true;
        sp<AMessage> notify = dupNotify();
        notify->setInt32("what", kWhatPauseOnBufferingStart);
        notify->post();
        notify->setInt32("what", kWhatBufferingStart);
        notify->post();
        ALOGI("HTTPLiveSource buffering start!\n");
        return finalResult;
    }

    return mLiveSession->dequeueAccessUnit(
            audio ? AmLiveSession::STREAMTYPE_AUDIO
                  : AmLiveSession::STREAMTYPE_VIDEO,
            accessUnit);
}

status_t AmNuPlayer::HTTPLiveSource::getDuration(int64_t *durationUs) {
    return mLiveSession->getDuration(durationUs);
}

size_t AmNuPlayer::HTTPLiveSource::getTrackCount() const {
    return mLiveSession->getTrackCount();
}

sp<AMessage> AmNuPlayer::HTTPLiveSource::getTrackInfo(size_t trackIndex) const {
    return mLiveSession->getTrackInfo(trackIndex);
}

ssize_t AmNuPlayer::HTTPLiveSource::getSelectedTrack(media_track_type type) const {
    if (mLiveSession == NULL) {
        return -1;
    } else {
        return mLiveSession->getSelectedTrack(type);
    }
}

status_t AmNuPlayer::HTTPLiveSource::selectTrack(size_t trackIndex, bool select, int64_t /*timeUs*/) {
    status_t err = mLiveSession->selectTrack(trackIndex, select);

    if (err == OK) {
        int32_t trackType;
        sp<AMessage> format = mLiveSession->getTrackInfo(trackIndex);
        if (format != NULL && format->findInt32("type", &trackType)
                && trackType == MEDIA_TRACK_TYPE_SUBTITLE) {
            mFetchSubtitleDataGeneration++;
            if (select) {
                mHasSub = true;
                mLiveSession->setSubTrackIndex(trackIndex);
                sp<AMessage> msg = new AMessage(kWhatFetchSubtitleData, this);
                msg->setInt32("generation", mFetchSubtitleDataGeneration);
                msg->post();
            } else {
                mHasSub = false;
            }
        }
    }

    // LiveSession::selectTrack returns BAD_VALUE when selecting the currently
    // selected track, or unselecting a non-selected track. In this case it's an
    // no-op so we return OK.
    return (err == OK || err == BAD_VALUE) ? (status_t)OK : err;
}

status_t AmNuPlayer::HTTPLiveSource::seekTo(int64_t seekTimeUs) {
    if (mHasSub) {
        sp<AMessage> msg = new AMessage(kWhatFetchSubtitleData, this);
        msg->setInt32("generation", mFetchSubtitleDataGeneration);
        msg->post();
    }
    return mLiveSession->seekTo(seekTimeUs);
}

void AmNuPlayer::HTTPLiveSource::onMessageReceived(const sp<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatSessionNotify:
        {
            onSessionNotify(msg);
            break;
        }

        case kWhatFetchSubtitleData:
        {
            int32_t generation;
            CHECK(msg->findInt32("generation", &generation));

            if (generation != mFetchSubtitleDataGeneration) {
                // stale
                break;
            }

            sp<ABuffer> buffer;
            if (mLiveSession->dequeueAccessUnit(
                    AmLiveSession::STREAMTYPE_SUBTITLES, &buffer) == OK) {
                sp<AMessage> notify = dupNotify();
                notify->setInt32("what", kWhatSubtitleData);
                notify->setBuffer("buffer", buffer);
                notify->post();

                int64_t timeUs, baseUs, durationUs, delayUs;
                CHECK(buffer->meta()->findInt64("baseUs", &baseUs));
                CHECK(buffer->meta()->findInt64("timeUs", &timeUs));
                CHECK(buffer->meta()->findInt64("durationUs", &durationUs));
                delayUs = baseUs + timeUs - ALooper::GetNowUs();

                msg->post(delayUs > 0ll ? delayUs : 0ll);
            } else {
                // try again in 1 second
                msg->post(1000000ll);
            }

            break;
        }

        default:
            Source::onMessageReceived(msg);
            break;
    }
}

void AmNuPlayer::HTTPLiveSource::onSessionNotify(const sp<AMessage> &msg) {
    int32_t what;
    CHECK(msg->findInt32("what", &what));
    ALOGI("session notify : %d\n", what);

    switch (what) {
        case AmLiveSession::kWhatPrepared:
        {
            // notify the current size here if we have it, otherwise report an initial size of (0,0)
            ALOGI("session notify prepared!\n");

            sp<AMessage> notify = dupNotify();
            notify->setInt32("what", kWhatSourceReady);
            notify->setInt32("err", 0);
            notify->post();

            sp<AMessage> format = getFormat(false /* audio */);
            int32_t width;
            int32_t height;
            if (format != NULL &&
                    format->findInt32("width", &width) && format->findInt32("height", &height)) {
                notifyVideoSizeChanged(format);
            } else {
                notifyVideoSizeChanged();
            }

            uint32_t flags = FLAG_CAN_PAUSE;
            if (mLiveSession->isSeekable()) {
                flags |= FLAG_CAN_SEEK;
                flags |= FLAG_CAN_SEEK_BACKWARD;
                flags |= FLAG_CAN_SEEK_FORWARD;
            }

            if (mLiveSession->hasDynamicDuration()) {
                flags |= FLAG_DYNAMIC_DURATION;
            }

            notifyFlagsChanged(flags);

            notifyPrepared();
            break;
        }

        case AmLiveSession::kWhatPreparationFailed:
        {
            ALOGI("session notify preparation failed!\n");
            status_t err;
            CHECK(msg->findInt32("err", &err));

            notifyPrepared(err);
            break;
        }

        case AmLiveSession::kWhatStreamsChanged:
        {
            ALOGI("session notify streams changed!\n");
            uint32_t changedMask;
            CHECK(msg->findInt32(
                        "changedMask", (int32_t *)&changedMask));

            bool audio = changedMask & AmLiveSession::STREAMTYPE_AUDIO;
            bool video = changedMask & AmLiveSession::STREAMTYPE_VIDEO;

            sp<AMessage> reply;
            CHECK(msg->findMessage("reply", &reply));

            sp<AMessage> notify = dupNotify();
            notify->setInt32("what", kWhatQueueDecoderShutdown);
            notify->setInt32("audio", audio);
            notify->setInt32("video", video);
            notify->setMessage("reply", reply);
            notify->post();
            break;
        }

        case AmLiveSession::kWhatError:
        {
            int32_t err;
            CHECK(msg->findInt32("err", &err));
            if (err == ERROR_UNSUPPORTED) {
                sp<AMessage> notify = dupNotify();
                notify->setInt32("what", kWhatSourceReady);
                notify->setInt32("err", 1);
                notify->post();
            }
            break;
        }

        case AmLiveSession::kWhatSourceReady:
        {
            sp<AMessage> notify = dupNotify();
            notify->setInt32("what", kWhatSourceReady);
            int32_t err;
            CHECK(msg->findInt32("err", &err));
            notify->setInt32("err", err);
            notify->post();
            break;
        }

        case AmLiveSession::kWhatSetFrameRate:
        {
            sp<AMessage> notify = dupNotify();
            float frameRate;
            CHECK(msg->findFloat("frame-rate", &frameRate));
            notify->setInt32("what", kWhatFrameRate);
            notify->setFloat("frame-rate", frameRate);
            notify->post();
            break;
        }

        default:
            TRESPASS();
    }
}

}  // namespace android

