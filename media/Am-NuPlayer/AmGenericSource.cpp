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
#define LOG_TAG "NU-GenericSource"

#include "AmGenericSource.h"

#include "AmAnotherPacketSource.h"

#include <media/IMediaHTTPService.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/FileSource.h>
#include <media/stagefright/MediaBuffer.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaExtractor.h>
#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/AmMetaDataExt.h>
#include <media/stagefright/Utils.h>
#include "../../libstagefright/include/DRMExtractor.h"
#include "../../libstagefright/include/NuCachedSource2.h"
#include "../../libstagefright/include/WVMExtractor.h"
#include "../../libstagefright/include/HTTPBase.h"
#include "AmUDPSource.h"


#include "AmMPEG2TSExtractor.h"
namespace android {

static int64_t kLowWaterMarkUs = 2000000ll;  // 2secs
static int64_t kHighWaterMarkUs = 5000000ll;  // 5secs
static int64_t kHighWaterMarkRebufferUs = 15000000ll;  // 15secs
static const ssize_t kLowWaterMarkBytes = 40000;
static const ssize_t kHighWaterMarkBytes = 200000;
static int kDVDtsDiffThreshold = 22;
static FILE* dv_fpbl = NULL;
static FILE* dv_fpel = NULL;
static FILE* dv_fpbel = NULL;



AmNuPlayer::GenericSource::GenericSource(
        const sp<AMessage> &notify,
        bool uidValid,
        uid_t uid)
    : Source(notify),
      mAudioTimeUs(0),
      mAudioLastDequeueTimeUs(0),
      mAudiofirstQueueTimeUs(-1ll),
      mVideoTimeUs(0),
      mVideoLastDequeueTimeUs(0),
      mVideofirstQueueTimeUs(-1ll),
      mFetchSubtitleDataGeneration(0),
      mFetchTimedTextDataGeneration(0),
      mDurationUs(-1ll),
      mAudioIsVorbis(false),
      mIsWidevine(false),
      mIsSecure(false),
      mIsStreaming(false),
      mUIDValid(uidValid),
      mUID(uid),
      mFd(-1),
      mDrmManagerClient(NULL),
      mBitrate(-1ll),
      mBuffering(false),
      mNotifynotifyPrepared(false),
      mBufferingAnchorUs(-1ll),
      mPendingReadBufferTypes(0) {
    mBufferingMonitor = new BufferingMonitor(notify);
    resetDataSource();
    DataSource::RegisterDefaultSniffers();
    mIsUdp = false;
    mAmSubtitle = new AmSubtitle();
    mAmSubtitle->setSubFlag(false);
    mAmSubtitle->setStartPtsUpdateFlag(false);
    mAmSubtitle->setTypeUpdateFlag(false);
    char value[PROPERTY_VALUE_MAX];
    if (property_get("media.dv.dumpEs", value, NULL)
        && (!strcmp(value, "1") || !strcasecmp(value, "true"))) {
            dv_fpbl = fopen("/data/tmp/dv_bl_es.h264", "wb");
            dv_fpel = fopen("/data/tmp/dv_el_es.h264", "wb");
            dv_fpbel = fopen("/data/tmp/dv_bel_es.h264", "wb");
            if (!(dv_fpbl && dv_fpel)) {
                ALOGE("[%s %d] create dump file failed\n", __FUNCTION__, __LINE__);
                dv_fpbl = NULL;
                dv_fpel = NULL;
                dv_fpbel = NULL;
            }
    }
}

void AmNuPlayer::GenericSource::resetDataSource() {
    for (int i = 0;i < (int)mSources.size();i++)
        mSources.itemAt(i)->pause();
    while (!mSources.empty()) {
        mSources.erase(mSources.begin());
    }
    mHTTPService.clear();
    mHttpSource.clear();
    extractor.clear();
    sp<DataSource> dataSource = mDataSource;
    dataSource.clear();
    Vector<MediaBuffer*>::iterator it = mDVMediaBuffer.begin();
    while (it != mDVMediaBuffer.end()) {
        MediaBuffer *mediabuf = *it;
        mediabuf->release();
        ++it;
    }
    mDVMediaBuffer.clear();
    ALOGI("[%s %d]", __FUNCTION__, __LINE__);
    mUri.clear();
    mUriHeaders.clear();
    if (mFd >= 0) {
        close(mFd);
        mFd = -1;
    }
    mOffset = 0;
    mLength = 0;
    setDrmPlaybackStatusIfNeeded(Playback::STOP, 0);
    mDecryptHandle = NULL;
    mDrmManagerClient = NULL;
    mStarted = false;
    mStopRead = true;

    if (mBufferingMonitorLooper != NULL) {
        mBufferingMonitorLooper->unregisterHandler(mBufferingMonitor->id());
        mBufferingMonitorLooper->stop();
        mBufferingMonitorLooper = NULL;
    }
    mBufferingMonitor->stop();
}

status_t AmNuPlayer::GenericSource::setDataSource(
        const sp<IMediaHTTPService> &httpService,
        const char *url,
        const KeyedVector<String8, String8> *headers) {
    resetDataSource();

    mHTTPService = httpService;
    mUri = url;

    if (headers) {
        mUriHeaders = *headers;
    }

    // delay data source creation to prepareAsync() to avoid blocking
    // the calling thread in setDataSource for any significant time.
    return OK;
}

status_t AmNuPlayer::GenericSource::setDataSource(
        int fd, int64_t offset, int64_t length) {
    resetDataSource();

    mFd = dup(fd);
    mOffset = offset;
    mLength = length;

    // delay data source creation to prepareAsync() to avoid blocking
    // the calling thread in setDataSource for any significant time.
    return OK;
}

status_t AmNuPlayer::GenericSource::setDataSource(const sp<DataSource>& source) {
    resetDataSource();
    mDataSource = source;
    return OK;
}

sp<MetaData> AmNuPlayer::GenericSource::getFileFormatMeta() const {
    return mFileMeta;
}

status_t AmNuPlayer::GenericSource::initFromDataSource() {

    String8 mimeType;
    float confidence;
    sp<AMessage> dummy;
    bool isWidevineStreaming = false;
    int subTotal = 0;
    ALOGI(">>>[%s %d]", __FUNCTION__, __LINE__);

    CHECK(mDataSource != NULL);

    if (mIsWidevine) {
        isWidevineStreaming = SniffWVM(
                mDataSource, &mimeType, &confidence, &dummy);
        if (!isWidevineStreaming ||
                strcasecmp(
                    mimeType.string(), MEDIA_MIMETYPE_CONTAINER_WVM)) {
            ALOGE("unsupported widevine mime: %s", mimeType.string());
            return UNKNOWN_ERROR;
        }
    } else if (mIsStreaming) {
        if (!mDataSource->sniff(&mimeType, &confidence, &dummy)) {
            return UNKNOWN_ERROR;
        }
        isWidevineStreaming = !strcasecmp(
                mimeType.string(), MEDIA_MIMETYPE_CONTAINER_WVM);
    }

    if (isWidevineStreaming) {
        // we don't want cached source for widevine streaming.
        mCachedSource.clear();
        mDataSource = mHttpSource;
        mWVMExtractor = new WVMExtractor(mDataSource);
        mWVMExtractor->setAdaptiveStreamingMode(true);
        if (mUIDValid) {
            mWVMExtractor->setUID(mUID);
        }
        extractor = mWVMExtractor;
    } else {
#if 1
        if (!mUri.empty() && !strncasecmp("udp:", mUri.c_str(), 4))
            extractor = new AmMPEG2TSExtractor(mDataSource);
        else
#endif
            extractor = MediaExtractor::Create(mDataSource,
                mimeType.isEmpty() ? "amnu+" : mimeType.string());
    }
    ALOGI(">>>MediaExtractor::Create [%s %d]", __FUNCTION__, __LINE__);

    if (extractor == NULL) {
        return UNKNOWN_ERROR;
    }

    if (extractor->getDrmFlag()) {
        checkDrmStatus(mDataSource);
    }

    mFileMeta = extractor->getMetaData();
    if (mFileMeta != NULL) {
        int64_t duration;
        if (mFileMeta->findInt64(kKeyDuration, &duration)) {
            mDurationUs = duration;
        }

        if (!mIsWidevine) {
            // Check mime to see if we actually have a widevine source.
            // If the data source is not URL-type (eg. file source), we
            // won't be able to tell until now.
            const char *fileMime;
            if (mFileMeta->findCString(kKeyMIMEType, &fileMime)
                    && !strncasecmp(fileMime, "video/wvm", 9)) {
                mIsWidevine = true;
            }
        }
    }

    int32_t totalBitrate = 0;

    size_t numtracks = extractor->countTracks();
    if (numtracks == 0) {
        return UNKNOWN_ERROR;
    }

    char value[PROPERTY_VALUE_MAX];
    bool noaudio = false;
    bool novideo = false;
    bool nosubtitle = false;
    if (property_get("media.amplayer.novideo", value, NULL) &&
                    (!strcmp("1", value) || !strcasecmp("true", value))) {
        ALOGE("Disabled video for debug!\n");
        novideo = true;
    }

    if (property_get("media.amplayer.noaudio", value, NULL) &&
                    (!strcmp("1", value) || !strcasecmp("true", value))) {
        ALOGE("Disabled audio for debug!\n");
        noaudio = true;
    }

    if (property_get("media.amplayer.nosubtitle", value, NULL) &&
                    (!strcmp("1", value) || !strcasecmp("true", value))) {
        ALOGE("Disabled subtitle for debug!\n");
        nosubtitle = true;
    }

    for (size_t i = 0; i < numtracks; ++i) {
        int32_t isDv = false;
        sp<IMediaSource> track = extractor->getTrack(i);
        if (track == NULL) {
            continue;
        }

        sp<MetaData> meta = extractor->getTrackMetaData(i);
        if (meta == NULL) {
            ALOGE("no metadata for track %zu", i);
            return UNKNOWN_ERROR;
        }

        const char *mime;
        CHECK(meta->findCString(kKeyMIMEType, &mime));
        //ALOGI("[initFromDataSource]mime[%d]:%s\n", i, mime);

        // Do the string compare immediately with "mime",
        // we can't assume "mime" would stay valid after another
        // extractor operation, some extractors might modify meta
        // during getTrack() and make it invalid.
        if (!strncasecmp(mime, "audio/", 6)) {
            if (noaudio) {
                continue;
            }
            if (mAudioTrack.mSource == NULL) {
                mAudioTrack.mIndex = i;
                mAudioTrack.mSource = track;
                mAudioTrack.mPackets =
                    new AmAnotherPacketSource(mAudioTrack.mSource->getFormat());

                if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_VORBIS)) {
                    mAudioIsVorbis = true;
                } else {
                    mAudioIsVorbis = false;
                }
            }
        } else if (!strncasecmp(mime, "video/", 6)) {
            if (novideo) {
                continue;
            }
             ALOGI("++++++++++++++++++++++++\n");
            if (meta->findInt32(KKeyIsDV, &isDv)) {
                ALOGI("is a dolby vision");
                isDv = true;
            }
            if (isDv && mVideoTrack.mSource != NULL) {
                mDVTrack.mIndex = i;
                mDVTrack.mSource = track;
                mDVTrack.mPackets = NULL;
                ALOGI("set dv source\n");
                int32_t streamtimeToUsRatio;
                if (meta->findInt32(MKTAG('s', 't', 't', 'u'), &streamtimeToUsRatio)) {
                    //get dts convert accuracy, for check dts when dv double layer
                    kDVDtsDiffThreshold = 5 * streamtimeToUsRatio;
                }
                //new AmAnotherPacketSource(mDVTrack.mSource->getFormat());
            }
            if (mVideoTrack.mSource == NULL) {//  default is first
                mVideoTrack.mIndex = i;
                mVideoTrack.mSource = track;
                mVideoTrack.mPackets =
                    new AmAnotherPacketSource(mVideoTrack.mSource->getFormat());

                // check if the source requires secure buffers
                int32_t secure;
                if (meta->findInt32(kKeyRequiresSecureBuffers, &secure)
                        && secure) {
                    mIsSecure = true;
                    if (mUIDValid) {
                        extractor->setUID(mUID);
                    }
                }
                float frameRate;
                if (meta->findFloat('frRa',&frameRate)) {
                    sp<AMessage> msg = dupNotify();
                    msg->setInt32("what", kWhatFrameRate);
                    msg->setFloat("frame-rate", frameRate);
                    //ALOGI("send a frame-rate %.2f",frameRate);
                    msg->post();
                }
                int32_t decodec_param;
                if (meta->findInt32('depa',&decodec_param)) {
                    sp<AMessage> msg = dupNotify();
                    msg->setInt32("what", kWhatDecodeParam);
                    msg->setInt32("decodc-param", decodec_param);
                    //ALOGI("send a decodc-param %d",decodec_param);
                    msg->post();
                }
            }
        } else if (!strncasecmp(mime, "subtitle/", 9) || !strncasecmp(mime, "text/", 5)) {
            if (nosubtitle) {
                continue;
            }
            subTotal++;
        }

