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

#define LOG_NDEBUG 0
#define LOG_TAG "NU-NuPlayerRenderer"
#include <utils/Log.h>

#include "AmNuPlayerRenderer.h"
#include <algorithm>
#include <cutils/properties.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/foundation/AUtils.h>
#include <media/stagefright/foundation/AWakeLock.h>
#include <media/stagefright/MediaClock.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/Utils.h>
#include <media/stagefright/VideoFrameScheduler.h>
#include <media/stagefright/MediaDefs.h>
#ifdef WITH_AMLOGIC_MEDIA_EX_SUPPORT
#include <media/stagefright/AmMediaDefsExt.h>
#endif

#include <inttypes.h>
#include <Amavutils.h>

namespace android {

/*
 * Example of common configuration settings in shell script form

   #Turn offload audio off (use PCM for Play Music) -- AudioPolicyManager
   adb shell setprop audio.offload.disable 1

   #Allow offload audio with video (requires offloading to be enabled) -- AudioPolicyManager
   adb shell setprop audio.offload.video 1

   #Use audio callbacks for PCM data
   adb shell setprop media.stagefright.audio.cbk 1

   #Use deep buffer for PCM data with video (it is generally enabled for audio-only)
   adb shell setprop media.stagefright.audio.deep 1

   #Set size of buffers for pcm audio sink in msec (example: 1000 msec)
   adb shell setprop media.stagefright.audio.sink 1000

 * These configurations take effect for the next track played (not the current track).
 */

static inline bool getUseAudioCallbackSetting() {
    return property_get_bool("media.stagefright.audio.cbk", false /* default_value */);
}

static inline int32_t getAudioSinkPcmMsSetting() {
    return property_get_int32(
            "media.stagefright.audio.sink", 500 /* default_value */);
}

// Maximum time in paused state when offloading audio decompression. When elapsed, the AudioSink
// is closed to allow the audio DSP to power down.
static const int64_t kOffloadPauseMaxUs = 10000000ll;

// Maximum allowed delay from AudioSink, 1.5 seconds.
static const int64_t kMaxAllowedAudioSinkDelayUs = 1500000ll;

static const int64_t kMinimumAudioClockUpdatePeriodUs = 20 /* msec */ * 1000;

// static
const AmNuPlayer::Renderer::PcmInfo AmNuPlayer::Renderer::AUDIO_PCMINFO_INITIALIZER = {
        AUDIO_CHANNEL_NONE,
        AUDIO_OUTPUT_FLAG_NONE,
        AUDIO_FORMAT_INVALID,
        0, // mNumChannels
        0 // mSampleRate
};

// static
const int64_t AmNuPlayer::Renderer::kMinPositionUpdateDelayUs = 100000ll;

#define PTS_LOG_LEVEL(level,formats...)\
    do {\
        if (mDebugLevel >= level) {\
            ALOGI("PTS: " formats);}\
    } while(0)


#define PTS_LOG(fmt...) PTS_LOG_LEVEL(0,##fmt)
#define PTS_LOGV(fmt...) PTS_LOG_LEVEL(2,##fmt)
#define PTS_LOGD(fmt...) PTS_LOG_LEVEL(4,##fmt)
#define PTS_LOGDD(fmt...) PTS_LOG_LEVEL(8,##fmt)

AmNuPlayer::Renderer::Renderer(
        const sp<MediaPlayerBase::AudioSink> &sink,
        const sp<AMessage> &notify,
        uint32_t flags)
    : mAudioSink(sink),
      mUseVirtualAudioSink(false),
      mNotify(notify),
      mFlags(flags),
      mNumFramesWritten(0),
      mDrainAudioQueuePending(false),
      mDrainVideoQueuePending(false),
      mAudioQueueGeneration(0),
      mVideoQueueGeneration(0),
      mAudioDrainGeneration(0),
      mVideoDrainGeneration(0),
      mAudioEOSGeneration(0),
      mPlaybackSettings(AUDIO_PLAYBACK_RATE_DEFAULT),
      mAudioFirstAnchorTimeMediaUs(-1),
      mAnchorTimeMediaUs(-1),
      mAnchorNumFramesWritten(-1),
      mVideoLateByUs(0ll),
      mHasAudio(false),
      mHasVideo(false),
      mAudioEOS(false),
      misTrickmode(false),
      mNotifyCompleteAudio(false),
      mNotifyCompleteVideo(false),
      mSyncQueues(false),
      mQueueInitial(true),
      mRenderStarted(false),
      mPaused(false),
      mPauseDrainAudioAllowedUs(0),
      mVideoSampleReceived(false),
      mVideoRenderingStarted(false),
      mVideoRenderingStartGeneration(0),
      mAudioRenderingStartGeneration(0),
      mRenderingDataDelivered(false),
      mNextAudioClockUpdateTimeUs(-1),
      mLastAudioMediaTimeUs(-1),
      mAudioOffloadPauseTimeoutGeneration(0),
      mAudioTornDown(false),
      mCurrentOffloadInfo(AUDIO_INFO_INITIALIZER),
      mCurrentPcmInfo(AUDIO_PCMINFO_INITIALIZER),
      mTotalBuffersQueued(0),
      mLastAudioBufferDrained(0),
      mUseAudioCallback(false),
      mWakeLock(new AWakeLock()) {
    mMediaClock = new MediaClock;
    mPlaybackRate = mPlaybackSettings.mSpeed;
    mMediaClock->setPlaybackRate(mPlaybackRate);
/////////////
    char value[PROPERTY_VALUE_MAX];
    mLastVideoUs = -1;
    mLastestVideoFrameIntervalUs = 0;
    mAvgVideoFrameIntervalUs =-1;

    mLastAudioUs = -1;
    mLastAudioFrameBytes = 0;

    mAudioTimeJump = false;
    mVideoTimeJump = false;
    mVideoJumpedTimeUs = 0;
    mAudioJumpedTimeUs = 0;
    mAjumpedNum = 0;
    mVjumpedNum = 0;
    mTotalAudioJumpedTimeUs = 0;
    mDebugLevel = 0;
    mLastInfoTime = 0;
    mVideoFrameOutNum = 0;
    mFirstVideoRealTime = 0;
    mVideoRealtime = 0;
    int ret;
    if (property_get("media.hls.ptsdebug", value, NULL) > 0) {
        if ((sscanf(value, "%d", &ret)) > 0) {
            ALOGW("media.hls.ptsdebug is set to %d\n", ret);
            mDebugLevel = ret;
        }
    }
/////////////
}

AmNuPlayer::Renderer::~Renderer() {
    if (offloadingAudio()) {
        mAudioSink->stop();
        mAudioSink->flush();
        mAudioSink->close();
    }
}

void AmNuPlayer::Renderer::queueBuffer(
        bool audio,
        const sp<ABuffer> &buffer,
        const sp<AMessage> &notifyConsumed) {
    sp<AMessage> msg = new AMessage(kWhatQueueBuffer, this);
    msg->setInt32("queueGeneration", getQueueGeneration(audio));
    msg->setInt32("audio", static_cast<int32_t>(audio));
    msg->setBuffer("buffer", buffer);
    msg->setMessage("notifyConsumed", notifyConsumed);
    msg->post();
}

void AmNuPlayer::Renderer::queueEOS(bool audio, status_t finalResult) {
    CHECK_NE(finalResult, (status_t)OK);

    sp<AMessage> msg = new AMessage(kWhatQueueEOS, this);
    msg->setInt32("queueGeneration", getQueueGeneration(audio));
    msg->setInt32("audio", static_cast<int32_t>(audio));
    msg->setInt32("finalResult", finalResult);
    msg->post();
}

status_t AmNuPlayer::Renderer::setPlaybackSettings(const AudioPlaybackRate &rate) {
    sp<AMessage> msg = new AMessage(kWhatConfigPlayback, this);
    writeToAMessage(msg, rate);
    sp<AMessage> response;
    status_t err = msg->postAndAwaitResponse(&response);
    if (err == OK && response != NULL) {
        CHECK(response->findInt32("err", &err));
    }
    return err;
}

status_t AmNuPlayer::Renderer::onConfigPlayback(const AudioPlaybackRate &rate /* sanitized */) {
    if (rate.mSpeed == 0.f) {
        onPause();
        // don't call audiosink's setPlaybackRate if pausing, as pitch does not
        // have to correspond to the any non-0 speed (e.g old speed). Keep
        // settings nonetheless, using the old speed, in case audiosink changes.
        AudioPlaybackRate newRate = rate;
        newRate.mSpeed = mPlaybackSettings.mSpeed;
        mPlaybackSettings = newRate;
        return OK;
    }

    if (mAudioSink != NULL && mAudioSink->ready()) {
        status_t err = mAudioSink->setPlaybackRate(rate);
        if (err != OK) {
            return err;
        }
    }
    mPlaybackSettings = rate;
    mPlaybackRate = rate.mSpeed;
    mMediaClock->setPlaybackRate(mPlaybackRate);
    return OK;
}

status_t AmNuPlayer::Renderer::getPlaybackSettings(AudioPlaybackRate *rate /* nonnull */) {
    sp<AMessage> msg = new AMessage(kWhatGetPlaybackSettings, this);
    sp<AMessage> response;
    status_t err = msg->postAndAwaitResponse(&response);
    if (err == OK && response != NULL) {
        CHECK(response->findInt32("err", &err));
        if (err == OK) {
            readFromAMessage(response, rate);
        }
    }
    return err;
}

status_t AmNuPlayer::Renderer::onGetPlaybackSettings(AudioPlaybackRate *rate /* nonnull */) {
    if (mAudioSink != NULL && mAudioSink->ready()) {
        status_t err = mAudioSink->getPlaybackRate(rate);
        if (err == OK) {
            if (!isAudioPlaybackRateEqual(*rate, mPlaybackSettings)) {
                ALOGW("correcting mismatch in internal/external playback rate");
            }
            // get playback settings used by audiosink, as it may be
            // slightly off due to audiosink not taking small changes.
            mPlaybackSettings = *rate;
            if (mPaused) {
                rate->mSpeed = 0.f;
            }
        }
        return err;
    }
    *rate = mPlaybackSettings;
    return OK;
}

status_t AmNuPlayer::Renderer::setSyncSettings(const AVSyncSettings &sync, float videoFpsHint) {
    sp<AMessage> msg = new AMessage(kWhatConfigSync, this);
    writeToAMessage(msg, sync, videoFpsHint);
    sp<AMessage> response;
    status_t err = msg->postAndAwaitResponse(&response);
    if (err == OK && response != NULL) {
        CHECK(response->findInt32("err", &err));
    }
    return err;
}

status_t AmNuPlayer::Renderer::onConfigSync(const AVSyncSettings &sync, float videoFpsHint __unused) {
    if (sync.mSource != AVSYNC_SOURCE_DEFAULT) {
        return BAD_VALUE;
    }
    // TODO: support sync sources
    return INVALID_OPERATION;
}

status_t AmNuPlayer::Renderer::getSyncSettings(AVSyncSettings *sync, float *videoFps) {
    sp<AMessage> msg = new AMessage(kWhatGetSyncSettings, this);
    sp<AMessage> response;
    status_t err = msg->postAndAwaitResponse(&response);
    if (err == OK && response != NULL) {
        CHECK(response->findInt32("err", &err));
        if (err == OK) {
            readFromAMessage(response, sync, videoFps);
        }
    }
    return err;
}

status_t AmNuPlayer::Renderer::onGetSyncSettings(
        AVSyncSettings *sync /* nonnull */, float *videoFps /* nonnull */) {
    *sync = mSyncSettings;
    *videoFps = -1.f;
    return OK;
}

void AmNuPlayer::Renderer::flush(bool audio, bool notifyComplete) {
    {
        Mutex::Autolock autoLock(mLock);
        if (audio) {
            mNotifyCompleteAudio |= notifyComplete;
            clearAudioFirstAnchorTime_l();
            ++mAudioQueueGeneration;
            ++mAudioDrainGeneration;
        } else {
            mNotifyCompleteVideo |= notifyComplete;
            ++mVideoQueueGeneration;
            ++mVideoDrainGeneration;
        }

        clearAnchorTime_l();
        mVideoLateByUs = 0;
        mSyncQueues = false;
    }

    sp<AMessage> msg = new AMessage(kWhatFlush, this);
    msg->setInt32("audio", static_cast<int32_t>(audio));
    msg->post();
}

void AmNuPlayer::Renderer::signalTimeDiscontinuity() {
}

void AmNuPlayer::Renderer::signalDisableOffloadAudio() {
    (new AMessage(kWhatDisableOffloadAudio, this))->post();
}

void AmNuPlayer::Renderer::signalEnableOffloadAudio() {
    (new AMessage(kWhatEnableOffloadAudio, this))->post();
}

void AmNuPlayer::Renderer::pause() {
    (new AMessage(kWhatPause, this))->post();
}

void AmNuPlayer::Renderer::resume() {
    (new AMessage(kWhatResume, this))->post();
}

void AmNuPlayer::Renderer::setVideoFrameRate(float fps) {
    sp<AMessage> msg = new AMessage(kWhatSetVideoFrameRate, this);
    msg->setFloat("frame-rate", fps);
    msg->post();
}

// Called on any threads without mLock acquired.
status_t AmNuPlayer::Renderer::getCurrentPosition(int64_t *mediaUs) {
    int64_t C_AudioUs = 0;
    status_t result = mMediaClock->getMediaTime(ALooper::GetNowUs(), &C_AudioUs);
    if (result == OK) {
        *mediaUs = C_AudioUs - mTotalAudioJumpedTimeUs;
        return result;
    }
    return NO_INIT;
    // MediaClock has not started yet. Try to start onDrainAudioQueue: rendering audio at media timeit if possible.
    {
        Mutex::Autolock autoLock(mLock);
        if (mAudioFirstAnchorTimeMediaUs == -1) {
            return result;
        }

        AudioTimestamp ts;
        status_t res = mAudioSink->getTimestamp(ts);
        if (res != OK) {
            return result;
        }

        // AudioSink has rendered some frames.
        int64_t nowUs = ALooper::GetNowUs();
        int64_t nowMediaUs = mAudioSink->getPlayedOutDurationUs(nowUs)
                + mAudioFirstAnchorTimeMediaUs;
        ALOGI("getCurrentPosition:nowMediaUs %lld ,nowUs %lld",nowMediaUs,nowUs);
        mMediaClock->updateAnchor(nowMediaUs, nowUs, -1);
    }
    return mMediaClock->getMediaTime(ALooper::GetNowUs(), mediaUs);;
}

void AmNuPlayer::Renderer::clearAudioFirstAnchorTime_l() {
    mAudioFirstAnchorTimeMediaUs = -1;
    mMediaClock->setStartingTimeMedia(-1);
}

void AmNuPlayer::Renderer::setAudioFirstAnchorTimeIfNeeded_l(int64_t mediaUs) {
    if (mAudioFirstAnchorTimeMediaUs == -1) {
        mAudioFirstAnchorTimeMediaUs = mediaUs;
        mMediaClock->setStartingTimeMedia(mediaUs);
    }
}

void AmNuPlayer::Renderer::clearAnchorTime_l() {
    mMediaClock->clearAnchor();
    mAnchorTimeMediaUs = -1;
    mAnchorNumFramesWritten = -1;
    ALOGI("clearAnchorTime_l");
/*AVsync by Zz*/
    mAvgVideoFrameIntervalUs = -1;
    mLastVideoUs = -1;
    mLastAudioUs = -1;
    mVideoJumpedTimeUs = 0;
    mVideoJumpedTimeUs = 0;
    mAudioTimeJump = false;
    mVideoTimeJump = false;
    mTotalAudioJumpedTimeUs = 0;
    mTotalAudioJumpedTimeUs = 0;

/*AVsync by Zz end*/
}

void AmNuPlayer::Renderer::setVideoLateByUs(int64_t lateUs) {
    Mutex::Autolock autoLock(mLock);
    mVideoLateByUs = lateUs;
}

int64_t AmNuPlayer::Renderer::getVideoLateByUs() {
    Mutex::Autolock autoLock(mLock);
    return mVideoLateByUs;
}

void AmNuPlayer::Renderer::setHasMedia(bool audio) {
    if (audio) {
        mHasAudio = true;
    } else {
        mHasVideo = true;
    }
}

void AmNuPlayer::Renderer::setHasNoMedia(bool audio) {
    if (audio) {
        mHasAudio = false;
        postDrainVideoQueue();
    } else {
        mHasVideo = false;
    }
}

status_t AmNuPlayer::Renderer::openAudioSink(
        const sp<AMessage> &format,
        bool offloadOnly,
        bool hasVideo,
        uint32_t flags,
        bool *isOffloaded) {
    sp<AMessage> msg = new AMessage(kWhatOpenAudioSink, this);
    msg->setMessage("format", format);
    msg->setInt32("offload-only", offloadOnly);
    msg->setInt32("has-video", hasVideo);
    msg->setInt32("flags", flags);

    sp<AMessage> response;
    msg->postAndAwaitResponse(&response);

    int32_t err;
    if (!response->findInt32("err", &err)) {
        err = INVALID_OPERATION;
    } else if (err == OK && isOffloaded != NULL) {
        int32_t offload;
        CHECK(response->findInt32("offload", &offload));
        *isOffloaded = (offload != 0);
    }
    return err;
}

void AmNuPlayer::Renderer::closeAudioSink() {
    sp<AMessage> msg = new AMessage(kWhatCloseAudioSink, this);

    sp<AMessage> response;
    msg->postAndAwaitResponse(&response);
}

void AmNuPlayer::Renderer::onMessageReceived(const sp<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatOpenAudioSink:
        {
            sp<AMessage> format;
            CHECK(msg->findMessage("format", &format));

            int32_t offloadOnly;
            CHECK(msg->findInt32("offload-only", &offloadOnly));

            int32_t hasVideo;
            CHECK(msg->findInt32("has-video", &hasVideo));

            uint32_t flags;
            CHECK(msg->findInt32("flags", (int32_t *)&flags));

            status_t err = onOpenAudioSink(format, offloadOnly, hasVideo, flags);

            sp<AMessage> response = new AMessage;
            response->setInt32("err", err);
            response->setInt32("offload", offloadingAudio());

            sp<AReplyToken> replyID;
            CHECK(msg->senderAwaitsResponse(&replyID));
            response->postReply(replyID);

            break;
        }

        case kWhatCloseAudioSink:
        {
            sp<AReplyToken> replyID;
            CHECK(msg->senderAwaitsResponse(&replyID));

            onCloseAudioSink();

            sp<AMessage> response = new AMessage;
            response->postReply(replyID);
            break;
        }

        case kWhatStopAudioSink:
        {
            mAudioSink->stop();
            break;
        }

        case kWhatDrainAudioQueue:
        {
            mDrainAudioQueuePending = false;

            int32_t generation;
            CHECK(msg->findInt32("drainGeneration", &generation));
            if (generation != getDrainGeneration(true /* audio */)) {
                break;
            }

            if (onDrainAudioQueue()) {
                uint32_t numFramesPlayed;
                CHECK_EQ(mAudioSink->getPosition(&numFramesPlayed),
                         (status_t)OK);

                uint32_t numFramesPendingPlayout =
                    mNumFramesWritten - numFramesPlayed;

                // This is how long the audio sink will have data to
                // play back.
                int64_t delayUs =
                    mAudioSink->msecsPerFrame()
                        * numFramesPendingPlayout * 1000ll;
                if (mPlaybackRate > 1.0f) {
                    delayUs /= mPlaybackRate;
                }

                // Let's give it more data after about half that time
                // has elapsed.
                delayUs /= 2;
                // check the buffer size to estimate maximum delay permitted.
                const int64_t maxDrainDelayUs = std::max(
                        mAudioSink->getBufferDurationInUs(), (int64_t)500000 /* half second */);
                ALOGD_IF(delayUs > maxDrainDelayUs, "postDrainAudioQueue long delay: %lld > %lld",
                        (long long)delayUs, (long long)maxDrainDelayUs);
                Mutex::Autolock autoLock(mLock);
                postDrainAudioQueue_l(delayUs);
            }
            break;
        }

        case kWhatDrainVideoQueue:
        {
            int32_t generation;
            CHECK(msg->findInt32("drainGeneration", &generation));
            if (generation != getDrainGeneration(false /* audio */)) {
                break;
            }

            mDrainVideoQueuePending = false;

            onDrainVideoQueue();

            postDrainVideoQueue();
            break;
        }

        case kWhatPostDrainVideoQueue:
        {
            int32_t generation;
            CHECK(msg->findInt32("drainGeneration", &generation));
            if (generation != getDrainGeneration(false /* audio */)) {
                break;
            }

            mDrainVideoQueuePending = false;
            postDrainVideoQueue();
            break;
        }

        case kWhatQueueBuffer:
        {
            onQueueBuffer(msg);
            break;
        }

        case kWhatQueueEOS:
        {
            onQueueEOS(msg);
            break;
        }

        case kWhatEOS:
        {
            int32_t generation;
            CHECK(msg->findInt32("audioEOSGeneration", &generation));
            if (generation != mAudioEOSGeneration) {
                break;
            }
            status_t finalResult;
            CHECK(msg->findInt32("finalResult", &finalResult));
            notifyEOS(true /* audio */, finalResult);
            break;
        }

        case kWhatConfigPlayback:
        {
            sp<AReplyToken> replyID;
            CHECK(msg->senderAwaitsResponse(&replyID));
            AudioPlaybackRate rate;
            readFromAMessage(msg, &rate);
            status_t err = onConfigPlayback(rate);
            sp<AMessage> response = new AMessage;
            response->setInt32("err", err);
            response->postReply(replyID);
            break;
        }

        case kWhatGetPlaybackSettings:
        {
            sp<AReplyToken> replyID;
            CHECK(msg->senderAwaitsResponse(&replyID));
            AudioPlaybackRate rate = AUDIO_PLAYBACK_RATE_DEFAULT;
            status_t err = onGetPlaybackSettings(&rate);
            sp<AMessage> response = new AMessage;
            if (err == OK) {
                writeToAMessage(response, rate);
            }
            response->setInt32("err", err);
            response->postReply(replyID);
            break;
        }

        case kWhatConfigSync:
        {
            sp<AReplyToken> replyID;
            CHECK(msg->senderAwaitsResponse(&replyID));
            AVSyncSettings sync;
            float videoFpsHint;
            readFromAMessage(msg, &sync, &videoFpsHint);
            status_t err = onConfigSync(sync, videoFpsHint);
            sp<AMessage> response = new AMessage;
            response->setInt32("err", err);
            response->postReply(replyID);
            break;
        }

        case kWhatGetSyncSettings:
        {
            sp<AReplyToken> replyID;
            CHECK(msg->senderAwaitsResponse(&replyID));

            ALOGV("kWhatGetSyncSettings");
            AVSyncSettings sync;
            float videoFps = -1.f;
            status_t err = onGetSyncSettings(&sync, &videoFps);
            sp<AMessage> response = new AMessage;
            if (err == OK) {
                writeToAMessage(response, sync, videoFps);
            }
            response->setInt32("err", err);
            response->postReply(replyID);
            break;
        }

        case kWhatFlush:
        {
            onFlush(msg);
            break;
        }

        case kWhatDisableOffloadAudio:
        {
            onDisableOffloadAudio();
            break;
        }

        case kWhatEnableOffloadAudio:
        {
            onEnableOffloadAudio();
            break;
        }

        case kWhatPause:
        {
            onPause();
            break;
        }

        case kWhatResume:
        {
            onResume();
            break;
        }

        case kWhatSetVideoFrameRate:
        {
            float fps;
            CHECK(msg->findFloat("frame-rate", &fps));
            onSetVideoFrameRate(fps);
            break;
        }

        case kWhatAudioTearDown:
        {
            int32_t reason;
            CHECK(msg->findInt32("reason", &reason));

            onAudioTearDown((AudioTearDownReason)reason);
            break;
        }

        case kWhatAudioOffloadPauseTimeout:
        {
            int32_t generation;
            CHECK(msg->findInt32("drainGeneration", &generation));
            if (generation != mAudioOffloadPauseTimeoutGeneration) {
                break;
            }
            ALOGV("Audio Offload tear down due to pause timeout.");
            onAudioTearDown(kDueToTimeout);
            mWakeLock->release();
            break;
        }

        default:
            TRESPASS();
            break;
    }
}

