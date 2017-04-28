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

#include <media/IMediaHTTPService.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/MediaDefs.h>

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
      mFlags(0),
      mFinalResult(OK),
      mOffset(0),
      mFetchSubtitleDataGeneration(0),
      mFetchMetaDataGeneration(0),
      mHasMetadata(false),
      mMetadataSelected(false),
      mInterruptCallback(pfunc),
      mBuffering(false) {
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
    ALOGI("~HTTPLiveSource");
    if (mLiveSession != NULL) {
        //mLiveSession->disconnect();

        mLiveLooper->unregisterHandler(mLiveSession->id());
        mLiveLooper->unregisterHandler(id());
        mLiveLooper->stop();

        mLiveSession.clear();
        mLiveLooper.clear();
    }
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

sp<AMessage> AmNuPlayer::HTTPLiveSource::getFormat(bool audio) {
    sp<AMessage> format;
    status_t err = -EWOULDBLOCK;
    if (mLiveSession != NULL) {
        err = mLiveSession->getStreamFormat(
                audio ? AmLiveSession::STREAMTYPE_AUDIO
                      : AmLiveSession::STREAMTYPE_VIDEO,
                &format);
    }

    if (err == -EWOULDBLOCK) {
        format = new AMessage();
        format->setInt32("err", err);
        return format;
    }

    if (err != OK) {
        return NULL;
    }

    return format;
}

void AmNuPlayer::HTTPLiveSource::disconnect() {
    if (mLiveSession != NULL)
        mLiveSession->disconnect();
}

status_t AmNuPlayer::HTTPLiveSource::feedMoreTSData() {
    return OK;
}

status_t AmNuPlayer::HTTPLiveSource::dequeueAccessUnit(
        bool audio, sp<ABuffer> *accessUnit) {

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
    } else if (type == MEDIA_TRACK_TYPE_METADATA) {
        // MEDIA_TRACK_TYPE_METADATA is always last track
        // mMetadataSelected can only be true when mHasMetadata is true
        return mMetadataSelected ? (mLiveSession->getTrackCount() - 1) : -1;
    } else {
        return mLiveSession->getSelectedTrack(type);
    }
}

status_t AmNuPlayer::HTTPLiveSource::selectTrack(size_t trackIndex, bool select, int64_t /*timeUs*/) {
    if (mLiveSession == NULL) {
        return INVALID_OPERATION;
    }

    status_t err = INVALID_OPERATION;
    bool postFetchMsg = false, isSub = false;
    if (!mHasMetadata || trackIndex != mLiveSession->getTrackCount() - 1) {
        err = mLiveSession->selectTrack(trackIndex, select);
        postFetchMsg = select;
        isSub = true;
    } else {
        // metadata track; i.e. (mHasMetadata && trackIndex == mLiveSession->getTrackCount() - 1)
        if (mMetadataSelected && !select) {
            err = OK;
        } else if (!mMetadataSelected && select) {
            postFetchMsg = true;
            err = OK;
        } else {
            err = BAD_VALUE; // behave as AmLiveSession::selectTrack
        }

        mMetadataSelected = select;
    }

    if (err == OK) {
        int32_t &generation = isSub ? mFetchSubtitleDataGeneration : mFetchMetaDataGeneration;
        generation++;
        if (postFetchMsg) {
            int32_t what = isSub ? kWhatFetchSubtitleData : kWhatFetchMetaData;
            sp<AMessage> msg = new AMessage(what, this);
            msg->setInt32("generation", generation);
            msg->post();
        }
    }

    // AmLiveSession::selectTrack returns BAD_VALUE when selecting the currently
    // selected track, or unselecting a non-selected track. In this case it's an
    // no-op so we return OK.
    return (err == OK || err == BAD_VALUE) ? (status_t)OK : err;
}

status_t AmNuPlayer::HTTPLiveSource::seekTo(int64_t seekTimeUs) {
    return mLiveSession->seekTo(seekTimeUs);
}