        mSources.push(track);
        int64_t durationUs;
        if (meta->findInt64(kKeyDuration, &durationUs)) {
            if (durationUs > mDurationUs) {
                mDurationUs = durationUs;
            }
        }

        int32_t bitrate;
        if (totalBitrate >= 0 && meta->findInt32(kKeyBitRate, &bitrate)) {
            totalBitrate += bitrate;
        } else {
            totalBitrate = -1;
        }
    }

    if (mSources.size() == 0) {
        ALOGE("b/23705695");
        return UNKNOWN_ERROR;
    }

    mBitrate = totalBitrate;

    mAmSubtitle->setSubTotal(subTotal);

    return OK;
}

status_t AmNuPlayer::GenericSource::startSources() {
    // Start the selected A/V tracks now before we start buffering.
    // Widevine sources might re-initialize crypto when starting, if we delay
    // this to start(), all data buffered during prepare would be wasted.
    // (We don't actually start reading until start().)
    if (mAudioTrack.mSource != NULL && mAudioTrack.mSource->start() != OK) {
        ALOGE("failed to start audio track!");
        return UNKNOWN_ERROR;
    }

    if (mVideoTrack.mSource != NULL && mVideoTrack.mSource->start() != OK) {
        ALOGE("failed to start video track!");
        return UNKNOWN_ERROR;
    }

    if (mDVTrack.mSource != NULL && mDVTrack.mSource->start() != OK) {
        ALOGE("failed to start dv track!");
        return UNKNOWN_ERROR;
    }

    return OK;
}

void AmNuPlayer::GenericSource::checkDrmStatus(const sp<DataSource>& dataSource) {
    dataSource->getDrmInfo(mDecryptHandle, &mDrmManagerClient);
    if (mDecryptHandle != NULL) {
        CHECK(mDrmManagerClient);
        if (RightsStatus::RIGHTS_VALID != mDecryptHandle->status) {
            sp<AMessage> msg = dupNotify();
            msg->setInt32("what", kWhatDrmNoLicense);
            msg->post();
        }
    }
}

int64_t AmNuPlayer::GenericSource::getLastReadPosition() {
    if (mAudioTrack.mSource != NULL) {
        return mAudioTimeUs;
    } else if (mVideoTrack.mSource != NULL) {
        return mVideoTimeUs;
    } else {
        return 0;
    }
}

status_t AmNuPlayer::GenericSource::setBuffers(
        bool audio, Vector<MediaBuffer *> &buffers) {
    if (mIsSecure && !audio && mVideoTrack.mSource != NULL) {
        return mVideoTrack.mSource->setBuffers(buffers);
    }
    return INVALID_OPERATION;
}

bool AmNuPlayer::GenericSource::isStreaming() const {
    return mIsStreaming;
}

void AmNuPlayer::GenericSource::setOffloadAudio(bool offload) {
    mBufferingMonitor->setOffloadAudio(offload);
}

AmNuPlayer::GenericSource::~GenericSource() {
    if (mLooper != NULL) {
        mLooper->unregisterHandler(id());
        mLooper->stop();
    }
    ALOGI(">>>[%s %d]", __FUNCTION__, __LINE__);
    resetDataSource();
    if (dv_fpbl && dv_fpbl && dv_fpbel) {
        fclose(dv_fpbl);
        fclose(dv_fpel);
        fclose(dv_fpbel);
    }

    delete mAmSubtitle;
}

void AmNuPlayer::GenericSource::prepareAsync() {
    if (mLooper == NULL) {
        mLooper = new ALooper;
        mLooper->setName("generic");
        mLooper->start();

        mLooper->registerHandler(this);
    }

    sp<AMessage> msg = new AMessage(kWhatPrepareAsync, this);
    msg->post();
}

void AmNuPlayer::GenericSource::onPrepareAsync() {
    //add for subtitle
    mAmSubtitle->init();

    // delayed data source creation
    if (mDataSource == NULL) {
        // set to false first, if the extractor
        // comes back as secure, set it to true then.
        mIsSecure = false;
        ALOGI("patch:%s",mUri.c_str());
        if (!mUri.empty() && !strncasecmp("udp:", mUri.c_str(), 4)) {
            mIsUdp = true;
            mIsWidevine = false;
            mDataSource = new AmUDPSource(mUri.c_str());
            ALOGI("create a AmUDPSource");
        } else if (!mUri.empty()) {
            const char* uri = mUri.c_str();
            String8 contentType;
            mIsWidevine = !strncasecmp(uri, "widevine://", 11);

            if (!strncasecmp("http://", uri, 7)
                    || !strncasecmp("https://", uri, 8)
                    || mIsWidevine) {
                mHttpSource = DataSource::CreateMediaHTTP(mHTTPService);
                if (mHttpSource == NULL) {
                    ALOGE("Failed to create http source!");
                    notifyPreparedAndCleanup(UNKNOWN_ERROR);
                    return;
                }
            }

            mDataSource = DataSource::CreateFromURI(
                   mHTTPService, uri, &mUriHeaders, &contentType,
                   static_cast<HTTPBase *>(mHttpSource.get()));
        } else {
            mIsWidevine = false;

            mDataSource = new FileSource(mFd, mOffset, mLength);
            mFd = -1;
        }

        if (mDataSource == NULL) {
            ALOGE("Failed to create data source!");
            notifyPreparedAndCleanup(UNKNOWN_ERROR);
            return;
        }
    }

    if (mDataSource->flags() & DataSource::kIsCachingDataSource) {
        mCachedSource = static_cast<NuCachedSource2 *>(mDataSource.get());
    }

    // For widevine or other cached streaming cases, we need to wait for
    // enough buffering before reporting prepared.
    // Note that even when URL doesn't start with widevine://, mIsWidevine
    // could still be set to true later, if the streaming or file source
    // is sniffed to be widevine. We don't want to buffer for file source
    // in that case, so must check the flag now.
    mIsStreaming = (mIsWidevine || mCachedSource != NULL);

    // init extractor from data source
    status_t err = initFromDataSource();

    if (err != OK) {
        ALOGE("Failed to init from data source!");
        notifyPreparedAndCleanup(err);
        return;
    }

    if (mVideoTrack.mSource != NULL) {
        sp<MetaData> meta = doGetFormatMeta(false /* audio */);
        sp<AMessage> msg = new AMessage;
        err = convertMetaDataToMessage(meta, &msg);
        if (err != OK) {
            notifyPreparedAndCleanup(err);
            return;
        }
        notifyVideoSizeChanged(msg);
    }

    notifyFlagsChanged(
            (mIsSecure ? FLAG_SECURE : 0)
            | (mDecryptHandle != NULL ? FLAG_PROTECTED : 0)
            | FLAG_CAN_PAUSE
            | FLAG_CAN_SEEK_BACKWARD
            | FLAG_CAN_SEEK_FORWARD
            | FLAG_CAN_SEEK);

    if (mIsSecure) {
        // secure decoders must be instantiated before starting widevine source
        sp<AMessage> reply = new AMessage(kWhatSecureDecodersInstantiated, this);
        notifyInstantiateSecureDecoders(reply);
    } else {
        if (!mIsUdp) {
            finishPrepareAsync();
        } else {
            mStopRead = false;
            if (mAudioTrack.mSource != NULL)
                postReadBuffer(MEDIA_TRACK_TYPE_AUDIO);
            if (mVideoTrack.mSource != NULL)
                postReadBuffer(MEDIA_TRACK_TYPE_VIDEO);
        }
    }
}

void AmNuPlayer::GenericSource::onSecureDecodersInstantiated(status_t err) {
    if (err != OK) {
        ALOGE("Failed to instantiate secure decoders!");
        notifyPreparedAndCleanup(err);
        return;
    }
    finishPrepareAsync();
}

void AmNuPlayer::GenericSource::finishPrepareAsync() {
    if (mNotifynotifyPrepared)
        return;
    mNotifynotifyPrepared = true;
    status_t err = startSources();
    if (err != OK) {
        ALOGE("Failed to init start data source!");
        notifyPreparedAndCleanup(err);
        return;
    }

    if (mIsStreaming) {
        if (mBufferingMonitorLooper == NULL) {
            mBufferingMonitor->prepare(mCachedSource, mWVMExtractor, mDurationUs, mBitrate,
                    mIsStreaming);

            mBufferingMonitorLooper = new ALooper;
            mBufferingMonitorLooper->setName("GSBMonitor");
            mBufferingMonitorLooper->start();
            mBufferingMonitorLooper->registerHandler(mBufferingMonitor);
        }

        mBufferingMonitor->ensureCacheIsFetching();
        mBufferingMonitor->restartPollBuffering();
    } else {
        ALOGI("notifyPrepared %d",__LINE__);
        notifyPrepared();
    }
}