void AmNuPlayer::Renderer::postDrainAudioQueue_l(int64_t delayUs) {
    if (mDrainAudioQueuePending || mSyncQueues || mUseAudioCallback) {
        return;
    }

    if (mAudioQueue.empty()) {
        return;
    }

    // FIXME: if paused, wait until AudioTrack stop() is complete before delivering data.
    if (mPaused) {
        const int64_t diffUs = mPauseDrainAudioAllowedUs - ALooper::GetNowUs();
        if (diffUs > delayUs) {
            delayUs = diffUs;
        }
    }

    mDrainAudioQueuePending = true;
    sp<AMessage> msg = new AMessage(kWhatDrainAudioQueue, this);
    msg->setInt32("drainGeneration", mAudioDrainGeneration);
    msg->post(delayUs);
}

void AmNuPlayer::Renderer::prepareForMediaRenderingStart_l() {
    mAudioRenderingStartGeneration = mAudioDrainGeneration;
    mVideoRenderingStartGeneration = mVideoDrainGeneration;
    mRenderingDataDelivered = false;
}

void AmNuPlayer::Renderer::notifyIfMediaRenderingStarted_l() {
    if (mVideoRenderingStartGeneration == mVideoDrainGeneration &&
        mAudioRenderingStartGeneration == mAudioDrainGeneration) {
        mRenderingDataDelivered = true;
        if (mPaused) {
            return;
        }
        mVideoRenderingStartGeneration = -1;
        mAudioRenderingStartGeneration = -1;

        sp<AMessage> notify = mNotify->dup();
        notify->setInt32("what", kWhatMediaRenderingStart);
        notify->post();
    }
}