void AmNuPlayer::HTTPLiveSource::setParentThreadId(android_thread_id_t thread_id) {
    mParentThreadId = thread_id;
}

void AmNuPlayer::HTTPLiveSource::pollForRawData(
        const sp<AMessage> &msg, int32_t currentGeneration,
        AmLiveSession::StreamType fetchType, int32_t pushWhat) {

    int32_t generation;
    CHECK(msg->findInt32("generation", &generation));

    if (generation != currentGeneration) {
        return;
    }

    sp<ABuffer> buffer;
    while (mLiveSession->dequeueAccessUnit(fetchType, &buffer) == OK) {

        sp<AMessage> notify = dupNotify();
        notify->setInt32("what", pushWhat);
        notify->setBuffer("buffer", buffer);

        int64_t timeUs, baseUs, delayUs;
        CHECK(buffer->meta()->findInt64("baseUs", &baseUs));
        CHECK(buffer->meta()->findInt64("timeUs", &timeUs));
        delayUs = baseUs + timeUs - ALooper::GetNowUs();

        if (fetchType == AmLiveSession::STREAMTYPE_SUBTITLES) {
            notify->post();
            msg->post(delayUs > 0ll ? delayUs : 0ll);
            return;
        } else if (fetchType == AmLiveSession::STREAMTYPE_METADATA) {
            if (delayUs < -1000000ll) { // 1 second
                continue;
            }
            notify->post();
            // push all currently available metadata buffers in each invocation of pollForRawData
            // continue;
        } else {
            TRESPASS();
        }
    }

    // try again in 1 second
    msg->post(1000000ll);
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
            pollForRawData(
                    msg, mFetchSubtitleDataGeneration,
                    /* fetch */ AmLiveSession::STREAMTYPE_SUBTITLES,
                    /* push */ kWhatSubtitleData);

            break;
        }

        case kWhatFetchMetaData:
        {
            if (!mMetadataSelected) {
                break;
            }

            pollForRawData(
                    msg, mFetchMetaDataGeneration,
                    /* fetch */ AmLiveSession::STREAMTYPE_METADATA,
                    /* push */ kWhatTimedMetaData);

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

    switch (what) {
        case AmLiveSession::kWhatPrepared:
        {
            ALOGI("session notify prepared!\n");

            sp<AMessage> notify = dupNotify();
            notify->setInt32("what", kWhatSourceReady);
            notify->setInt32("err", 0);
            notify->post();

            // notify the current size here if we have it, otherwise report an initial size of (0,0)
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
            status_t err;
            CHECK(msg->findInt32("err", &err));

            notifyPrepared(err);
            break;
        }

        case AmLiveSession::kWhatStreamsChanged:
        {
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

        case AmLiveSession::kWhatBufferingStart:
        {
            mBuffering = true;
            sp<AMessage> notify = dupNotify();
            notify->setInt32("what", kWhatPauseOnBufferingStart);
            notify->post();
            break;
        }

        case AmLiveSession::kWhatBufferingEnd:
        {
            mBuffering = false;
            sp<AMessage> notify = dupNotify();
            notify->setInt32("what", kWhatResumeOnBufferingEnd);
            notify->post();
            break;
        }


        case AmLiveSession::kWhatBufferingUpdate:
        {
            sp<AMessage> notify = dupNotify();
            int32_t percentage;
            CHECK(msg->findInt32("percentage", &percentage));
            notify->setInt32("what", kWhatBufferingUpdate);
            notify->setInt32("percentage", percentage);
            notify->post();
            break;
        }

        case AmLiveSession::kWhatMetadataDetected:
        {
            if (!mHasMetadata) {
                mHasMetadata = true;

                sp<AMessage> notify = dupNotify();
                // notification without buffer triggers MEDIA_INFO_METADATA_UPDATE
                notify->setInt32("what", kWhatTimedMetaData);
                notify->post();
            }
            break;
        }

        case AmLiveSession::kWhatError:
        {
            int32_t err;
            CHECK(msg->findInt32("err", &err));
            if (err == ERROR_UNSUPPORTED) {  //add
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