void AmNuPlayer::GenericSource::notifyPreparedAndCleanup(status_t err) {
    if (err != OK) {
        {
            sp<DataSource> dataSource = mDataSource;
            sp<NuCachedSource2> cachedSource = mCachedSource;
            sp<DataSource> httpSource = mHttpSource;
            {
                Mutex::Autolock _l(mDisconnectLock);
                mDataSource.clear();
                mDecryptHandle = NULL;
                mDrmManagerClient = NULL;
                mCachedSource.clear();
                mHttpSource.clear();
            }
        }
        mBitrate = -1;

        mBufferingMonitor->cancelPollBuffering();
    }
    notifyPrepared(err);
}

void AmNuPlayer::GenericSource::start() {
    ALOGI("start");

    mStopRead = false;
    if (mAudioTrack.mSource != NULL) {
        postReadBuffer(MEDIA_TRACK_TYPE_AUDIO);
    }

    if (mVideoTrack.mSource != NULL) {
        postReadBuffer(MEDIA_TRACK_TYPE_VIDEO);
    }

    setDrmPlaybackStatusIfNeeded(Playback::START, getLastReadPosition() / 1000);
    mStarted = true;

    (new AMessage(kWhatStart, this))->post();
}

void AmNuPlayer::GenericSource::stop() {
    // nothing to do, just account for DRM playback status
    setDrmPlaybackStatusIfNeeded(Playback::STOP, 0);
    mStarted = false;
    if (mIsWidevine || mIsSecure) {
        // For widevine or secure sources we need to prevent any further reads.
        sp<AMessage> msg = new AMessage(kWhatStopWidevine, this);
        sp<AMessage> response;
        (void) msg->postAndAwaitResponse(&response);
    }
}

void AmNuPlayer::GenericSource::pause() {
    // nothing to do, just account for DRM playback status
    setDrmPlaybackStatusIfNeeded(Playback::PAUSE, 0);
    mStarted = false;
}

void AmNuPlayer::GenericSource::resume() {
    // nothing to do, just account for DRM playback status
    setDrmPlaybackStatusIfNeeded(Playback::START, getLastReadPosition() / 1000);
    mStarted = true;

    (new AMessage(kWhatResume, this))->post();
}

void AmNuPlayer::GenericSource::disconnect() {
    sp<DataSource> dataSource, httpSource;
    {
        Mutex::Autolock _l(mDisconnectLock);
        dataSource = mDataSource;
        httpSource = mHttpSource;
    }

    if (dataSource != NULL) {
        // disconnect data source
        if (dataSource->flags() & DataSource::kIsCachingDataSource) {
            static_cast<NuCachedSource2 *>(dataSource.get())->disconnect();
        }
    } else if (httpSource != NULL) {
        static_cast<HTTPBase *>(httpSource.get())->disconnect();
    }
}

void AmNuPlayer::GenericSource::setDrmPlaybackStatusIfNeeded(int playbackStatus, int64_t position) {
    if (mDecryptHandle != NULL) {
        mDrmManagerClient->setPlaybackStatus(mDecryptHandle, playbackStatus, position);
    }
    mSubtitleTrack.mPackets = new AmAnotherPacketSource(NULL);
    mTimedTextTrack.mPackets = new AmAnotherPacketSource(NULL);
}

status_t AmNuPlayer::GenericSource::feedMoreTSData() {
    return OK;
}

void AmNuPlayer::GenericSource::onMessageReceived(const sp<AMessage> &msg) {
    switch (msg->what()) {
      case kWhatPrepareAsync:
      {
          onPrepareAsync();
          break;
      }
      case kWhatFetchSubtitleData:
      {
          fetchTextData(kWhatSendSubtitleData, MEDIA_TRACK_TYPE_SUBTITLE,
                  mFetchSubtitleDataGeneration, mSubtitleTrack.mPackets, msg);
          break;
      }

      case kWhatFetchTimedTextData:
      {
          fetchTextData(kWhatSendTimedTextData, MEDIA_TRACK_TYPE_TIMEDTEXT,
                  mFetchTimedTextDataGeneration, mTimedTextTrack.mPackets, msg);
          break;
      }

      case kWhatSendSubtitleData:
      {
          sendTextData(kWhatSubtitleData, MEDIA_TRACK_TYPE_SUBTITLE,
                  mFetchSubtitleDataGeneration, mSubtitleTrack.mPackets, msg);
          break;
      }

      case kWhatSendGlobalTimedTextData:
      {
          sendGlobalTextData(kWhatTimedTextData, mFetchTimedTextDataGeneration, msg);
          break;
      }
      case kWhatSendTimedTextData:
      {
          sendTextData(kWhatTimedTextData, MEDIA_TRACK_TYPE_TIMEDTEXT,
                  mFetchTimedTextDataGeneration, mTimedTextTrack.mPackets, msg);
          break;
      }

      case kWhatChangeAVSource:
      {
          int32_t trackIndex;
          CHECK(msg->findInt32("trackIndex", &trackIndex));
          const sp<IMediaSource> source = mSources.itemAt(trackIndex);

          Track* track;
          const char *mime;
          media_track_type trackType, counterpartType;
          sp<MetaData> meta = source->getFormat();
          meta->findCString(kKeyMIMEType, &mime);
          if (!strncasecmp(mime, "audio/", 6)) {
              track = &mAudioTrack;
              trackType = MEDIA_TRACK_TYPE_AUDIO;
              counterpartType = MEDIA_TRACK_TYPE_VIDEO;;
          } else {
              CHECK(!strncasecmp(mime, "video/", 6));
              track = &mVideoTrack;
              trackType = MEDIA_TRACK_TYPE_VIDEO;
              counterpartType = MEDIA_TRACK_TYPE_AUDIO;;
          }


          if (track->mSource != NULL) {
              track->mSource->stop();
          }
          track->mSource = source;
          track->mSource->start();
          track->mIndex = trackIndex;

          int64_t timeUs, actualTimeUs;
          const bool formatChange = true;
          if (trackType == MEDIA_TRACK_TYPE_AUDIO) {
              timeUs = mAudioLastDequeueTimeUs;
          } else {
              timeUs = mVideoLastDequeueTimeUs;
          }
          readBuffer(trackType, timeUs, &actualTimeUs, formatChange);
          readBuffer(counterpartType, -1, NULL, false);
          ALOGV("timeUs %lld actualTimeUs %lld", (long long)timeUs, (long long)actualTimeUs);

          break;
      }

      case kWhatStart:
      case kWhatResume:
      {
          mBufferingMonitor->restartPollBuffering();
          break;
      }

      case kWhatGetFormat:
      {
          onGetFormatMeta(msg);
          break;
      }

      case kWhatGetSelectedTrack:
      {
          onGetSelectedTrack(msg);
          break;
      }

      case kWhatSelectTrack:
      {
          onSelectTrack(msg);
          break;
      }

      case kWhatSeek:
      {
          onSeek(msg);
          break;
      }

      case kWhatReadBuffer:
      {
          onReadBuffer(msg);
          break;
      }

      case kWhatSecureDecodersInstantiated:
      {
          int32_t err;
          CHECK(msg->findInt32("err", &err));
          onSecureDecodersInstantiated(err);
          break;
      }

      case kWhatStopWidevine:
      {
          // mStopRead is only used for Widevine to prevent the video source
          // from being read while the associated video decoder is shutting down.
          mStopRead = true;
          if (mVideoTrack.mSource != NULL) {
              mVideoTrack.mPackets->clear();
          }
          sp<AMessage> response = new AMessage;
          sp<AReplyToken> replyID;
          CHECK(msg->senderAwaitsResponse(&replyID));
          response->postReply(replyID);
          break;
      }

      default:
          Source::onMessageReceived(msg);
          break;
    }
}

void AmNuPlayer::GenericSource::fetchTextData(
        uint32_t sendWhat,
        media_track_type type,
        int32_t curGen,
        sp<AmAnotherPacketSource> packets,
        sp<AMessage> msg) {
    int32_t msgGeneration;
    CHECK(msg->findInt32("generation", &msgGeneration));
    if (msgGeneration != curGen) {
        // stale
        return;
    }

    int32_t avail;
    if (packets->hasBufferAvailable(&avail)) {
        return;
    }

    int64_t timeUs;
    CHECK(msg->findInt64("timeUs", &timeUs));

    int64_t subTimeUs;
    readBuffer(type, timeUs, &subTimeUs);

    int64_t delayUs = subTimeUs - timeUs;
    if (msg->what() == kWhatFetchSubtitleData) {
        const int64_t oneSecUs = 1000000ll;
        delayUs -= oneSecUs;
    }
    sp<AMessage> msg2 = new AMessage(sendWhat, this);
    msg2->setInt32("generation", msgGeneration);
    msg2->post(delayUs < 0 ? 0 : delayUs);
}

void AmNuPlayer::GenericSource::sendTextData(
        uint32_t what,
        media_track_type type,
        int32_t curGen,
        sp<AmAnotherPacketSource> packets,
        sp<AMessage> msg) {
    int32_t msgGeneration;
    CHECK(msg->findInt32("generation", &msgGeneration));
    if (msgGeneration != curGen) {
        // stale
        return;
    }

    int64_t subTimeUs;
    if (packets->nextBufferTime(&subTimeUs) != OK) {
        return;
    }

    int64_t nextSubTimeUs;
    int readAgain = 0;
    readBuffer(type, -1, &nextSubTimeUs, false, &readAgain);
    if (readAgain == 1) {
        msg->post(0);
        return;
    }

    sp<ABuffer> buffer;
    status_t dequeueStatus = packets->dequeueAccessUnit(&buffer);
    if (dequeueStatus == OK) {
        if (!mAmSubtitle->getSubFlag()) {
            sp<AMessage> notify = dupNotify();
            notify->setInt32("what", what);
            notify->setBuffer("buffer", buffer);
            notify->post();
        }

        const int64_t delayUs = nextSubTimeUs - subTimeUs;
        msg->post(delayUs < 0 ? 0 : delayUs);
    }
}