// static
size_t AmNuPlayer::Renderer::AudioSinkCallback(
        MediaPlayerBase::AudioSink * /* audioSink */,
        void *buffer,
        size_t size,
        void *cookie,
        MediaPlayerBase::AudioSink::cb_event_t event) {
    AmNuPlayer::Renderer *me = (AmNuPlayer::Renderer *)cookie;

    switch (event) {
        case MediaPlayerBase::AudioSink::CB_EVENT_FILL_BUFFER:
        {
            return me->fillAudioBuffer(buffer, size);
            break;
        }

        case MediaPlayerBase::AudioSink::CB_EVENT_STREAM_END:
        {
            ALOGV("AudioSink::CB_EVENT_STREAM_END");
            me->notifyEOS(true /* audio */, ERROR_END_OF_STREAM);
            break;
        }

        case MediaPlayerBase::AudioSink::CB_EVENT_TEAR_DOWN:
        {
            ALOGV("AudioSink::CB_EVENT_TEAR_DOWN");
            me->notifyAudioTearDown(kDueToError);
            break;
        }
    }

    return 0;
}

size_t AmNuPlayer::Renderer::fillAudioBuffer(void *buffer, size_t size) {
    Mutex::Autolock autoLock(mLock);

    if (!mUseAudioCallback) {
        return 0;
    }

    bool hasEOS = false;

    size_t sizeCopied = 0;
    bool firstEntry = true;
    int64_t mediaTimeUs;
    while (sizeCopied < size && !mAudioQueue.empty() && !mQueueInitial) {
        if (entry == NULL) {
            entry = &*mAudioQueue.begin();
            entry->mOffset = 0;
        }
        if (entry->mBuffer == NULL) { // EOS
            hasEOS = true;
            mAudioQueue.erase(mAudioQueue.begin());
            break;
        }

        if (firstEntry && entry->mOffset == 0) {
            firstEntry = false;
            CHECK(entry->mBuffer->meta()->findInt64("timeUs", &mediaTimeUs));
            ALOGV("fillAudioBuffer: rendering audio at media time %.2f secs", mediaTimeUs / 1E6);
            setAudioFirstAnchorTimeIfNeeded_l(mediaTimeUs);
        }

        size_t copy = entry->mBuffer->size() - entry->mOffset;
        size_t sizeRemaining = size - sizeCopied;
        if (copy > sizeRemaining) {
            copy = sizeRemaining;
        }
        //use for audio passthrough
        size_t outlen_raw = 0,outlen_pcm = 0;
        size_t datalength = entry->mBuffer->size();
        if (copy > 8 && mCurrentPcmInfo.mFormat != AUDIO_FORMAT_PCM_16_BIT) {
            unsigned char *pbuf = entry->mBuffer->data() /*+ entry->mOffset*/;
            memcpy(&outlen_pcm,pbuf,4);
            if (outlen_pcm + 8 <= datalength)
                memcpy(&outlen_raw,pbuf+4+outlen_pcm,4);

            if (outlen_raw >0 && (outlen_raw + outlen_pcm + 8 != datalength) )
                ALOGI("packet data not right");
            //ALOGI("copy:%d,outlen_pcm:%d,outlen_raw:%d",copy,outlen_pcm,outlen_raw);
            if (copy >= outlen_raw &&  entry->mOffset == 0) {
                memcpy((char *)buffer + sizeCopied,
                       entry->mBuffer->data() + entry->mOffset+8+outlen_pcm,
                       outlen_raw);
                copy = outlen_raw ;
            } else {
               if (outlen_raw - entry->mOffset >= copy) {
                  memcpy((char *)buffer + sizeCopied,
                        entry->mBuffer->data() + entry->mOffset+8+outlen_pcm,
                        copy);

                }else {
                  memcpy((char *)buffer + sizeCopied,
                        entry->mBuffer->data() + entry->mOffset+8+outlen_pcm,
                        outlen_raw - entry->mOffset);
                  copy = outlen_raw - entry->mOffset;
               }
            }

            entry->mOffset += copy;
            if ((entry->mOffset + outlen_pcm  + 8) >= entry->mBuffer->size() ||  entry->mBuffer->size() == 0) {
                entry->mNotifyConsumed->post();
                mAudioQueue.erase(mAudioQueue.begin());
                entry = NULL;
            }
        }else {
           memcpy((char *)buffer + sizeCopied,
           entry->mBuffer->data() + entry->mOffset,
           copy);
           entry->mOffset += copy;
           if (entry->mOffset == entry->mBuffer->size()) {
               entry->mNotifyConsumed->post();
               mAudioQueue.erase(mAudioQueue.begin());
               entry = NULL;
           }

       }
        sizeCopied += copy;

        notifyIfMediaRenderingStarted_l();

    }

    if (mAudioFirstAnchorTimeMediaUs >= 0) {
        int64_t nowUs = ALooper::GetNowUs();
        int64_t nowMediaUs =
            mAudioFirstAnchorTimeMediaUs + mAudioSink->getPlayedOutDurationUs(nowUs);
        // we don't know how much data we are queueing for offloaded tracks.
        mMediaClock->updateAnchor(nowMediaUs, nowUs, INT64_MAX);
        mAnchorTimeMediaUs = mediaTimeUs;
    }

    // for non-offloaded audio, we need to compute the frames written because
    // there is no EVENT_STREAM_END notification. The frames written gives
    // an estimate on the pending played out duration.
    if (!offloadingAudio()) {
        mNumFramesWritten += sizeCopied / mAudioSink->frameSize();
    }

    if (hasEOS) {
        // As there is currently no EVENT_STREAM_END callback notification for
        // non-offloaded audio tracks, we need to post the EOS ourselves.
        if (!offloadingAudio()) {
            int64_t postEOSDelayUs = 0;
            if (mAudioSink->needsTrailingPadding()) {
                postEOSDelayUs = getPendingAudioPlayoutDurationUs(ALooper::GetNowUs());
            }
            ALOGV("fillAudioBuffer: notifyEOS "
                    "mNumFramesWritten:%u  finalResult:%d  postEOSDelay:%lld",
                    mNumFramesWritten, entry->mFinalResult, (long long)postEOSDelayUs);
            notifyEOS(true /* audio */, entry->mFinalResult, postEOSDelayUs);
            //if kWhatStopAudioSink is at the head of notifyEOS, mAudioSink->getPlayedOutDurationUs will be zero
            //and getPendingAudioPlayoutDurationUs will return writtenAudioDurationUs.
            (new AMessage(kWhatStopAudioSink, this))->post();
        }
    }
    return sizeCopied;
}

void AmNuPlayer::Renderer::drainAudioQueueUntilLastEOS() {
    List<QueueEntry>::iterator it = mAudioQueue.begin(), itEOS = it;
    bool foundEOS = false;
    while (it != mAudioQueue.end()) {
        int32_t eos;
        QueueEntry *entry = &*it++;
        if (entry->mBuffer == NULL
                || (entry->mNotifyConsumed->findInt32("eos", &eos) && eos != 0)) {
            itEOS = it;
            foundEOS = true;
        }
    }

    if (foundEOS) {
        // post all replies before EOS and drop the samples
        for (it = mAudioQueue.begin(); it != itEOS; it++) {
            if (it->mBuffer == NULL) {
                // delay doesn't matter as we don't even have an AudioTrack
                notifyEOS(true /* audio */, it->mFinalResult);
            } else {
                it->mNotifyConsumed->post();
            }
        }
        mAudioQueue.erase(mAudioQueue.begin(), itEOS);
    }
}