void AmNuPlayer::GenericSource::sendGlobalTextData(
        uint32_t what,
        int32_t curGen,
        sp<AMessage> msg) {
    int32_t msgGeneration;
    CHECK(msg->findInt32("generation", &msgGeneration));
    if (msgGeneration != curGen) {
        // stale
        return;
    }

    uint32_t textType;
    const void *data;
    size_t size = 0;
    if (mTimedTextTrack.mSource->getFormat()->findData(
                    kKeyTextFormatData, &textType, &data, &size)) {
        mGlobalTimedText = new ABuffer(size);
        if (mGlobalTimedText->data()) {
            memcpy(mGlobalTimedText->data(), data, size);
            sp<AMessage> globalMeta = mGlobalTimedText->meta();
            globalMeta->setInt64("timeUs", 0);
            globalMeta->setString("mime", MEDIA_MIMETYPE_TEXT_3GPP);
            globalMeta->setInt32("global", 1);
            sp<AMessage> notify = dupNotify();
            notify->setInt32("what", what);
            notify->setBuffer("buffer", mGlobalTimedText);
            notify->post();
        }
    }
}

sp<MetaData> AmNuPlayer::GenericSource::getFormatMeta(bool audio) {
    sp<AMessage> msg = new AMessage(kWhatGetFormat, this);
    msg->setInt32("audio", audio);

    sp<AMessage> response;
    sp<RefBase> format;
    status_t err = msg->postAndAwaitResponse(&response);
    if (err == OK && response != NULL) {
        CHECK(response->findObject("format", &format));
        return static_cast<MetaData*>(format.get());
    } else {
        return NULL;
    }
}

void AmNuPlayer::GenericSource::onGetFormatMeta(sp<AMessage> msg) const {
    int32_t audio;
    CHECK(msg->findInt32("audio", &audio));

    sp<AMessage> response = new AMessage;
    sp<MetaData> format = doGetFormatMeta(audio);
    response->setObject("format", format);

    sp<AReplyToken> replyID;
    CHECK(msg->senderAwaitsResponse(&replyID));
    response->postReply(replyID);
}

sp<MetaData> AmNuPlayer::GenericSource::doGetFormatMeta(bool audio) const {
    sp<IMediaSource> source = audio ? mAudioTrack.mSource : mVideoTrack.mSource;

    if (source == NULL) {
        return NULL;
    }

    return source->getFormat();
}

status_t AmNuPlayer::GenericSource::dequeueAccessUnit(
        bool audio, sp<ABuffer> *accessUnit) {
    if (audio && !mStarted) {
        return -EWOULDBLOCK;
    }

    Track *track = audio ? &mAudioTrack : &mVideoTrack;

    if (track->mSource == NULL) {
        return -EWOULDBLOCK;
    }

    if (mIsWidevine && !audio) {
        // try to read a buffer as we may not have been able to the last time
        postReadBuffer(MEDIA_TRACK_TYPE_VIDEO);
    }
    if (mIsUdp && mBuffering)
        return -EWOULDBLOCK;

    status_t finalResult;

    if (!track->mPackets->hasBufferAvailable(&finalResult)) {
        if (finalResult == OK) {
            if (!mIsUdp)
                postReadBuffer(
                        audio ? MEDIA_TRACK_TYPE_AUDIO : MEDIA_TRACK_TYPE_VIDEO);
            //ALOGI("hasBufferAvailable == 0 is audio? %d",audio);
            if (mBufferingAnchorUs < 0 && mIsUdp)
                mBufferingAnchorUs = ALooper::GetNowUs();
            if (mBufferingAnchorUs > 0 && ALooper::GetNowUs() - mBufferingAnchorUs >= 10000ll) { //delay 50ms
                sp<AMessage> notify = dupNotify();
                notify->setInt32("what", kWhatPauseOnBufferingStart);
                mBuffering = true;
                notify->post();
                ALOGI("UDP buffering start!\n");
            }
            return -EWOULDBLOCK;
        }
        ALOGE("hasBufferAvailable return %d",finalResult);
        return finalResult;
    }

    mBufferingAnchorUs = -1;
    status_t result = track->mPackets->dequeueAccessUnit(accessUnit);

    // start pulling in more buffers if we only have one (or no) buffer left
    // so that decoder has less chance of being starved
    if (track->mPackets->getAvailableBufferCount(&finalResult) < 10) {
        postReadBuffer(audio? MEDIA_TRACK_TYPE_AUDIO : MEDIA_TRACK_TYPE_VIDEO);
    }


    if (result != OK) {
        if (mSubtitleTrack.mSource != NULL) {
            mSubtitleTrack.mPackets->clear();
            mFetchSubtitleDataGeneration++;
        }
        if (mTimedTextTrack.mSource != NULL) {
            mTimedTextTrack.mPackets->clear();
            mFetchTimedTextDataGeneration++;
        }
        ALOGE("dequeueAccessUnit return %d",result);
        return result;
    }

    int64_t timeUs;
    status_t eosResult; // ignored
    CHECK((*accessUnit)->meta()->findInt64("timeUs", &timeUs));
    if (audio) {
        mAudioLastDequeueTimeUs = timeUs;
        mBufferingMonitor->updateDequeuedBufferTime(timeUs);
    } else {
        mVideoLastDequeueTimeUs = timeUs;
    }
    //ALOGI("dequeueAccessUnit %s timeUs %lld conut %d",audio?"audio":"video",timeUs,track->mPackets->getAvailableBufferCount(&finalResult));
    if (mSubtitleTrack.mSource != NULL
            && !mSubtitleTrack.mPackets->hasBufferAvailable(&eosResult)) {
        sp<AMessage> msg = new AMessage(kWhatFetchSubtitleData, this);
        msg->setInt64("timeUs", timeUs);
        msg->setInt32("generation", mFetchSubtitleDataGeneration);
        msg->post();
    }

    if (mTimedTextTrack.mSource != NULL
            && !mTimedTextTrack.mPackets->hasBufferAvailable(&eosResult)) {
        sp<AMessage> msg = new AMessage(kWhatFetchTimedTextData, this);
        msg->setInt64("timeUs", timeUs);
        msg->setInt32("generation", mFetchTimedTextDataGeneration);
        msg->post();
    }

    return result;
}

status_t AmNuPlayer::GenericSource::getDuration(int64_t *durationUs) {
    *durationUs = mDurationUs;
    return OK;
}

size_t AmNuPlayer::GenericSource::getTrackCount() const {
    return mSources.size();
}

sp<AMessage> AmNuPlayer::GenericSource::getTrackInfo(size_t trackIndex) const {
    size_t trackCount = mSources.size();
    if (trackIndex >= trackCount) {
        return NULL;
    }

    sp<AMessage> format = new AMessage();
    sp<MetaData> meta = mSources.itemAt(trackIndex)->getFormat();
    if (meta == NULL) {
        ALOGE("no metadata for track %zu", trackIndex);
        return NULL;
    }

    const char *mime;
    CHECK(meta->findCString(kKeyMIMEType, &mime));
    if (!strncasecmp(mime, "audio/", 6)) {
        convertMetaDataToMessage(meta, &format);
    }
    format->setString("mime", mime);

    int32_t trackType;
    if (!strncasecmp(mime, "video/", 6)) {
        trackType = MEDIA_TRACK_TYPE_VIDEO;
    } else if (!strncasecmp(mime, "audio/", 6)) {
        trackType = MEDIA_TRACK_TYPE_AUDIO;
    } else if (!strncasecmp(mime, "subtitle/", 9)) {
        trackType = MEDIA_TRACK_TYPE_SUBTITLE;
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_TEXT_3GPP)) {
        trackType = MEDIA_TRACK_TYPE_TIMEDTEXT;
    } else {
        trackType = MEDIA_TRACK_TYPE_UNKNOWN;
    }
    format->setInt32("type", trackType);

    const char *lang;
    if (!meta->findCString(kKeyMediaLanguage, &lang)) {
        lang = "und";
    }
    format->setString("language", lang);

    if (trackType == MEDIA_TRACK_TYPE_SUBTITLE) {
        int32_t isAutoselect = 1, isDefault = 0, isForced = 0;
        meta->findInt32(kKeyTrackIsAutoselect, &isAutoselect);
        meta->findInt32(kKeyTrackIsDefault, &isDefault);
        meta->findInt32(kKeyTrackIsForced, &isForced);

        format->setInt32("auto", !!isAutoselect);
        format->setInt32("default", !!isDefault);
        format->setInt32("forced", !!isForced);
    } else if(trackType == MEDIA_TRACK_TYPE_VIDEO) {
        const char *programName;
        if (meta->findCString(kKeyProgramName, &programName)) {
            format->setString("program-name", programName);
        }
    }

    int32_t prgnum_num = -1;
    if (meta->findInt32(kKeyProgramNum, &prgnum_num)) {
        format->setInt32("program-num", prgnum_num);
    }

    return format;
}

ssize_t AmNuPlayer::GenericSource::getSelectedTrack(media_track_type type) const {
    sp<AMessage> msg = new AMessage(kWhatGetSelectedTrack, this);
    msg->setInt32("type", type);

    sp<AMessage> response;
    int32_t index;
    status_t err = msg->postAndAwaitResponse(&response);
    if (err == OK && response != NULL) {
        CHECK(response->findInt32("index", &index));
        return index;
    } else {
        return -1;
    }
}

void AmNuPlayer::GenericSource::onGetSelectedTrack(sp<AMessage> msg) const {
    int32_t tmpType;
    CHECK(msg->findInt32("type", &tmpType));
    media_track_type type = (media_track_type)tmpType;

    sp<AMessage> response = new AMessage;
    ssize_t index = doGetSelectedTrack(type);
    response->setInt32("index", index);

    sp<AReplyToken> replyID;
    CHECK(msg->senderAwaitsResponse(&replyID));
    response->postReply(replyID);
}

ssize_t AmNuPlayer::GenericSource::doGetSelectedTrack(media_track_type type) const {
    const Track *track = NULL;
    switch (type) {
    case MEDIA_TRACK_TYPE_VIDEO:
        track = &mVideoTrack;
        break;
    case MEDIA_TRACK_TYPE_AUDIO:
        track = &mAudioTrack;
        break;
    case MEDIA_TRACK_TYPE_TIMEDTEXT:
        track = &mTimedTextTrack;
        break;
    case MEDIA_TRACK_TYPE_SUBTITLE:
        track = &mSubtitleTrack;
        break;
    default:
        break;
    }

    if (track != NULL && track->mSource != NULL) {
        return track->mIndex;
    }

    return -1;
}

status_t AmNuPlayer::GenericSource::selectTrack(size_t trackIndex, bool select, int64_t timeUs) {
    ALOGI("%s track: %zu", select ? "select" : "deselect", trackIndex);
    sp<AMessage> msg = new AMessage(kWhatSelectTrack, this);
    msg->setInt32("trackIndex", trackIndex);
    msg->setInt32("select", select);
    msg->setInt64("timeUs", timeUs);

    sp<AMessage> response;
    status_t err = msg->postAndAwaitResponse(&response);
    if (err == OK && response != NULL) {
        CHECK(response->findInt32("err", &err));
    }

    return err;
}

void AmNuPlayer::GenericSource::onSelectTrack(sp<AMessage> msg) {
    int32_t trackIndex, select;
    int64_t timeUs;
    CHECK(msg->findInt32("trackIndex", &trackIndex));
    CHECK(msg->findInt32("select", &select));
    CHECK(msg->findInt64("timeUs", &timeUs));

    sp<AMessage> response = new AMessage;
    status_t err = doSelectTrack(trackIndex, select, timeUs);
    response->setInt32("err", err);

    sp<AReplyToken> replyID;
    CHECK(msg->senderAwaitsResponse(&replyID));
    response->postReply(replyID);
}

status_t AmNuPlayer::GenericSource::doSelectTrack(size_t trackIndex, bool select, int64_t timeUs) {
    if (trackIndex >= mSources.size()) {
        return BAD_INDEX;
    }

    if (!select) {
        Track* track = NULL;
        if (mSubtitleTrack.mSource != NULL && trackIndex == mSubtitleTrack.mIndex) {
            track = &mSubtitleTrack;
            mFetchSubtitleDataGeneration++;
        } else if (mTimedTextTrack.mSource != NULL && trackIndex == mTimedTextTrack.mIndex) {
            track = &mTimedTextTrack;
            mFetchTimedTextDataGeneration++;
        }
        if (track == NULL) {
            return INVALID_OPERATION;
        }
        track->mSource->stop();
        track->mSource = NULL;
        track->mPackets->clear();
        return OK;
    }

    const sp<IMediaSource> source = mSources.itemAt(trackIndex);
    sp<MetaData> meta = source->getFormat();
    const char *mime;
    CHECK(meta->findCString(kKeyMIMEType, &mime));
    bool isAmSubtitle = !strncasecmp(mime, "subtitle/", 9);
    mAmSubtitle->setSubFlag(isAmSubtitle);
    if (isAmSubtitle || !strncasecmp(mime, "text/", 5)) {
        mAmSubtitle->setTypeUpdateFlag(false);
    }

    if (!strncasecmp(mime, "text/", 5) || isAmSubtitle) {
        bool isSubtitle = strcasecmp(mime, MEDIA_MIMETYPE_TEXT_3GPP) || isAmSubtitle;
        Track *track = isSubtitle ? &mSubtitleTrack : &mTimedTextTrack;
        if (track->mSource != NULL && track->mIndex == trackIndex) {
            return OK;
        }
        track->mIndex = trackIndex;
        if (track->mSource != NULL) {
            track->mSource->stop();
        }
        track->mSource = mSources.itemAt(trackIndex);
        track->mSource->start();
        if (track->mPackets == NULL) {
            track->mPackets = new AmAnotherPacketSource(track->mSource->getFormat());
        } else {
            track->mPackets->clear();
            track->mPackets->setFormat(track->mSource->getFormat());

        }

        if (isAmSubtitle) {
            mAmSubtitle->reset();
        }

        if (isSubtitle) {
            mFetchSubtitleDataGeneration++;
        } else {
            mFetchTimedTextDataGeneration++;
        }

        status_t eosResult; // ignored
        if (mSubtitleTrack.mSource != NULL
                && !mSubtitleTrack.mPackets->hasBufferAvailable(&eosResult)) {
            sp<AMessage> msg = new AMessage(kWhatFetchSubtitleData, this);
            msg->setInt64("timeUs", timeUs);
            msg->setInt32("generation", mFetchSubtitleDataGeneration);
            msg->post();
        }

        if (!isAmSubtitle) {
            sp<AMessage> msg2 = new AMessage(kWhatSendGlobalTimedTextData, this);
            msg2->setInt32("generation", mFetchTimedTextDataGeneration);
            msg2->post();
        }

        if (mTimedTextTrack.mSource != NULL
                && !mTimedTextTrack.mPackets->hasBufferAvailable(&eosResult)) {
            sp<AMessage> msg = new AMessage(kWhatFetchTimedTextData, this);
            msg->setInt64("timeUs", timeUs);
            msg->setInt32("generation", mFetchTimedTextDataGeneration);
            msg->post();
        }

        return OK;
    } else if (!strncasecmp(mime, "audio/", 6) || !strncasecmp(mime, "video/", 6)) {
        bool audio = !strncasecmp(mime, "audio/", 6);
        Track *track = audio ? &mAudioTrack : &mVideoTrack;
        if (track->mSource != NULL && track->mIndex == trackIndex) {
            return OK;
        }

        sp<AMessage> msg = new AMessage(kWhatChangeAVSource, this);
        msg->setInt32("trackIndex", trackIndex);
        msg->post();
        return OK;
    }

    return INVALID_OPERATION;
}

status_t AmNuPlayer::GenericSource::seekTo(int64_t seekTimeUs) {
    sp<AMessage> msg = new AMessage(kWhatSeek, this);
    msg->setInt64("seekTimeUs", seekTimeUs);

    sp<AMessage> response;
    status_t err = msg->postAndAwaitResponse(&response);
    if (err == OK && response != NULL) {
        CHECK(response->findInt32("err", &err));
    }

    return err;
}

void AmNuPlayer::GenericSource::onSeek(sp<AMessage> msg) {
    int64_t seekTimeUs;
    CHECK(msg->findInt64("seekTimeUs", &seekTimeUs));

    sp<AMessage> response = new AMessage;
    status_t err = doSeek(seekTimeUs);
    response->setInt32("err", err);

    sp<AReplyToken> replyID;
    CHECK(msg->senderAwaitsResponse(&replyID));
    response->postReply(replyID);
}

status_t AmNuPlayer::GenericSource::doSeek(int64_t seekTimeUs) {
    mBufferingMonitor->updateDequeuedBufferTime(-1ll);

    // If the Widevine source is stopped, do not attempt to read any
    // more buffers.
    if (mStopRead) {
        return INVALID_OPERATION;
    }
    if (mVideoTrack.mSource != NULL) {
        int64_t actualTimeUs;
        readBuffer(MEDIA_TRACK_TYPE_VIDEO, seekTimeUs, &actualTimeUs);

        seekTimeUs = actualTimeUs;
        mVideoLastDequeueTimeUs = seekTimeUs;
    }

    if (mAudioTrack.mSource != NULL) {
        readBuffer(MEDIA_TRACK_TYPE_AUDIO, seekTimeUs);
        mAudioLastDequeueTimeUs = seekTimeUs;
    }

    if (mSubtitleTrack.mSource != NULL && mAmSubtitle->getSubFlag()) {
        mAmSubtitle->reset();
        readBuffer(MEDIA_TRACK_TYPE_SUBTITLE, seekTimeUs);
    }

    setDrmPlaybackStatusIfNeeded(Playback::START, seekTimeUs / 1000);
    if (!mStarted) {
        setDrmPlaybackStatusIfNeeded(Playback::PAUSE, 0);
    }

    // If currently buffering, post kWhatBufferingEnd first, so that
    // NuPlayer resumes. Otherwise, if cache hits high watermark
    // before new polling happens, no one will resume the playback.
    mBufferingMonitor->stopBufferingIfNecessary();
    mBufferingMonitor->restartPollBuffering();

    return OK;
}

sp<ABuffer> AmNuPlayer::GenericSource::mediaBufferToABuffer(
        MediaBuffer* mb,
        media_track_type trackType,
        int64_t /* seekTimeUs */,
        int64_t *actualTimeUs) {
    bool audio = trackType == MEDIA_TRACK_TYPE_AUDIO;
    size_t outLength = mb->range_length();

    if (audio && mAudioIsVorbis) {
        outLength += sizeof(int32_t);
    }

    sp<ABuffer> ab;
    if (mIsSecure && !audio) {
        // data is already provided in the buffer
        ab = new ABuffer(NULL, mb->range_length());
        mb->add_ref();
        ab->setMediaBufferBase(mb);
    } else {
        ab = new ABuffer(outLength);
        memcpy(ab->data(),
               (const uint8_t *)mb->data() + mb->range_offset(),
               mb->range_length());
    }

    if (audio && mAudioIsVorbis) {
        int32_t numPageSamples;
        if (!mb->meta_data()->findInt32(kKeyValidSamples, &numPageSamples)) {
            numPageSamples = -1;
        }

        uint8_t* abEnd = ab->data() + mb->range_length();
        memcpy(abEnd, &numPageSamples, sizeof(numPageSamples));
    }

    sp<AMessage> meta = ab->meta();

    int64_t timeUs;
    CHECK(mb->meta_data()->findInt64(kKeyTime, &timeUs));
    meta->setInt64("timeUs", timeUs);

#if 0
    // Temporarily disable pre-roll till we have a full solution to handle
    // both single seek and continous seek gracefully.
    if (seekTimeUs > timeUs) {
        sp<AMessage> extra = new AMessage;
        extra->setInt64("resume-at-mediaTimeUs", seekTimeUs);
        meta->setMessage("extra", extra);
    }
#endif

    if (trackType == MEDIA_TRACK_TYPE_TIMEDTEXT) {
        const char *mime;
        CHECK(mTimedTextTrack.mSource != NULL
                && mTimedTextTrack.mSource->getFormat()->findCString(kKeyMIMEType, &mime));
        meta->setString("mime", mime);
    }

    int64_t durationUs;
    if (mb->meta_data()->findInt64(kKeyDuration, &durationUs)) {
        meta->setInt64("durationUs", durationUs);
    }

    if (trackType == MEDIA_TRACK_TYPE_SUBTITLE) {
        meta->setInt32("trackIndex", mSubtitleTrack.mIndex);
    }

    uint32_t dataType; // unused
    const void *seiData;
    size_t seiLength;
    if (mb->meta_data()->findData(kKeySEI, &dataType, &seiData, &seiLength)) {
        sp<ABuffer> sei = ABuffer::CreateAsCopy(seiData, seiLength);;
        meta->setBuffer("sei", sei);
    }

    const void *mpegUserDataPointer;
    size_t mpegUserDataLength;
    if (mb->meta_data()->findData(
            kKeyMpegUserData, &dataType, &mpegUserDataPointer, &mpegUserDataLength)) {
        sp<ABuffer> mpegUserData = ABuffer::CreateAsCopy(mpegUserDataPointer, mpegUserDataLength);
        meta->setBuffer("mpegUserData", mpegUserData);
    }

    if (actualTimeUs) {
        *actualTimeUs = timeUs;
    }

    mb->release();
    mb = NULL;

    return ab;
}