bool AmNuPlayer::Renderer::onDrainAudioQueue() {
    // do not drain audio during teardown as queued buffers may be invalid.
    if (mAudioTornDown) {
        return false;
    }
    // TODO: This call to getPosition checks if AudioTrack has been created
    // in AudioSink before draining audio. If AudioTrack doesn't exist, then
    // CHECKs on getPosition will fail.
    // We still need to figure out why AudioTrack is not created when
    // this function is called. One possible reason could be leftover
    // audio. Another possible place is to check whether decoder
    // has received INFO_FORMAT_CHANGED as the first buffer since
    // AudioSink is opened there, and possible interactions with flush
    // immediately after start. Investigate error message
    // "vorbis_dsp_synthesis returned -135", along with RTSP.
    uint32_t numFramesPlayed;
    if (mAudioSink->getPosition(&numFramesPlayed) != OK) {
        // When getPosition fails, renderer will not reschedule the draining
        // unless new samples are queued.
        // If we have pending EOS (or "eos" marker for discontinuities), we need
        // to post these now as NuPlayerDecoder might be waiting for it.
        drainAudioQueueUntilLastEOS();

        ALOGW("onDrainAudioQueue(): audio sink is not ready");
        return false;
    }

#if 0
    ssize_t numFramesAvailableToWrite =
        mAudioSink->frameCount() - (mNumFramesWritten - numFramesPlayed);

    if (numFramesAvailableToWrite == mAudioSink->frameCount()) {
        ALOGI("audio sink underrun");
    } else {
        ALOGV("audio queue has %d frames left to play",
             mAudioSink->frameCount() - numFramesAvailableToWrite);
    }
#endif

    uint32_t prevFramesWritten = mNumFramesWritten;
    while (!mAudioQueue.empty()) {
        QueueEntry *entry = &*mAudioQueue.begin();

        mLastAudioBufferDrained = entry->mBufferOrdinal;

        if (entry->mBuffer == NULL) {
            // EOS
            int64_t postEOSDelayUs = 0;
            if (mAudioSink->needsTrailingPadding()) {
                postEOSDelayUs = getPendingAudioPlayoutDurationUs(ALooper::GetNowUs());
            }
            notifyEOS(true /* audio */, entry->mFinalResult, postEOSDelayUs);
            mLastAudioMediaTimeUs = getDurationUsIfPlayedAtSampleRate(mNumFramesWritten);

            mAudioQueue.erase(mAudioQueue.begin());
            entry = NULL;
            if (mAudioSink->needsTrailingPadding()) {
                // If we're not in gapless playback (i.e. through setNextPlayer), we
                // need to stop the track here, because that will play out the last
                // little bit at the end of the file. Otherwise short files won't play.
                mAudioSink->stop();
                mNumFramesWritten = 0;
            }
            return false;
        }

        // ignore 0-sized buffer which could be EOS marker with no data
        if (entry->mOffset == 0 && entry->mBuffer->size() > 0) {
            int64_t mediaTimeUs;
            CHECK(entry->mBuffer->meta()->findInt64("timeUs", &mediaTimeUs));
            //ALOGV("onDrainAudioQueue: rendering audio at media time %.2f secs",
            //        mediaTimeUs / 1E6);
            onNewAudioMediaTime(mediaTimeUs);
        }

        size_t copy = entry->mBuffer->size() - entry->mOffset;

        ssize_t written = mAudioSink->write(entry->mBuffer->data() + entry->mOffset,
                                            copy, false /* blocking */);
        if (written < 0) {
            // An error in AudioSink write. Perhaps the AudioSink was not properly opened.
            if (written == WOULD_BLOCK) {
                //ALOGV("AudioSink write would block when writing %zu bytes", copy);
            } else {
                ALOGE("AudioSink write error(%zd) when writing %zu bytes", written, copy);
                // This can only happen when AudioSink was opened with doNotReconnect flag set to
                // true, in which case the AmNuPlayer will handle the reconnect.
                notifyAudioTearDown(kDueToError);
            }
            break;
        }

        entry->mOffset += written;
        size_t remainder = entry->mBuffer->size() - entry->mOffset;
        if ((ssize_t)remainder < mAudioSink->frameSize()) {
            if (remainder > 0) {
                ALOGW("Corrupted audio buffer has fractional frames, discarding %zu bytes.",
                        remainder);
                entry->mOffset += remainder;
                copy -= remainder;
            }

            entry->mNotifyConsumed->post();
            mAudioQueue.erase(mAudioQueue.begin());

            entry = NULL;
        }

        size_t copiedFrames = written / mAudioSink->frameSize();
        mNumFramesWritten += copiedFrames;

        {
            Mutex::Autolock autoLock(mLock);
            int64_t maxTimeMedia;
            maxTimeMedia =
                mAnchorTimeMediaUs +
                        (int64_t)(max((long long)mNumFramesWritten - mAnchorNumFramesWritten, 0LL)
                                * 1000LL * mAudioSink->msecsPerFrame());
            mMediaClock->updateMaxTimeMedia(maxTimeMedia);

            notifyIfMediaRenderingStarted_l();
        }

        if (written != (ssize_t)copy) {
            // A short count was received from AudioSink::write()
            //
            // AudioSink write is called in non-blocking mode.
            // It may return with a short count when:
            //
            // 1) Size to be copied is not a multiple of the frame size. Fractional frames are
            //    discarded.
            // 2) The data to be copied exceeds the available buffer in AudioSink.
            // 3) An error occurs and data has been partially copied to the buffer in AudioSink.
            // 4) AudioSink is an AudioCache for data retrieval, and the AudioCache is exceeded.

            // (Case 1)
            // Must be a multiple of the frame size.  If it is not a multiple of a frame size, it
            // needs to fail, as we should not carry over fractional frames between calls.
            CHECK_EQ(copy % mAudioSink->frameSize(), 0);

            // (Case 2, 3, 4)
            // Return early to the caller.
            // Beware of calling immediately again as this may busy-loop if you are not careful.
            //ALOGV("AudioSink write short frame count %zd < %zu", written, copy);
            break;
        }
    }

    // calculate whether we need to reschedule another write.
    bool reschedule = !mAudioQueue.empty()
            && (!mPaused
                || prevFramesWritten != mNumFramesWritten); // permit pause to fill buffers
    //ALOGD("reschedule:%d  empty:%d  mPaused:%d  prevFramesWritten:%u  mNumFramesWritten:%u",
    //        reschedule, mAudioQueue.empty(), mPaused, prevFramesWritten, mNumFramesWritten);
    return reschedule;
}

int64_t AmNuPlayer::Renderer::getDurationUsIfPlayedAtSampleRate(uint32_t numFrames) {
    int32_t sampleRate = offloadingAudio() ?
            mCurrentOffloadInfo.sample_rate : mCurrentPcmInfo.mSampleRate;
    if (sampleRate == 0) {
        ALOGE("sampleRate is 0 in %s mode", offloadingAudio() ? "offload" : "non-offload");
        return 0;
    }
    // TODO: remove the (int32_t) casting below as it may overflow at 12.4 hours.
    return (int64_t)((int32_t)numFrames * 1000000LL / sampleRate);
}

// Calculate duration of pending samples if played at normal rate (i.e., 1.0).
int64_t AmNuPlayer::Renderer::getPendingAudioPlayoutDurationUs(int64_t nowUs) {
#if 1 //12.4 hours.
    int64_t writtenAudioDurationUs = getDurationUsIfPlayedAtSampleRate(mNumFramesWritten);
    if (mUseVirtualAudioSink) {
        int64_t nowUs = ALooper::GetNowUs();
        int64_t mediaUs;
        if (mMediaClock->getMediaTime(nowUs, &mediaUs) != OK) {
            return 0ll;
        } else {
            return writtenAudioDurationUs - (mediaUs - mAudioFirstAnchorTimeMediaUs);
        }
    }
    //when playing , hdmi device pull  and insert audiosink maybe could not get correct pts ,so
    //the PlayedOutDurationUs will be 0;it is not right, so we make PendingAudioPlayoutDurationUs
    //0 here.
    if (mAudioSink->getPlayedOutDurationUs(nowUs)) {
        return writtenAudioDurationUs - mAudioSink->getPlayedOutDurationUs(nowUs);
    } else {
        return 0ll;
    }
#endif
    uint32_t numFramesPlayed;
    mAudioSink->getPosition(&numFramesPlayed);
    uint32_t numFramesPendingPlayout = mNumFramesWritten - numFramesPlayed;
    int64_t realTimeOffsetUs = (mAudioSink->latency() / 2 +
            numFramesPendingPlayout * mAudioSink->msecsPerFrame()) * 1000ll;
    return realTimeOffsetUs;
}

int64_t AmNuPlayer::Renderer::getRealTimeUs(int64_t mediaTimeUs, int64_t nowUs) {
    int64_t realUs;
    if (mMediaClock->getRealTimeFor(mediaTimeUs, &realUs) != OK) {
        // If failed to get current position, e.g. due to audio clock is
        // not ready, then just play out video immediately without delay.
        return nowUs;
    }
    return realUs;
}

void AmNuPlayer::Renderer::onNewAudioMediaTime(int64_t mediaTimeUs) {
    Mutex::Autolock autoLock(mLock);
    // TRICKY: vorbis decoder generates multiple frames with the same
    // timestamp, so only update on the first frame with a given timestamp
    if (mediaTimeUs == mAnchorTimeMediaUs) {
        return;
    }
    setAudioFirstAnchorTimeIfNeeded_l(mediaTimeUs);

    // mNextAudioClockUpdateTimeUs is -1 if we're waiting for audio sink to start
    if (mNextAudioClockUpdateTimeUs == -1) {
        AudioTimestamp ts;
        if (mAudioSink->getTimestamp(ts) == OK && ts.mPosition > 0) {
            mNextAudioClockUpdateTimeUs = 0; // start our clock updates
        }
    }
    int64_t nowUs = ALooper::GetNowUs();
    if (mNextAudioClockUpdateTimeUs >= 0) {
        if (nowUs >= mNextAudioClockUpdateTimeUs) {
            int64_t nowMediaUs = mediaTimeUs - getPendingAudioPlayoutDurationUs(nowUs);
            mMediaClock->updateAnchor(nowMediaUs, nowUs, mediaTimeUs);
            mUseVirtualAudioSink = false;
            mNextAudioClockUpdateTimeUs = nowUs + kMinimumAudioClockUpdatePeriodUs;
        }
    } else {
        int64_t unused;
        if ((mMediaClock->getMediaTime(nowUs, &unused) != OK)
                && (getDurationUsIfPlayedAtSampleRate(mNumFramesWritten)
                        > kMaxAllowedAudioSinkDelayUs)) {
            // Enough data has been sent to AudioSink, but AudioSink has not rendered
            // any data yet. Something is wrong with AudioSink, e.g., the device is not
            // connected to audio out.
            // Switch to system clock. This essentially creates a virtual AudioSink with
            // initial latenty of getDurationUsIfPlayedAtSampleRate(mNumFramesWritten).
            // This virtual AudioSink renders audio data starting from the very first sample
            // and it's paced by system clock.
            ALOGW("AudioSink stuck. ARE YOU CONNECTED TO AUDIO OUT? Switching to system clock.");
            mMediaClock->updateAnchor(mAudioFirstAnchorTimeMediaUs, nowUs, mediaTimeUs);
            mUseVirtualAudioSink = true;
        }
    }
    mAnchorNumFramesWritten = mNumFramesWritten;
    mAnchorTimeMediaUs = mediaTimeUs;
}