void AmNuPlayer::GenericSource::postReadBuffer(media_track_type trackType) {
    Mutex::Autolock _l(mReadBufferLock);

    if ((mPendingReadBufferTypes & (1 << trackType)) == 0) {
        mPendingReadBufferTypes |= (1 << trackType);
        sp<AMessage> msg = new AMessage(kWhatReadBuffer, this);
        msg->setInt32("trackType", trackType);
        msg->post();
    }
}

void AmNuPlayer::GenericSource::onReadBuffer(sp<AMessage> msg) {
    int32_t tmpType;
    CHECK(msg->findInt32("trackType", &tmpType));
    media_track_type trackType = (media_track_type)tmpType;
    readBuffer(trackType);
    {
        // only protect the variable change, as readBuffer may
        // take considerable time.
        Mutex::Autolock _l(mReadBufferLock);
        mPendingReadBufferTypes &= ~(1 << trackType);
    }

    if (mIsUdp) {
        if (mBuffering) {
            bool start = true;
            status_t finalResult;
            if (mAudioTrack.mSource != NULL && mAudioTrack.mPackets->getAvailableBufferCount(&finalResult) < 30)
                start = false;
            if (mVideoTrack.mSource != NULL &&  mVideoTrack.mPackets->getAvailableBufferCount(&finalResult) < 30)
                start = false;
            if (start) {
                sp<AMessage> notify = dupNotify();
                notify->setInt32("what", kWhatResumeOnBufferingEnd);
                ALOGI("UDP buffering end!\n");
                notify->post();
                mBuffering = false;
            }

        }
        int64_t min_dur = -1,dur = 0;

        status_t err = OK;
        media_track_type mintype;
        if (mAudioTrack.mSource != NULL) {
            dur = mAudioTrack.mPackets->getBufferedDurationUs(&err);
            if (err == OK && (min_dur == -1 || min_dur > dur)) {
                min_dur = dur;
                mintype = MEDIA_TRACK_TYPE_AUDIO;
            }
            //ALOGI("audio dur %lld count %d",(long long)dur,mAudioTrack.mPackets->getAvailableBufferCount(&err));
        }

        if (mVideoTrack.mSource != NULL) {
            dur = mVideoTrack.mPackets->getBufferedDurationUs(&err);
            if (err == OK && (min_dur == -1 || min_dur > dur)) {
                min_dur = dur;
                mintype = MEDIA_TRACK_TYPE_VIDEO;
            }
            //ALOGI("video dur %lld count %d",(long long)dur,mVideoTrack.mPackets->getAvailableBufferCount(&err));
        }

        int64_t diff = 0;
        if (mVideofirstQueueTimeUs >= 0 && mAudiofirstQueueTimeUs >= 0 && !mNotifynotifyPrepared) {
            if (min_dur >= 500*1000ll && mVideofirstQueueTimeUs > mAudiofirstQueueTimeUs) {//to down audio
                mintype = MEDIA_TRACK_TYPE_AUDIO;
                min_dur = mAudioTrack.mPackets->getBufferedDurationUs(&err);
                diff += (mVideofirstQueueTimeUs - mAudiofirstQueueTimeUs)/3;
                if (diff > 2000000ll) diff = 2000000ll;
            }
        }
        if (min_dur >= 500*1000ll + diff && !mNotifynotifyPrepared) {//udp buffing 300ms play. it may modify
            finishPrepareAsync();
            ALOGI("udp finishPrepareAsync %lld", (long long)(mVideofirstQueueTimeUs - mAudiofirstQueueTimeUs));
        }
        postReadBuffer(mintype);

    }
}

void AmNuPlayer::GenericSource::readBuffer(
        media_track_type trackType, int64_t seekTimeUs, int64_t *actualTimeUs, bool formatChange, int *readAgain) {
    // Do not read data if Widevine source is stopped
    if (mStopRead) {
        return;
    }
    Track *track;
    size_t maxBuffers = 1;
    switch (trackType) {
        case MEDIA_TRACK_TYPE_VIDEO:
            track = &mVideoTrack;
            if (mIsWidevine) {
                maxBuffers = 2;
            } else {
                if (mIsUdp)
                    maxBuffers = 1;
                else
                    maxBuffers = 4;
            }
            break;
        case MEDIA_TRACK_TYPE_AUDIO:
            track = &mAudioTrack;
            if (mIsWidevine) {
                maxBuffers = 8;
            } else {
                if (mIsUdp)
                    maxBuffers = 1;
                else
                    maxBuffers = 6;
            }
            break;
        case MEDIA_TRACK_TYPE_SUBTITLE:
            track = &mSubtitleTrack;
            break;
        case MEDIA_TRACK_TYPE_TIMEDTEXT:
            track = &mTimedTextTrack;
            break;
        default:
            TRESPASS();
    }

    if (track->mSource == NULL) {
        return;
    }

    if (actualTimeUs) {
        *actualTimeUs = seekTimeUs;
    }

    MediaSource::ReadOptions options;

    bool seeking = false;

    if (seekTimeUs >= 0) {
        options.setSeekTo(seekTimeUs, MediaSource::ReadOptions::SEEK_PREVIOUS_SYNC);
        seeking = true;
        Vector<MediaBuffer*>::iterator it = mDVMediaBuffer.begin();
        while (it != mDVMediaBuffer.end()) {
            MediaBuffer *mediabuf = *it;
            mediabuf->release();
            ++it;
        }
        mDVMediaBuffer.clear();
    }

    if (mIsWidevine) {
        options.setNonBlocking();
    }
    bool couldReadMultiple = (!mIsWidevine && trackType == MEDIA_TRACK_TYPE_AUDIO && !mIsUdp);
    for (size_t numBuffers = 0; numBuffers < maxBuffers; ) {
        Vector<MediaBuffer *> mediaBuffers;
        status_t err = NO_ERROR;

        if (!seeking && couldReadMultiple) {
            err = track->mSource->readMultiple(&mediaBuffers, (maxBuffers - numBuffers));
        } else {
            MediaBuffer *mbuf = NULL;
            err = track->mSource->read(&mbuf, &options);
            if (err == OK && mbuf != NULL) {
                mediaBuffers.push_back(mbuf);
            }
            else if (err == ERROR_READ_TIME_OUT && readAgain) {
                *readAgain = 1;
                break;
            }
        }

        options.clearSeekTo();

        size_t id = 0;
        size_t count = mediaBuffers.size();
        for (; id < count; ++id) {
            int64_t timeUs;
            int64_t dtsTime;
            MediaBuffer *mbuf = mediaBuffers[id];
            if (!mbuf->meta_data()->findInt64(kKeyTime, &timeUs)) {
                mbuf->meta_data()->dumpToLog();
                track->mPackets->signalEOS(ERROR_MALFORMED);
                break;
            }
            if (!mbuf->meta_data()->findInt64(kKeyDTSFromContainer, &dtsTime)) {
                ALOGE("no dts found\n");
            }
            if (trackType == MEDIA_TRACK_TYPE_AUDIO) {
                if (mAudiofirstQueueTimeUs < 0) mAudiofirstQueueTimeUs = timeUs;
                mAudioTimeUs = timeUs;
                mBufferingMonitor->updateQueuedTime(true /* isAudio */, timeUs);
            } else if (trackType == MEDIA_TRACK_TYPE_VIDEO) {
                if (mVideofirstQueueTimeUs < 0) mVideofirstQueueTimeUs = timeUs;
                mVideoTimeUs = timeUs;
                mBufferingMonitor->updateQueuedTime(false /* isAudio */, timeUs);
                mAmSubtitle->setSubStartPts(mbuf);//prepare subtitle start pts
            }

            queueDiscontinuityIfNeeded(seeking, formatChange, trackType, track);

            if ((trackType == MEDIA_TRACK_TYPE_SUBTITLE && mAmSubtitle->getSubFlag())
                || trackType == MEDIA_TRACK_TYPE_TIMEDTEXT) {//timed text show by google default
                mAmSubtitle->sendToSubtitleService(mbuf);
            }

            sp<ABuffer> buffer;
            if (trackType == MEDIA_TRACK_TYPE_VIDEO && mDVTrack.mSource != NULL) { //merge dv
                MediaBuffer *tmpbuf;
                int64_t tmptimeUs, tmptimeUs1;
                int index = 0;
                int waitelcount = 0;
                bool findavailabledts = false;
                //ALOGI("need to merge dts %lld\n", dtsTime);
                do {
                    MediaBuffer *buf, *copybuf;
                    err = mDVTrack.mSource->read(&buf, NULL);
                    if (err != OK)
                        break;
                    copybuf = new MediaBuffer(buf->range_length());
                    copybuf->set_range(0, buf->range_length());
                    buf->meta_data()->findInt64(kKeyDTSFromContainer, &tmptimeUs1);
                    copybuf->meta_data()->setInt64(kKeyDTSFromContainer, tmptimeUs1);
                    //ALOGI("read el dts:%lld\n", tmptimeUs1);
                    memcpy((uint8_t *)copybuf->data() + copybuf->range_offset(),
                        (const uint8_t *)buf->data() + buf->range_offset(), buf->range_length());
                    mDVMediaBuffer.push_back(copybuf);
                    buf->release();
                    buf = NULL;
                    if (mDVMediaBuffer.size() > index) {
                        for (int i = index ; i < mDVMediaBuffer.size(); i++,index++) {
                            mDVMediaBuffer[i]->meta_data()->findInt64(kKeyDTSFromContainer, &tmptimeUs);
                            //ALOGI("tmptimeUs %lld timeUs %lld",(long long)tmptimeUs,(long long)timeUs);
                            if (abs(dtsTime - tmptimeUs) <= kDVDtsDiffThreshold) {
                                tmpbuf = mDVMediaBuffer[i];
                               // ALOGI("size %d, index=%d\n",mDVMediaBuffer.size(), index);
                                mDVMediaBuffer.erase(mDVMediaBuffer.begin()+i); //mDVMediaBuffer[i]
                                findavailabledts = true;
                                break;
                            }
                        }
                    }
                    if (!findavailabledts)
                        ALOGI("need to read dv mediabuffer again\n");
                    //abnormal dv stream not support now
                    if (waitelcount ++ > 10) {
                        //if (mDVTrack.mSource != NULL)
                        //    mDVTrack.mPackets->signalEOS(ERROR_UNSUPPORTED);
                        if (mAudioTrack.mSource != NULL)
                            mAudioTrack.mPackets->signalEOS(ERROR_UNSUPPORTED);
                        if (mVideoTrack.mSource != NULL)
                            mVideoTrack.mPackets->signalEOS(ERROR_UNSUPPORTED);
                        return;
                    }
                } while (!findavailabledts);
                if (err == OK) {
                    //ALOGI("mergeMediaBufferandtoABuffer %lld\n",(long long)timeUs);
                    sp<ABuffer> buffer = mergeElMetataToBl(mbuf,tmpbuf, trackType, seekTimeUs,numBuffers == 0 ? actualTimeUs : NULL);
                    ALOGI("bufflen= %d %d %d\n", mbuf->range_length(), tmpbuf->range_length(), buffer->size());
                    if (buffer == NULL)
                        ALOGE("[%s:%d] this should not enter\n", __FUNCTION__, __LINE__);
                    track->mPackets->queueAccessUnit(buffer);
                }
             } else {
                sp<ABuffer> buffer = mediaBufferToABuffer(
                        mbuf, trackType, seekTimeUs,
                        numBuffers == 0 ? actualTimeUs : NULL);
                track->mPackets->queueAccessUnit(buffer);
            }
            formatChange = false;
            seeking = false;
            ++numBuffers;
        }
        if (id < count) {
            // Error, some mediaBuffer doesn't have kKeyTime.
            for (; id < count; ++id) {
                mediaBuffers[id]->release();
            }
            break;
        }

        if (err == WOULD_BLOCK) {
            break;
        } else if (err == INFO_FORMAT_CHANGED) {
#if 0
            track->mPackets->queueDiscontinuity(
                    ATSParser::DISCONTINUITY_FORMATCHANGE,
                    NULL,
                    false /* discard */);
#endif
        } else if (err != OK) {
            queueDiscontinuityIfNeeded(seeking, formatChange, trackType, track);
            ALOGI("err %d",err);
            track->mPackets->signalEOS(err);
            break;
        }
    }
}

sp<ABuffer> AmNuPlayer::GenericSource::mergeElMetataToBl(
    MediaBuffer* mb,
    MediaBuffer* second,
    media_track_type trackType,
    int64_t /* seekTimeUs */,
    int64_t *actualTimeUs) {// modify mediaBufferToABuffer
        bool audio = trackType == MEDIA_TRACK_TYPE_AUDIO;
        size_t outLength = mb->range_length() + second->range_length();
        size_t elLength = second->range_length();
        int start_code_length = 0;
        const char dolbyvision_eheader_4[6] = {0x00, 0x00, 0x00, 0x01, 0x7e, 0x01};
        const char dolbyvision_eheader_3[5] = {0x00, 0x00, 0x01, 0x7e, 0x01};
#define MAX_EL_UNIT_COUNT (20)
        if (audio && mAudioIsVorbis) {
            outLength += sizeof(int32_t);
        }
        sp<ABuffer> ab;
        if (mIsSecure && !audio) {
             // data is already provided in the buffer
             ab = new ABuffer(NULL, mb->range_length());
             mb->add_ref();
             ab->setMediaBufferBase(mb);
             ALOGE("%s:%d",__func__,__LINE__);
         } else {
             char *pdata = (char *)second->data()  + second->range_offset();
             if (pdata[0] == 0x00 && pdata[1] == 0x00 && pdata[2] == 0x00 && pdata[3] == 0x01) {
                start_code_length = 4;
             } else if (pdata[0] == 0x00 && pdata[1] == 0x00 && pdata[2] == 0x01) {
                start_code_length = 3;
             } else {
                ALOGE("%s:%d should not enter here~\n",__func__,__LINE__);
             }
             int startcode_index[MAX_EL_UNIT_COUNT];
             int el_unitcnt = 0;
             if (start_code_length == 4) {
                 for (int i = 0; i < elLength - 6; i++) {
                    if (pdata[i] == 0x00 && pdata[i+1] == 0x00 && pdata[i+2] == 0x00 && pdata[i+3] == 0x01) {
                        startcode_index[el_unitcnt] = i;
                        el_unitcnt ++;
                        if (pdata[i+4] == 0x7c && pdata[5] == 0x01) {
                            //ALOGI("[%s:%d] find the metadata!\n",__func__,__LINE__);
                            break;
                        }
                    }
                 }
                 //ALOGI("el_unitcnt=%d\n", el_unitcnt);
                 outLength += (el_unitcnt - 1) * 2 ;
                 ab = new ABuffer(outLength);
                 memcpy(ab->data(),
                    (const uint8_t *)mb->data() + mb->range_offset(),
                    mb->range_length());
                 pdata = (char *)second->data()  + second->range_offset();
                 int buflen = mb->range_length();
                 //add 0x7e01 el header
                 for (int j = 0; j < el_unitcnt -1; j ++) {
                    memcpy(ab->data() + buflen, dolbyvision_eheader_4, 6);
                    buflen += 6;
                    pdata += 4;
                    //ALOGI("startcode_index[j+1]=%d %d\n", startcode_index[j+1], startcode_index[j]);
                    memcpy(ab->data() + buflen, pdata, startcode_index[j+1] - startcode_index[j] - 4);
                    buflen += startcode_index[j+1] - startcode_index[j] - 4;
                    pdata += startcode_index[j+1] - startcode_index[j] - 4;
                 }
                 memcpy(ab->data() + buflen, pdata, outLength - buflen);
             } else if (start_code_length == 3) {
                 for (int i = 0; i < elLength - 5; i++) {
                    if (pdata[i] == 0x00 && pdata[i+1] == 0x00 && pdata[i+2] == 0x01) {
                        startcode_index[el_unitcnt] = i;
                        el_unitcnt ++;
                        if (pdata[i+3] == 0x7c && pdata[4] == 0x01) {
                            //ALOGI("[%s:%d] find the metadata!\n",__func__,__LINE__);
                            break;
                        }
                    }
                 }
                 //ALOGI("el_unitcnt=%d\n", el_unitcnt);
                 outLength += (el_unitcnt - 1) * 2 ;
                 ab = new ABuffer(outLength);
                 memcpy(ab->data(),
                    (const uint8_t *)mb->data() + mb->range_offset(),
                    mb->range_length());
                 pdata = (char *)second->data()  + second->range_offset();
                 int buflen = mb->range_length();
                 //add 0x7e01 el header
                 for (int j = 0; j < el_unitcnt -1; j ++) {
                    memcpy(ab->data() + buflen, dolbyvision_eheader_3, 5);
                    buflen += 5;
                    pdata += 3;
                    //ALOGI("startcode_index[j+1]=%d %d\n", startcode_index[j+1], startcode_index[j]);
                    memcpy(ab->data() + buflen, pdata, startcode_index[j+1] - startcode_index[j] - 3);
                    buflen += startcode_index[j+1] - startcode_index[j] - 3;
                    pdata += startcode_index[j+1] - startcode_index[j] - 3;
                 }
                 memcpy(ab->data() + buflen, pdata, outLength - buflen);
             } else {
                ALOGE("[%s:%d] this should not enter!\n",__func__,__LINE__);
                return NULL;
             }

             if (dv_fpbl) {
                fwrite((const uint8_t *)mb->data() + mb->range_offset(), mb->range_length(), 1, dv_fpbl);
             }
             if (dv_fpel) {
                fwrite((const uint8_t *)second->data()  + second->range_offset(), second->range_length(), 1, dv_fpel);
             }
             if (dv_fpbel) {
                fwrite((const uint8_t *)mb->data() + mb->range_offset(), mb->range_length(), 1, dv_fpbel);
                fwrite((const uint8_t *)second->data()  + second->range_offset(), second->range_length(), 1, dv_fpbel);
             }
         }

         sp<AMessage> meta = ab->meta();

         int64_t timeUs;
         CHECK(mb->meta_data()->findInt64(kKeyTime, &timeUs));
         meta->setInt64("timeUs", timeUs);


         int64_t durationUs;
         if (mb->meta_data()->findInt64(kKeyDuration, &durationUs)) {
            meta->setInt64("durationUs", durationUs);
         }

         if (trackType == MEDIA_TRACK_TYPE_SUBTITLE) {
            meta->setInt32("trackIndex", mSubtitleTrack.mIndex);
         }

         uint32_t dataType; // unused
         const void *seiData;
         size_t seiLength;
         if (mb->meta_data()->findData(kKeySEI, &dataType, &seiData, &seiLength)) {
            sp<ABuffer> sei = ABuffer::CreateAsCopy(seiData, seiLength);
            meta->setBuffer("sei", sei);
         }

         const void *mpegUserDataPointer;
         size_t mpegUserDataLength;
         if (mb->meta_data()->findData(
            kKeyMpegUserData, &dataType, &mpegUserDataPointer, &mpegUserDataLength)) {
            sp<ABuffer> mpegUserData = ABuffer::CreateAsCopy(mpegUserDataPointer, mpegUserDataLength);
            meta->setBuffer("mpegUserData", mpegUserData);
         }

         mb->release();
         second->release();

         if (actualTimeUs) {
            *actualTimeUs = timeUs;
         }

         return ab;
}

void AmNuPlayer::GenericSource::queueDiscontinuityIfNeeded(
        bool seeking, bool formatChange, media_track_type trackType, Track *track) {
    // formatChange && seeking: track whose source is changed during selection
    // formatChange && !seeking: track whose source is not changed during selection
    // !formatChange: normal seek
    if ((seeking || formatChange)
            && (trackType == MEDIA_TRACK_TYPE_AUDIO
            || trackType == MEDIA_TRACK_TYPE_VIDEO)) {
        AmATSParser::DiscontinuityType type = (formatChange && seeking)
                ? AmATSParser::DISCONTINUITY_FORMATCHANGE
                : AmATSParser::DISCONTINUITY_NONE;
        track->mPackets->queueDiscontinuity(type, NULL /* extra */, true /* discard */);
    }
}

AmNuPlayer::GenericSource::BufferingMonitor::BufferingMonitor(const sp<AMessage> &notify)
    : mNotify(notify),
      mDurationUs(-1ll),
      mBitrate(-1ll),
      mIsStreaming(false),
      mAudioTimeUs(0),
      mVideoTimeUs(0),
      mPollBufferingGeneration(0),
      mPrepareBuffering(false),
      mBuffering(false),
      mPrevBufferPercentage(-1),
      mOffloadAudio(false),
      mFirstDequeuedBufferRealUs(-1ll),
      mFirstDequeuedBufferMediaUs(-1ll),
      mlastDequeuedBufferMediaUs(-1ll) {
}

AmNuPlayer::GenericSource::BufferingMonitor::~BufferingMonitor() {
}

void AmNuPlayer::GenericSource::BufferingMonitor::prepare(
        const sp<NuCachedSource2> &cachedSource,
        const sp<WVMExtractor> &wvmExtractor,
        int64_t durationUs,
        int64_t bitrate,
        bool isStreaming) {
    Mutex::Autolock _l(mLock);
    prepare_l(cachedSource, wvmExtractor, durationUs, bitrate, isStreaming);
}