// Called without mLock acquired.
void AmNuPlayer::Renderer::postDrainVideoQueue() {
    if (mDrainVideoQueuePending
            || getSyncQueues()
            || (mPaused && mVideoSampleReceived)) {
        return;
    }

    if (mVideoQueue.empty()) {
        return;
    }

    QueueEntry &entry = *mVideoQueue.begin();

    sp<AMessage> msg = new AMessage(kWhatDrainVideoQueue, this);
    msg->setInt32("drainGeneration", getDrainGeneration(false /* audio */));

    if (entry.mBuffer == NULL) {
        // EOS doesn't carry a timestamp.
        msg->post();
        mDrainVideoQueuePending = true;
        return;
    }

    bool needRepostDrainVideoQueue = false;
    int64_t delayUs;
    int64_t nowUs = ALooper::GetNowUs();
    int64_t realTimeUs;
    int64_t mediaTimeUs;
    if (mFlags & FLAG_REAL_TIME) {
        CHECK(entry.mBuffer->meta()->findInt64("timeUs", &mediaTimeUs));
        realTimeUs = mediaTimeUs;
    } else {
        CHECK(entry.mBuffer->meta()->findInt64("timeUs", &mediaTimeUs));

        {
            Mutex::Autolock autoLock(mLock);
            if (mAnchorTimeMediaUs < 0) {
                if (!mHasAudio) {
                    mMediaClock->updateAnchor(mediaTimeUs, nowUs, mediaTimeUs);
                    mAnchorTimeMediaUs = mediaTimeUs;
                }
                realTimeUs = nowUs + 180000;//add the average realtime diff before audio init ok, whick will out video smoothly(set omx_pts) befor audio init ok
            } else if (!mVideoSampleReceived) {
                // Always render the first video frame.
                realTimeUs = nowUs;
            } else if (mAudioFirstAnchorTimeMediaUs < 0
                || mMediaClock->getRealTimeFor(mediaTimeUs, &realTimeUs) == OK) {
                realTimeUs = getRealTimeUs(mediaTimeUs, nowUs);
            } else if (mediaTimeUs - mAudioFirstAnchorTimeMediaUs >= 0) {
                needRepostDrainVideoQueue = true;
                realTimeUs = nowUs;
            } else {
                if (/*mHasAudio*/0) {// if realtime uninitialized will retry in 10ms
                    msg->setWhat(kWhatPostDrainVideoQueue);
                    msg->post(10000);
                    mDrainVideoQueuePending = true;
                    mVideoScheduler->restart();
                    ALOGI("uninitialized media clock, retrying in 10ms");
                    return;
                } else {
                    realTimeUs = nowUs;
                }
            }
        }

        if (!mHasAudio) {
            // smooth out videos >= 10fps
            mMediaClock->updateMaxTimeMedia(mediaTimeUs + 100000);
        }

        // Heuristics to handle situation when media time changed without a
        // discontinuity. If we have not drained an audio buffer that was
        // received after this buffer, repost in 10 msec. Otherwise repost
        // in 500 msec.
        if (mHasAudio && mAudioEOS)
            delayUs = 0;
        else
            delayUs = realTimeUs - nowUs;

        int64_t postDelayUs = -1;
        if (delayUs > 500000 && !mVideoTimeJump && !mAudioTimeJump) {
            postDelayUs = 500000;
            if (mHasAudio && (mLastAudioBufferDrained - entry.mBufferOrdinal) <= 0) {
                postDelayUs = 10000;
            }
        } else if (needRepostDrainVideoQueue && !mVideoTimeJump && !mAudioTimeJump) {
            // CHECK(mPlaybackRate > 0);
            // CHECK(mAudioFirstAnchorTimeMediaUs >= 0);
            // CHECK(mediaTimeUs - mAudioFirstAnchorTimeMediaUs >= 0);
            postDelayUs = mediaTimeUs - mAudioFirstAnchorTimeMediaUs;
            postDelayUs /= mPlaybackRate;
        }

        /*if (postDelayUs >= 0) {
            msg->setWhat(kWhatPostDrainVideoQueue);
            msg->post(postDelayUs);
            mVideoScheduler->restart();
            ALOGI("possible video time jump of %dms or uninitialized media clock, retrying in %dms",
                    (int)(delayUs / 1000), (int)(postDelayUs / 1000));
            mDrainVideoQueuePending = true;
            return;
        }*/

        if (mNextAudioClockUpdateTimeUs < 0 && mHasAudio &&
            mMediaClock->getRealTimeFor(mediaTimeUs, &realTimeUs) != OK) {
            if (!mFirstVideoRealTime) {
                mFirstVideoRealTime = realTimeUs;
                mVideoRealtime = mFirstVideoRealTime;//record the real time for the first video, the next will update based the first realtime
            } else {
                if (mLastestVideoFrameIntervalUs > 0)
                    mVideoRealtime += mLastestVideoFrameIntervalUs;
                else
                    mVideoRealtime += 16666;//first use 60fps instead, next will use mLastestVideoFrameIntervalUs(the real differ)
            }
            entry.mBuffer->meta()->setInt64("RealTimeUs", mVideoRealtime);
            ALOGI("realtime=%lld, mLastestVideoFrameIntervalUs=%lld\n",
                mVideoRealtime, mLastestVideoFrameIntervalUs);
            msg->post(0);
            mDrainVideoQueuePending = true;
            return;
       }

    }
    if (mVideoTimeJump || mAudioTimeJump ) {
        if (mSmootOutNum == 0)
            mLastNoJumpVideoFrameUs = mLastVideoDrainRealTimeUs;
        mSmootOutNum++;
        /*smooth out video frame...*/
        if (mLastNoJumpVideoFrameUs > 0 &&
            mAvgVideoFrameIntervalUs > 0 &&
            llabs(nowUs - mLastVideoDrainRealTimeUs) < 300*1000) {/*not too long for pause or others..*/
                realTimeUs = mLastNoJumpVideoFrameUs + mSmootOutNum * mAvgVideoFrameIntervalUs;
            PTS_LOGD("mLastVideoDrainRealTimeUs1 =%" PRId64 ",mAvgVideoFrameIntervalUs=%" PRId64 ",nowUs=%" PRId64 "\n",
                mLastVideoDrainRealTimeUs, mAvgVideoFrameIntervalUs, nowUs);
        } else {
            realTimeUs = nowUs;
            PTS_LOGD("mLastVideoDrainRealTimeUs2 =%" PRId64 ",mAvgVideoFrameIntervalUs=%" PRId64 ",nowUs=%" PRId64 "\n",
                mLastVideoDrainRealTimeUs, mAvgVideoFrameIntervalUs, nowUs);
        }
        PTS_LOGD("smooth out video frame on jump3 V:%d A:%d delayUs:%d uS",
          mVideoTimeJump, mAudioTimeJump, (int)(realTimeUs - nowUs));
    } else {
        mSmootOutNum = 0;
    }

    realTimeUs = mVideoScheduler->schedule(realTimeUs * 1000) / 1000;
    int64_t twoVsyncsUs = 2 * (mVideoScheduler->getVsyncPeriod() / 1000);
    if (mHasAudio && mAudioEOS) {
       ALOGI("mAudioQueue empty ,delayUs 0");
       delayUs = 0;
    } else {
        delayUs = realTimeUs - nowUs;
    }

    PTS_LOGDD("realTimeUs=%" PRId64 ",system=%" PRId64 ",delay=%" PRId64 "\n",
        realTimeUs, nowUs, delayUs);
    entry.mBuffer->meta()->setInt64("RealTimeUs", realTimeUs);
    ALOGW_IF(delayUs > 500000, "unusually high delayUs: %" PRId64, delayUs);
    // post 2 display refreshes before rendering is due
    msg->post(delayUs > twoVsyncsUs ? delayUs - twoVsyncsUs : 0);

    mDrainVideoQueuePending = true;
}

void AmNuPlayer::Renderer::onDrainVideoQueue() {
    if (mVideoQueue.empty()) {
        return;
    }

    QueueEntry *entry = &*mVideoQueue.begin();

    if (entry->mBuffer == NULL) {
        // EOS

        notifyEOS(false /* audio */, entry->mFinalResult);

        mVideoQueue.erase(mVideoQueue.begin());
        entry = NULL;

        setVideoLateByUs(0);
        return;
    }

    mVideoFrameOutNum ++;
    int64_t nowUs = ALooper::GetNowUs();
    int64_t realTimeUs;
    int64_t mediaTimeUs = -1;
    if (mFlags & FLAG_REAL_TIME) {
        CHECK(entry->mBuffer->meta()->findInt64("timeUs", &realTimeUs));
    } else {
        CHECK(entry->mBuffer->meta()->findInt64("timeUs", &mediaTimeUs));
        if (!entry->mBuffer->meta()->findInt64("RealTimeUs", &realTimeUs)) {
            nowUs = ALooper::GetNowUs();
            realTimeUs = getRealTimeUs(mediaTimeUs, nowUs);
        }
        mLastVideoDrainTimeUs = mediaTimeUs;
    }

    bool tooLate = false;
    bool render = true;
    if (!mPaused) {
        mLastVideoDrainRealTimeUs = nowUs;
        setVideoLateByUs(nowUs - realTimeUs);
        tooLate = (mVideoLateByUs > 40000);
        render = !tooLate;

        if (tooLate) {
            ALOGV("video late by %lld us (%.2f secs)",
                 (long long)mVideoLateByUs, mVideoLateByUs / 1E6);
            if (mVideoLateByUs < 1000000) {
                render = mVideoFrameOutNum & 1;
            } else if (mVideoLateByUs < 2000000) {
                render = ((mVideoFrameOutNum & 3) == 1);
            } else {
                render = ((mVideoFrameOutNum & 7) == 1);
            }
        } else {
            int64_t mediaUs = 0;
            mMediaClock->getMediaTime(realTimeUs, &mediaUs);
            //ALOGV("rendering video at media time %.2f secs",
            //        (mFlags & FLAG_REAL_TIME ? realTimeUs :
            //        mediaUs) / 1E6);

            if (!(mFlags & FLAG_REAL_TIME)
                    && mLastAudioMediaTimeUs != -1
                    && mediaTimeUs > mLastAudioMediaTimeUs) {
                // If audio ends before video, video continues to drive media clock.
                // Also smooth out videos >= 10fps.
                mMediaClock->updateMaxTimeMedia(mediaTimeUs + 100000);
            }
        }
    } else {
        setVideoLateByUs(0);
        if (!mVideoSampleReceived && !mHasAudio) {
            // This will ensure that the first frame after a flush won't be used as anchor
            // when renderer is in paused state, because resume can happen any time after seek.
            Mutex::Autolock autoLock(mLock);
            clearAnchorTime_l();
        }
    }

    // Always render the first video frame while keeping stats on A/V sync.
    if (!mVideoSampleReceived) {
        realTimeUs = nowUs;
        tooLate = false;
    }

    entry->mNotifyConsumed->setInt64("timestampNs", realTimeUs * 1000ll);
    entry->mNotifyConsumed->setInt32("render", render);
    entry->mNotifyConsumed->post();
    mVideoQueue.erase(mVideoQueue.begin());
    entry = NULL;

    mVideoSampleReceived = true;

    if (!mPaused) {
        if (!mVideoRenderingStarted) {
            mVideoRenderingStarted = true;
            notifyVideoRenderingStart();
        }
        Mutex::Autolock autoLock(mLock);
        notifyIfMediaRenderingStarted_l();
    }
    if (!mHasAudio && mVideoTimeJump) {
        mMediaClock->updateAnchor(mediaTimeUs, nowUs, mediaTimeUs);
    }
}