void AmNuPlayer::GenericSource::BufferingMonitor::stop() {
    Mutex::Autolock _l(mLock);
    prepare_l(NULL /* cachedSource */, NULL /* wvmExtractor */, -1 /* durationUs */,
            -1 /* bitrate */, false /* isStreaming */);
}

void AmNuPlayer::GenericSource::BufferingMonitor::cancelPollBuffering() {
    Mutex::Autolock _l(mLock);
    cancelPollBuffering_l();
}

void AmNuPlayer::GenericSource::BufferingMonitor::restartPollBuffering() {
    Mutex::Autolock _l(mLock);
    if (mIsStreaming) {
        cancelPollBuffering_l();
        onPollBuffering_l();
    }
}

void AmNuPlayer::GenericSource::BufferingMonitor::stopBufferingIfNecessary() {
    Mutex::Autolock _l(mLock);
    stopBufferingIfNecessary_l();
}

void AmNuPlayer::GenericSource::BufferingMonitor::ensureCacheIsFetching() {
    Mutex::Autolock _l(mLock);
    ensureCacheIsFetching_l();
}

void AmNuPlayer::GenericSource::BufferingMonitor::updateQueuedTime(bool isAudio, int64_t timeUs) {
    Mutex::Autolock _l(mLock);
    if (isAudio) {
        mAudioTimeUs = timeUs;
    } else {
        mVideoTimeUs = timeUs;
    }
}

void AmNuPlayer::GenericSource::BufferingMonitor::setOffloadAudio(bool offload) {
    Mutex::Autolock _l(mLock);
    mOffloadAudio = offload;
}

void AmNuPlayer::GenericSource::BufferingMonitor::updateDequeuedBufferTime(int64_t mediaUs) {
    Mutex::Autolock _l(mLock);
    if (mediaUs < 0) {
        mFirstDequeuedBufferRealUs = -1ll;
        mFirstDequeuedBufferMediaUs = -1ll;
    } else if (mFirstDequeuedBufferRealUs < 0) {
        mFirstDequeuedBufferRealUs = ALooper::GetNowUs();
        mFirstDequeuedBufferMediaUs = mediaUs;
    }
    mlastDequeuedBufferMediaUs = mediaUs;
}

void AmNuPlayer::GenericSource::BufferingMonitor::prepare_l(
        const sp<NuCachedSource2> &cachedSource,
        const sp<WVMExtractor> &wvmExtractor,
        int64_t durationUs,
        int64_t bitrate,
        bool isStreaming) {
    ALOGW_IF(wvmExtractor != NULL && cachedSource != NULL,
            "WVMExtractor and NuCachedSource are both present when "
            "BufferingMonitor::prepare_l is called, ignore NuCachedSource");

    mCachedSource = cachedSource;
    mWVMExtractor = wvmExtractor;
    mDurationUs = durationUs;
    mBitrate = bitrate;
    mIsStreaming = isStreaming;
    mAudioTimeUs = 0;
    mVideoTimeUs = 0;
    mPrepareBuffering = (cachedSource != NULL || wvmExtractor != NULL);
    cancelPollBuffering_l();
    mOffloadAudio = false;
    mFirstDequeuedBufferRealUs = -1ll;
    mFirstDequeuedBufferMediaUs = -1ll;
    mlastDequeuedBufferMediaUs = -1ll;
}

void AmNuPlayer::GenericSource::BufferingMonitor::cancelPollBuffering_l() {
    mBuffering = false;
    ++mPollBufferingGeneration;
    mPrevBufferPercentage = -1;
}

void AmNuPlayer::GenericSource::BufferingMonitor::notifyBufferingUpdate_l(int32_t percentage) {
    // Buffering percent could go backward as it's estimated from remaining
    // data and last access time. This could cause the buffering position
    // drawn on media control to jitter slightly. Remember previously reported
    // percentage and don't allow it to go backward.
    if (percentage < mPrevBufferPercentage) {
        percentage = mPrevBufferPercentage;
    } else if (percentage > 100) {
        percentage = 100;
    }

    mPrevBufferPercentage = percentage;

    ALOGV("notifyBufferingUpdate_l: buffering %d%%", percentage);

    sp<AMessage> msg = mNotify->dup();
    msg->setInt32("what", kWhatBufferingUpdate);
    msg->setInt32("percentage", percentage);
    msg->post();
}

void AmNuPlayer::GenericSource::BufferingMonitor::startBufferingIfNecessary_l() {
    if (mPrepareBuffering) {
        return;
    }

    if (!mBuffering) {
        ALOGD("startBufferingIfNecessary_l");

        mBuffering = true;

        ensureCacheIsFetching_l();
        sendCacheStats_l();

        sp<AMessage> notify = mNotify->dup();
        notify->setInt32("what", kWhatPauseOnBufferingStart);
        notify->post();
    }
}

void AmNuPlayer::GenericSource::BufferingMonitor::stopBufferingIfNecessary_l() {
    if (mPrepareBuffering) {
        ALOGI("stopBufferingIfNecessary_l, mBuffering=%d", mBuffering);

        mPrepareBuffering = false;

        sp<AMessage> notify = mNotify->dup();
        notify->setInt32("what", kWhatPrepared);
        notify->setInt32("err", OK);
        notify->post();

        return;
    }

    if (mBuffering) {
        ALOGD("stopBufferingIfNecessary_l");
        mBuffering = false;

        sendCacheStats_l();

        sp<AMessage> notify = mNotify->dup();
        notify->setInt32("what", kWhatResumeOnBufferingEnd);
        notify->post();
    }
}

void AmNuPlayer::GenericSource::BufferingMonitor::sendCacheStats_l() {
    int32_t kbps = 0;
    status_t err = UNKNOWN_ERROR;

    if (mWVMExtractor != NULL) {
        err = mWVMExtractor->getEstimatedBandwidthKbps(&kbps);
    } else if (mCachedSource != NULL) {
        err = mCachedSource->getEstimatedBandwidthKbps(&kbps);
    }

    if (err == OK) {
        sp<AMessage> notify = mNotify->dup();
        notify->setInt32("what", kWhatCacheStats);
        notify->setInt32("bandwidth", kbps);
        notify->post();
    }
}

void AmNuPlayer::GenericSource::BufferingMonitor::ensureCacheIsFetching_l() {
    if (mCachedSource != NULL) {
        mCachedSource->resumeFetchingIfNecessary();
    }
}

void AmNuPlayer::GenericSource::BufferingMonitor::schedulePollBuffering_l() {
    sp<AMessage> msg = new AMessage(kWhatPollBuffering, this);
    msg->setInt32("generation", mPollBufferingGeneration);
    // Enquires buffering status every second.
    msg->post(1000000ll);
}

int64_t AmNuPlayer::GenericSource::BufferingMonitor::getLastReadPosition_l() {
    if (mAudioTimeUs > 0) {
        return mAudioTimeUs;
    } else if (mVideoTimeUs > 0) {
        return mVideoTimeUs;
    } else {
        return 0;
    }
}

void AmNuPlayer::GenericSource::BufferingMonitor::onPollBuffering_l() {
    status_t finalStatus = UNKNOWN_ERROR;
    int64_t cachedDurationUs = -1ll;
    ssize_t cachedDataRemaining = -1;

    if (mWVMExtractor != NULL) {
        cachedDurationUs =
                mWVMExtractor->getCachedDurationUs(&finalStatus);
    } else if (mCachedSource != NULL) {
        cachedDataRemaining =
                mCachedSource->approxDataRemaining(&finalStatus);

        if (finalStatus == OK) {
            off64_t size;
            int64_t bitrate = 0ll;
            if (mDurationUs > 0 && mCachedSource->getSize(&size) == OK) {
                // |bitrate| uses bits/second unit, while size is number of bytes.
                bitrate = size * 8000000ll / mDurationUs;
            } else if (mBitrate > 0) {
                bitrate = mBitrate;
            }
            if (bitrate > 0) {
                cachedDurationUs = cachedDataRemaining * 8000000ll / bitrate;
            }
        }
    }

    if (finalStatus != OK) {
        ALOGV("onPollBuffering_l: EOS (finalStatus = %d)", finalStatus);

        if (finalStatus == ERROR_END_OF_STREAM) {
            notifyBufferingUpdate_l(100);
        }

        stopBufferingIfNecessary_l();
        return;
    } else if (cachedDurationUs >= 0ll) {
        if (mDurationUs > 0ll) {
            int64_t cachedPosUs = getLastReadPosition_l() + cachedDurationUs;
            int percentage = 100.0 * cachedPosUs / mDurationUs;
            if (percentage > 100) {
                percentage = 100;
            }

            notifyBufferingUpdate_l(percentage);
        }

        ALOGV("onPollBuffering_l: cachedDurationUs %.1f sec",
                cachedDurationUs / 1000000.0f);

        if (cachedDurationUs < kLowWaterMarkUs) {
            // Take into account the data cached in downstream components to try to avoid
            // unnecessary pause.
            if (mOffloadAudio && mFirstDequeuedBufferRealUs >= 0) {
                int64_t downStreamCacheUs = mlastDequeuedBufferMediaUs - mFirstDequeuedBufferMediaUs
                        - (ALooper::GetNowUs() - mFirstDequeuedBufferRealUs);
                if (downStreamCacheUs > 0) {
                    cachedDurationUs += downStreamCacheUs;
                }
            }

            if (cachedDurationUs < kLowWaterMarkUs) {
                startBufferingIfNecessary_l();
            }
        } else {
            int64_t highWaterMark = mPrepareBuffering ? kHighWaterMarkUs : kHighWaterMarkRebufferUs;
            if (cachedDurationUs > highWaterMark) {
                stopBufferingIfNecessary_l();
            }
        }
    } else if (cachedDataRemaining >= 0) {
        ALOGV("onPollBuffering_l: cachedDataRemaining %zd bytes",
                cachedDataRemaining);

        if (cachedDataRemaining < kLowWaterMarkBytes) {
            startBufferingIfNecessary_l();
        } else if (cachedDataRemaining > kHighWaterMarkBytes) {
            stopBufferingIfNecessary_l();
        }
    }

    schedulePollBuffering_l();
}

void AmNuPlayer::GenericSource::BufferingMonitor::onMessageReceived(const sp<AMessage> &msg) {
    switch (msg->what()) {
      case kWhatPollBuffering:
      {
          int32_t generation;
          CHECK(msg->findInt32("generation", &generation));
          Mutex::Autolock _l(mLock);
          if (generation == mPollBufferingGeneration) {
              onPollBuffering_l();
          }
          break;
      }
      default:
          TRESPASS();
          break;
    }
}

}  // namespace android