void AmNuPlayer::Renderer::notifyVideoRenderingStart() {
    sp<AMessage> notify = mNotify->dup();
    notify->setInt32("what", kWhatVideoRenderingStart);
    notify->post();
}

void AmNuPlayer::Renderer::notifyEOS(bool audio, status_t finalResult, int64_t delayUs) {
    if (audio)
        mAudioEOS = true;
    if (audio && delayUs > 0) {
        sp<AMessage> msg = new AMessage(kWhatEOS, this);
        msg->setInt32("audioEOSGeneration", mAudioEOSGeneration);
        msg->setInt32("finalResult", finalResult);
        msg->post(delayUs);
        return;
    }
    sp<AMessage> notify = mNotify->dup();
    notify->setInt32("what", kWhatEOS);
    notify->setInt32("audio", static_cast<int32_t>(audio));
    notify->setInt32("finalResult", finalResult);
    notify->post(delayUs);
}

void AmNuPlayer::Renderer::notifyAudioTearDown(AudioTearDownReason reason) {
    sp<AMessage> msg = new AMessage(kWhatAudioTearDown, this);
    msg->setInt32("reason", reason);
    msg->post();
}

void AmNuPlayer::Renderer::onQueueBufferDiscontinueCheck(sp<ABuffer> buffer, bool audio)
{
    int64_t mediaTimeUs;
    bool mContinuous = false;
    int64_t cur_time = ALooper::GetNowUs();
    int64_t postions;
    getCurrentPosition(&postions);
    CHECK(buffer->meta()->findInt64("timeUs", &mediaTimeUs));
    PTS_LOGD("queue[%s]:%lld:VJ=%d:Aj=%d ,postions %lld", audio?"audio":"video",
        mediaTimeUs, mVideoTimeJump, mAudioTimeJump,postions);

    if ((cur_time - mLastInfoTime > 10000000ll) ||
        (mDebugLevel >= 2 && cur_time - mLastInfoTime > 2000000ll)) {
        int64_t SytemTimeStampUs;
        status_t ret = mMediaClock->getMediaTime(cur_time, &SytemTimeStampUs);
        ///getCurrentPositionFromAnchor(&SytemTimeStampUs, cur_time);   //////////
        if (ret != OK)
            SytemTimeStampUs = mAnchorTimeMediaUs;
        int64_t diff=(SytemTimeStampUs - mLastVideoDrainTimeUs);
        const char *sync_info = "N/A";
		if (mAudioTimeJump || mVideoTimeJump) {
            sync_info ="Time Jumping";
        } else if (llabs(diff) < 200000ll) {/*<200ms ,think synced.*/
            sync_info ="AV SYNCED";
        } else if (diff > 0) {/*A> 200ms,*/
            sync_info ="Audio on Header";
        } else if (diff < 0) {/*V> 200ms,*/
            sync_info ="Video on Header";
        }
        PTS_LOG("AV sync info:%s \n",sync_info);
        PTS_LOG("  SystemTimeStamp:%lld\n", SytemTimeStampUs);
        PTS_LOG("  Last AudioTimeStamp:%lld\n", mAnchorTimeMediaUs);
        PTS_LOG("  Last VideoTimeStamp:%lld\n", mLastVideoDrainTimeUs);
        PTS_LOG("  AV diff:%lld ,Ajump:%d,Vjump:%d\n", diff, mAudioTimeJump, mVideoTimeJump);
        PTS_LOG("  mAjumpedNum:%d, mAudioJumped till now=%lld\n", mAjumpedNum, cur_time - mAudioJumpedTimeUs);
        PTS_LOG("  mVjumpedNum:%d, mVideoJumped till now=%lld\n", mVjumpedNum, cur_time - mVideoJumpedTimeUs);
        PTS_LOG("  mTotalAudioJumpedTimeUs:%lld\n",mTotalAudioJumpedTimeUs);
        PTS_LOG("  mLastVideoUs:%lld\n", mLastVideoUs);
        PTS_LOG("  mLastAudioUs:%lld\n", mLastAudioUs);
        mLastInfoTime = cur_time;
    }

    if (!audio) {/*get latest video duration.*/
        if (mAvgVideoFrameIntervalUs <= 0 ||
            mAvgVideoFrameIntervalUs != (mediaTimeUs - mLastVideoUs)) {
            int newIntervalUs = mediaTimeUs - mLastVideoUs;
            if (newIntervalUs <  1000000 &&//>1fps
                newIntervalUs > 1000000/120)//<120fps
            {
                if (llabs(mLastestVideoFrameIntervalUs - newIntervalUs) < 100 ||
                    mAvgVideoFrameIntervalUs < 0)
                    mAvgVideoFrameIntervalUs = mLastestVideoFrameIntervalUs;
                mLastestVideoFrameIntervalUs = newIntervalUs;
            }
        }
    }
    if (audio && mLastAudioUs != -1 &&
       (mCurrentPcmInfo.mSampleRate > 0 && mCurrentPcmInfo.mNumChannels > 0)) {
        int64_t expectTimeUs;
        int64_t diff_us = 0;
        int64_t abs_diff_us = 0 ;
        expectTimeUs = (mLastAudioFrameBytes * 1000000ll)/
            (mCurrentPcmInfo.mSampleRate * mCurrentPcmInfo.mNumChannels * 2);/*16bytes.*/
        expectTimeUs += mLastAudioUs;
        diff_us = (mediaTimeUs - expectTimeUs);
        abs_diff_us = llabs(diff_us);
        if (abs_diff_us < 20ll) {
            mContinuous = true;
        }
        if (diff_us > 4500000ll || diff_us < -500000ll) {
            PTS_LOG("audio Discontinue %lld-->%lld us,jump =%lld us",
            expectTimeUs, mediaTimeUs, (mediaTimeUs - expectTimeUs));
            mAudioTimeJump = true;
            mAudioJumpedTimeUs = cur_time;
            mAjumpedNum ++ ;
            mTotalAudioJumpedTimeUs += diff_us;
        } else if (mAudioTimeJump && mContinuous && (cur_time - mAudioJumpedTimeUs > 5000000ll)) {
            PTS_LOG("audio Discontinue timeout!");
            mAudioTimeJump = false;
            mAudioJumpedTimeUs = 0;
        }
    }
    if (!audio && mLastVideoUs != -1 &&
         mAvgVideoFrameIntervalUs > 0) {
        int64_t expectTimeUs = mLastVideoUs + mAvgVideoFrameIntervalUs;
        int64_t diff_us = mediaTimeUs - expectTimeUs;
        int64_t abs_diff_us = llabs(diff_us);
        if (abs_diff_us < 20ll) {
            mContinuous = true;
        }
        if (diff_us > 4500000ll || diff_us < -500000ll) {
            PTS_LOG("video Discontinue %lld-->%lld us,jump =%lld us",
            expectTimeUs, mediaTimeUs, (mediaTimeUs - expectTimeUs));
            mVideoTimeJump = true;
            mVideoJumpedTimeUs = cur_time;
            mVjumpedNum ++ ;
        } else if (mVideoTimeJump && mContinuous && (cur_time - mVideoJumpedTimeUs > 5000000ll)) {
            PTS_LOG("video Discontinue timeout!");
            mVideoTimeJump = false;
            mVideoJumpedTimeUs = 0;
        }
    }
    if (mContinuous && (mVideoTimeJump || mAudioTimeJump)) {
        int64_t SytemTimeStampUs;
        status_t ret = mMediaClock->getMediaTime(cur_time, &SytemTimeStampUs);
        PTS_LOG("mLastVideoDrainTimeUs %lld SytemTimeStampUs %lld diff %lld",
            mLastVideoDrainTimeUs,SytemTimeStampUs,mLastVideoDrainTimeUs -SytemTimeStampUs);
        if (!audio && ret == OK && /*check on video stream...*/
         llabs(mLastVideoDrainTimeUs - SytemTimeStampUs) < 100000ll && /*av diff < 500ms */
         ((mVideoTimeJump && (cur_time - mVideoJumpedTimeUs) > 500000ll) || /*video jumped > 500ms*/
         (mAudioTimeJump && (cur_time - mAudioJumpedTimeUs) > 500000ll))){ /*audio jumped > 500ms*/
            mAudioTimeJump = false;
            mVideoTimeJump = false;
            PTS_LOG("AV SYNCED,clear jumpinfo!!\n");
        }
    }

    if (audio) {/*latset audio */
        mLastAudioUs = mediaTimeUs;
        mLastAudioFrameBytes = buffer->size();
    } else {
        mLastVideoUs = mediaTimeUs;
    }

}

void AmNuPlayer::Renderer::onQueueBuffer(const sp<AMessage> &msg) {
    int32_t audio;
    CHECK(msg->findInt32("audio", &audio));

    if (dropBufferIfStale(audio, msg)) {
        return;
    }
#if 0
    if (audio) {
        mHasAudio = true;
    } else {
        mHasVideo = true;
    }
#endif

    if (mHasVideo) {
        if (mVideoScheduler == NULL) {
            mVideoScheduler = new VideoFrameScheduler();
            mVideoScheduler->init();
        }
    }

    sp<ABuffer> buffer;
    CHECK(msg->findBuffer("buffer", &buffer));
    onQueueBufferDiscontinueCheck(buffer,audio);
    sp<AMessage> notifyConsumed;
    CHECK(msg->findMessage("notifyConsumed", &notifyConsumed));

    QueueEntry entry;
    entry.mBuffer = buffer;
    entry.mNotifyConsumed = notifyConsumed;
    entry.mOffset = 0;
    entry.mFinalResult = OK;
    entry.mBufferOrdinal = ++mTotalBuffersQueued;

    if (audio) {
        Mutex::Autolock autoLock(mLock);
        mAudioQueue.push_back(entry);
        //postDrainAudioQueue_l();
    } else {
        mVideoQueue.push_back(entry);
        //postDrainVideoQueue();
    }

    if (mHasAudio && mHasVideo && !mRenderStarted) {
        if (mAudioQueue.empty() || mVideoQueue.empty()) {
            return;
        }
        mRenderStarted = true;
    }
    int32_t eos;
    if (notifyConsumed->findInt32("eos", &eos) && eos != 0)
        mQueueInitial = false;
    if (mQueueInitial) {
        Mutex::Autolock autoLock(mLock);
        //if (!mSyncQueues || mAudioQueue.empty() || mVideoQueue.empty()) {
        //    return;
        //}
        if (mHasAudio && mHasVideo) {
            if (mAudioQueue.empty() || mVideoQueue.empty()) {
                // EOS signalled on either queue.
                syncQueuesDone_l();
                return;
            }
        } else {
            mQueueInitial = false;
            goto PASS;
        }
        sp<ABuffer> firstAudioBuffer = (*mAudioQueue.begin()).mBuffer;
        sp<ABuffer> firstVideoBuffer = (*mVideoQueue.begin()).mBuffer;

        int64_t firstAudioTimeUs;
        int64_t firstVideoTimeUs;
        CHECK(firstAudioBuffer->meta()
                ->findInt64("timeUs", &firstAudioTimeUs));
        CHECK(firstVideoBuffer->meta()
                ->findInt64("timeUs", &firstVideoTimeUs));

        int64_t diff = firstVideoTimeUs - firstAudioTimeUs;

        ALOGI(" firstVideoTimeUs %lld firstAudioTimeUs %lld queueDiff = %.2f secs",
            (long long)firstVideoTimeUs,(long long)firstAudioTimeUs,diff / 1E6);

        if (diff > 100000ll) {
            // Audio data starts More than 0.1 secs before video.
            // Drop some audio.

            (*mAudioQueue.begin()).mNotifyConsumed->post();
            mAudioQueue.erase(mAudioQueue.begin());
            return;
        } else if (diff < -100000ll) {
            (*mVideoQueue.begin()).mNotifyConsumed->post();
            mVideoQueue.erase(mVideoQueue.begin());
            return;
        } else {
            mQueueInitial = false;
        }
    }
PASS:
    postDrainAudioQueue_l();
    postDrainVideoQueue();
}

void AmNuPlayer::Renderer::syncQueuesDone_l() {
    if (!mSyncQueues) {
        return;
    }

    mSyncQueues = false;

    if (!mAudioQueue.empty()) {
        postDrainAudioQueue_l();
    }

    if (!mVideoQueue.empty()) {
        mLock.unlock();
        postDrainVideoQueue();
        mLock.lock();
    }
}

void AmNuPlayer::Renderer::onQueueEOS(const sp<AMessage> &msg) {
    int32_t audio;
    CHECK(msg->findInt32("audio", &audio));

    if (dropBufferIfStale(audio, msg)) {
        return;
    }

    int32_t finalResult;
    CHECK(msg->findInt32("finalResult", &finalResult));

    QueueEntry entry;
    entry.mOffset = 0;
    entry.mFinalResult = finalResult;

    if (audio) {
        Mutex::Autolock autoLock(mLock);
        if (mAudioQueue.empty() && mSyncQueues) {
            syncQueuesDone_l();
        }
        mAudioQueue.push_back(entry);
        postDrainAudioQueue_l();
    } else {
        if (mVideoQueue.empty() && getSyncQueues()) {
            Mutex::Autolock autoLock(mLock);
            syncQueuesDone_l();
        }
        mVideoQueue.push_back(entry);
        postDrainVideoQueue();
    }
}

void AmNuPlayer::Renderer::setplaystate(String8 keyStr) {
    ALOGI("setplaystate [%s]\n", keyStr.string());
    if (!strcmp("trick_mode:fastforward", keyStr.string()) || !strcmp("trick_mode:fastbackward", keyStr.string()))
        misTrickmode = true;
    if (!strcmp("trick_mode:faststop", keyStr.string()))
        misTrickmode = false;
}

void AmNuPlayer::Renderer::onFlush(const sp<AMessage> &msg) {
    int32_t audio, notifyComplete;
    CHECK(msg->findInt32("audio", &audio));

    {
        Mutex::Autolock autoLock(mLock);
        if (audio) {
            notifyComplete = mNotifyCompleteAudio;
            mNotifyCompleteAudio = false;
            mLastAudioMediaTimeUs = -1;
        } else {
            notifyComplete = mNotifyCompleteVideo;
            mNotifyCompleteVideo = false;
        }

        // If we're currently syncing the queues, i.e. dropping audio while
        // aligning the first audio/video buffer times and only one of the
        // two queues has data, we may starve that queue by not requesting
        // more buffers from the decoder. If the other source then encounters
        // a discontinuity that leads to flushing, we'll never find the
        // corresponding discontinuity on the other queue.
        // Therefore we'll stop syncing the queues if at least one of them
        // is flushed.
        syncQueuesDone_l();
        clearAnchorTime_l();
    }

	if (!misTrickmode) {
        ALOGI("misTrickmode %d", misTrickmode);
        mQueueInitial = true;
    }
    mFirstVideoRealTime = 0;

    ALOGI("flushing %s", audio ? "audio" : "video");
    if (audio) {
        {
            Mutex::Autolock autoLock(mLock);
            entry = NULL;
            flushQueue(&mAudioQueue);

            ++mAudioDrainGeneration;
            ++mAudioEOSGeneration;
            prepareForMediaRenderingStart_l();

            // the frame count will be reset after flush.
            clearAudioFirstAnchorTime_l();
        }

        mDrainAudioQueuePending = false;

        if (offloadingAudio()) {
            mAudioSink->pause();
            mAudioSink->flush();
            if (!mPaused) {
                mAudioSink->start();
            }
        } else {
            mAudioSink->pause();
            mAudioSink->flush();
            // Call stop() to signal to the AudioSink to completely fill the
            // internal buffer before resuming playback.
            // FIXME: this is ignored after flush().
            mAudioSink->stop();
            if (mPaused) {
                // Race condition: if renderer is paused and audio sink is stopped,
                // we need to make sure that the audio track buffer fully drains
                // before delivering data.
                // FIXME: remove this if we can detect if stop() is complete.
                const int delayUs = 2 * 50 * 1000; // (2 full mixer thread cycles at 50ms)
                mPauseDrainAudioAllowedUs = ALooper::GetNowUs() + delayUs;
            } else {
                mAudioSink->start();
            }
            mNumFramesWritten = 0;
        }
        mNextAudioClockUpdateTimeUs = -1;
    } else {
        flushQueue(&mVideoQueue);

        mDrainVideoQueuePending = false;

        if (mVideoScheduler != NULL) {
            mVideoScheduler->restart();
        }

        Mutex::Autolock autoLock(mLock);
        ++mVideoDrainGeneration;
        prepareForMediaRenderingStart_l();
    }

    mVideoSampleReceived = false;

    if (notifyComplete) {
        notifyFlushComplete(audio);
    }
}

void AmNuPlayer::Renderer::flushQueue(List<QueueEntry> *queue) {
    while (!queue->empty()) {
        QueueEntry *entry = &*queue->begin();

        if (entry->mBuffer != NULL) {
            entry->mNotifyConsumed->post();
        }

        queue->erase(queue->begin());
        entry = NULL;
    }
}

void AmNuPlayer::Renderer::notifyFlushComplete(bool audio) {
    sp<AMessage> notify = mNotify->dup();
    notify->setInt32("what", kWhatFlushComplete);
    notify->setInt32("audio", static_cast<int32_t>(audio));
    notify->post();
}

bool AmNuPlayer::Renderer::dropBufferIfStale(
        bool audio, const sp<AMessage> &msg) {
    int32_t queueGeneration;
    CHECK(msg->findInt32("queueGeneration", &queueGeneration));

    if (queueGeneration == getQueueGeneration(audio)) {
        return false;
    }

    sp<AMessage> notifyConsumed;
    if (msg->findMessage("notifyConsumed", &notifyConsumed)) {
        notifyConsumed->post();
    }

    return true;
}

void AmNuPlayer::Renderer::onAudioSinkChanged() {
    if (offloadingAudio()) {
        return;
    }
    CHECK(!mDrainAudioQueuePending);
    mNumFramesWritten = 0;
    {
        Mutex::Autolock autoLock(mLock);
        mAnchorNumFramesWritten = -1;
    }
    uint32_t written;
    if (mAudioSink->getFramesWritten(&written) == OK) {
        mNumFramesWritten = written;
    }
}

void AmNuPlayer::Renderer::onDisableOffloadAudio() {
    Mutex::Autolock autoLock(mLock);
    mFlags &= ~FLAG_OFFLOAD_AUDIO;
    ++mAudioDrainGeneration;
    if (mAudioRenderingStartGeneration != -1) {
        prepareForMediaRenderingStart_l();
    }
}

void AmNuPlayer::Renderer::onEnableOffloadAudio() {
    Mutex::Autolock autoLock(mLock);
    mFlags |= FLAG_OFFLOAD_AUDIO;
    ++mAudioDrainGeneration;
    if (mAudioRenderingStartGeneration != -1) {
        prepareForMediaRenderingStart_l();
    }
}

void AmNuPlayer::Renderer::onPause() {
    if (mPaused) {
        return;
    }

    {
        Mutex::Autolock autoLock(mLock);
        // we do not increment audio drain generation so that we fill audio buffer during pause.
        ++mVideoDrainGeneration;
        prepareForMediaRenderingStart_l();
        mPaused = true;
        mMediaClock->setPlaybackRate(0.0);
    }

    mDrainAudioQueuePending = false;
    mDrainVideoQueuePending = false;

    // Note: audio data may not have been decoded, and the AudioSink may not be opened.
    mAudioSink->pause();
    startAudioOffloadPauseTimeout();

    ALOGI("now paused audio queue has %zu entries, video has %zu entries",
          mAudioQueue.size(), mVideoQueue.size());
}

void AmNuPlayer::Renderer::onResume() {
    if (!mPaused) {
        return;
    }

    // Note: audio data may not have been decoded, and the AudioSink may not be opened.
    cancelAudioOffloadPauseTimeout();
    if (mAudioSink->ready()) {
        status_t err = mAudioSink->start();
        if (err != OK) {
            ALOGE("cannot start AudioSink err %d", err);
            notifyAudioTearDown(kDueToError);
        }
    }

    {
        Mutex::Autolock autoLock(mLock);
        mPaused = false;
        // rendering started message may have been delayed if we were paused.
        if (mRenderingDataDelivered) {
            notifyIfMediaRenderingStarted_l();
        }
        // configure audiosink as we did not do it when pausing
        if (mAudioSink != NULL && mAudioSink->ready()) {
            mAudioSink->setPlaybackRate(mPlaybackSettings);
        }

        mMediaClock->setPlaybackRate(mPlaybackRate);

        if (!mAudioQueue.empty()) {
            postDrainAudioQueue_l();
        }
    }

    if (!mVideoQueue.empty()) {
        postDrainVideoQueue();
    }
}

void AmNuPlayer::Renderer::onSetVideoFrameRate(float fps) {
    if (mVideoScheduler == NULL) {
        mVideoScheduler = new VideoFrameScheduler();
    }
    mVideoScheduler->init(fps);
}

int32_t AmNuPlayer::Renderer::getQueueGeneration(bool audio) {
    Mutex::Autolock autoLock(mLock);
    return (audio ? mAudioQueueGeneration : mVideoQueueGeneration);
}

int32_t AmNuPlayer::Renderer::getDrainGeneration(bool audio) {
    Mutex::Autolock autoLock(mLock);
    return (audio ? mAudioDrainGeneration : mVideoDrainGeneration);
}

bool AmNuPlayer::Renderer::getSyncQueues() {
    Mutex::Autolock autoLock(mLock);
    return mSyncQueues;
}

void AmNuPlayer::Renderer::onAudioTearDown(AudioTearDownReason reason) {
    if (mAudioTornDown) {
        return;
    }
    mAudioTornDown = true;

    int64_t currentPositionUs;
    sp<AMessage> notify = mNotify->dup();
    if (getCurrentPosition(&currentPositionUs) == OK) {
        notify->setInt64("positionUs", currentPositionUs);
    }

    mAudioSink->stop();
    mAudioSink->flush();

    notify->setInt32("what", kWhatAudioTearDown);
    notify->setInt32("reason", reason);
    notify->post();
}

void AmNuPlayer::Renderer::startAudioOffloadPauseTimeout() {
    if (offloadingAudio()) {
        mWakeLock->acquire();
        sp<AMessage> msg = new AMessage(kWhatAudioOffloadPauseTimeout, this);
        msg->setInt32("drainGeneration", mAudioOffloadPauseTimeoutGeneration);
        msg->post(kOffloadPauseMaxUs);
    }
}

void AmNuPlayer::Renderer::cancelAudioOffloadPauseTimeout() {
    // We may have called startAudioOffloadPauseTimeout() without
    // the AudioSink open and with offloadingAudio enabled.
    //
    // When we cancel, it may be that offloadingAudio is subsequently disabled, so regardless
    // we always release the wakelock and increment the pause timeout generation.
    //
    // Note: The acquired wakelock prevents the device from suspending
    // immediately after offload pause (in case a resume happens shortly thereafter).
    mWakeLock->release(true);
    ++mAudioOffloadPauseTimeoutGeneration;
}

status_t AmNuPlayer::Renderer::onOpenAudioSink(
        const sp<AMessage> &format,
        bool offloadOnly,
        bool hasVideo,
        uint32_t flags) {
    ALOGV("openAudioSink: offloadOnly(%d) offloadingAudio(%d)",
            offloadOnly, offloadingAudio());
    bool audioSinkChanged = false;

    int32_t numChannels;
    CHECK(format->findInt32("channel-count", &numChannels));

    int32_t channelMask;
    if (!format->findInt32("channel-mask", &channelMask)) {
        // signal to the AudioSink to derive the mask from count.
        channelMask = CHANNEL_MASK_USE_CHANNEL_ORDER;
    }

    int32_t sampleRate;
    CHECK(format->findInt32("sample-rate", &sampleRate));

    if (offloadingAudio()) {
        audio_format_t audioFormat = AUDIO_FORMAT_PCM_16_BIT;
        AString mime;
        CHECK(format->findString("mime", &mime));
        status_t err = mapMimeToAudioFormat(audioFormat, mime.c_str());

        if (err != OK) {
            ALOGE("Couldn't map mime \"%s\" to a valid "
                    "audio_format", mime.c_str());
            onDisableOffloadAudio();
        } else {
            ALOGV("Mime \"%s\" mapped to audio_format 0x%x",
                    mime.c_str(), audioFormat);

            int avgBitRate = -1;
            format->findInt32("bitrate", &avgBitRate);

            int32_t aacProfile = -1;
            if (audioFormat == AUDIO_FORMAT_AAC
                    && format->findInt32("aac-profile", &aacProfile)) {
                // Redefine AAC format as per aac profile
                mapAACProfileToAudioFormat(
                        audioFormat,
                        aacProfile);
            }

            audio_offload_info_t offloadInfo = AUDIO_INFO_INITIALIZER;
            offloadInfo.duration_us = -1;
            format->findInt64(
                    "durationUs", &offloadInfo.duration_us);
            offloadInfo.sample_rate = sampleRate;
            offloadInfo.channel_mask = channelMask;
            offloadInfo.format = audioFormat;
            offloadInfo.stream_type = AUDIO_STREAM_MUSIC;
            offloadInfo.bit_rate = avgBitRate;
            offloadInfo.has_video = hasVideo;
            offloadInfo.is_streaming = true;

            if (memcmp(&mCurrentOffloadInfo, &offloadInfo, sizeof(offloadInfo)) == 0) {
                ALOGV("openAudioSink: no change in offload mode");
                // no change from previous configuration, everything ok.
                return OK;
            }
            mCurrentPcmInfo = AUDIO_PCMINFO_INITIALIZER;

            ALOGV("openAudioSink: try to open AudioSink in offload mode");
            uint32_t offloadFlags = flags;
            offloadFlags |= AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD;
            offloadFlags &= ~AUDIO_OUTPUT_FLAG_DEEP_BUFFER;
            audioSinkChanged = true;
            mAudioSink->close();

            err = mAudioSink->open(
                    sampleRate,
                    numChannels,
                    (audio_channel_mask_t)channelMask,
                    audioFormat,
                    0 /* bufferCount - unused */,
                    &AmNuPlayer::Renderer::AudioSinkCallback,
                    this,
                    (audio_output_flags_t)offloadFlags,
                    &offloadInfo);

            if (err == OK) {
                err = mAudioSink->setPlaybackRate(mPlaybackSettings);
            }

            if (err == OK) {
                // If the playback is offloaded to h/w, we pass
                // the HAL some metadata information.
                // We don't want to do this for PCM because it
                // will be going through the AudioFlinger mixer
                // before reaching the hardware.
                // TODO
                mCurrentOffloadInfo = offloadInfo;
                if (!mPaused) { // for preview mode, don't start if paused
                    err = mAudioSink->start();
                }
                ALOGV_IF(err == OK, "openAudioSink: offload succeeded");
            }
            if (err != OK) {
                // Clean up, fall back to non offload mode.
                mAudioSink->close();
                onDisableOffloadAudio();
                mCurrentOffloadInfo = AUDIO_INFO_INITIALIZER;
                ALOGV("openAudioSink: offload failed");
                if (offloadOnly) {
                    notifyAudioTearDown(kForceNonOffload);
                }
            } else {
                mUseAudioCallback = true;  // offload mode transfers data through callback
                ++mAudioDrainGeneration;  // discard pending kWhatDrainAudioQueue message.
            }
        }
    }
    if (!offloadOnly && !offloadingAudio()) {
        ALOGV("openAudioSink: open AudioSink in NON-offload mode");
        uint32_t pcmFlags = flags;
        bool FS_88_96_enable = false;
        audio_format_t aformat = AUDIO_FORMAT_PCM_16_BIT;
        pcmFlags &= ~AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD;

        AString mime;
        CHECK(format->findString("mime", &mime));
        int digital_raw = amsysfs_get_sysfs_int("/sys/class/audiodsp/digital_raw");
        int user_raw_enable = digital_raw && (
                              !strcasecmp(MEDIA_MIMETYPE_AUDIO_DTSHD, mime.c_str())  ||
                              !strcasecmp(MEDIA_MIMETYPE_AUDIO_AC3, mime.c_str()) ||
                              !strcasecmp(MEDIA_MIMETYPE_AUDIO_EAC3, mime.c_str()) ||
                              !strcasecmp(MEDIA_MIMETYPE_AUDIO_TRUEHD, mime.c_str()));
        if ( user_raw_enable ) {
            pcmFlags |= (AUDIO_OUTPUT_FLAG_IEC958_NONAUDIO | AUDIO_OUTPUT_FLAG_DIRECT);
            if (!strcasecmp(MEDIA_MIMETYPE_AUDIO_DTSHD, mime.c_str())) {
                aformat = AUDIO_FORMAT_IEC61937;
                //for high samprate ma stream, raw output raw samplerate will be half
                if (sampleRate == 96000 || sampleRate == 88200)
                     sampleRate = sampleRate / 2;
            }
            if (!strcasecmp(MEDIA_MIMETYPE_AUDIO_AC3, mime.c_str()) ||
                (!strcasecmp(MEDIA_MIMETYPE_AUDIO_EAC3, mime.c_str()) && digital_raw == 1))
                 aformat = AUDIO_FORMAT_IEC61937;
            if (!strcasecmp(MEDIA_MIMETYPE_AUDIO_EAC3, mime.c_str()) && digital_raw == 2) {
                 aformat = AUDIO_FORMAT_IEC61937;
                 sampleRate = sampleRate * 4;
            }
            if (!strcasecmp(MEDIA_MIMETYPE_AUDIO_TRUEHD, mime.c_str()))
                 aformat = AUDIO_FORMAT_TRUEHD;
        }else if((sampleRate == 96000 || sampleRate == 88200) && !strcasecmp(MEDIA_MIMETYPE_AUDIO_DTSHD, mime.c_str())){
           FS_88_96_enable = true;
           ALOGI("dts direct output hr pcm");
           pcmFlags |=  AUDIO_OUTPUT_FLAG_DIRECT;
        }

        const PcmInfo info = {
                (audio_channel_mask_t)channelMask,
                (audio_output_flags_t)pcmFlags,
                aformat, // TODO: change to audioFormat
                numChannels,
                sampleRate
        };
        if (memcmp(&mCurrentPcmInfo, &info, sizeof(info)) == 0) {
            ALOGV("openAudioSink: no change in pcm mode");
            // no change from previous configuration, everything ok.
            return OK;
        }

        audioSinkChanged = true;
        mAudioSink->close();
        mCurrentOffloadInfo = AUDIO_INFO_INITIALIZER;
        // Note: It is possible to set up the callback, but not use it to send audio data.
        // This requires a fix in AudioSink to explicitly specify the transfer mode.
        mUseAudioCallback = getUseAudioCallbackSetting();
        if ( user_raw_enable || FS_88_96_enable)
           mUseAudioCallback = 1;
        if (mUseAudioCallback) {
            ++mAudioDrainGeneration;  // discard pending kWhatDrainAudioQueue message.
        }

        // Compute the desired buffer size.
        // For callback mode, the amount of time before wakeup is about half the buffer size.
        //for passthrough frameCount need to be decided by hal buf size
        const uint32_t frameCount = (aformat!= AUDIO_FORMAT_PCM_16_BIT) ? 0:
            (unsigned long long)sampleRate * getAudioSinkPcmMsSetting()  / 1000;

        // The doNotReconnect means AudioSink will signal back and let AmNuPlayer to re-construct
        // AudioSink. We don't want this when there's video because it will cause a video seek to
        // the previous I frame. But we do want this when there's only audio because it will give
        // AmNuPlayer a chance to switch from non-offload mode to offload mode.
        // So we only set doNotReconnect when there's no video.
        const bool doNotReconnect = !hasVideo;

        // We should always be able to set our playback settings if the sink is closed.
        LOG_ALWAYS_FATAL_IF(mAudioSink->setPlaybackRate(mPlaybackSettings) != OK,
                "onOpenAudioSink: can't set playback rate on closed sink");
        status_t err = mAudioSink->open(
                    sampleRate,
                    numChannels,
                    (audio_channel_mask_t)channelMask,
                    aformat,
                    0 /* bufferCount - unused */,
                    mUseAudioCallback ? &AmNuPlayer::Renderer::AudioSinkCallback : NULL,
                    mUseAudioCallback ? this : NULL,
                    (audio_output_flags_t)pcmFlags,
                    NULL,
                    doNotReconnect,
                    frameCount);
        if (err != OK) {
            ALOGW("openAudioSink: non offloaded open failed status: %d", err);
            mAudioSink->close();
            mCurrentPcmInfo = AUDIO_PCMINFO_INITIALIZER;
            return err;
        }
        mCurrentPcmInfo = info;
        if (!mPaused) { // for preview mode, don't start if paused
            mAudioSink->start();
        }
    }
    if (audioSinkChanged) {
        onAudioSinkChanged();
    }
    mAudioTornDown = false;
    return OK;
}

void AmNuPlayer::Renderer::onCloseAudioSink() {
    mAudioSink->close();
    mCurrentOffloadInfo = AUDIO_INFO_INITIALIZER;
    mCurrentPcmInfo = AUDIO_PCMINFO_INITIALIZER;
}

}  // namespace android

