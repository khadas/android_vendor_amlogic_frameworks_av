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

#ifndef HTTP_LIVE_SOURCE_H_

#define HTTP_LIVE_SOURCE_H_

#include "AmNuPlayer.h"
#include "AmNuPlayerSource.h"
#include "AmLiveSession.h"

namespace android {

struct AmLiveSession;

struct AmNuPlayer::HTTPLiveSource : public AmNuPlayer::Source {
    HTTPLiveSource(
            const sp<AMessage> &notify,
            const sp<IMediaHTTPService> &httpService,
            const char *url,
            const KeyedVector<String8, String8> *headers,
            interruptcallback pfunc);

    virtual void prepareAsync();
    virtual void start();

    virtual status_t dequeueAccessUnit(bool audio, sp<ABuffer> *accessUnit);
    virtual sp<AMessage> getFormat(bool audio);
    virtual void disconnect();

    virtual status_t feedMoreTSData();
    virtual status_t getDuration(int64_t *durationUs);
    virtual size_t getTrackCount() const;
    virtual sp<AMessage> getTrackInfo(size_t trackIndex) const;
    virtual ssize_t getSelectedTrack(media_track_type /* type */) const;
    virtual status_t selectTrack(size_t trackIndex, bool select, int64_t timeUs);
    virtual status_t seekTo(int64_t seekTimeUs);
    virtual void setParentThreadId(android_thread_id_t thread_id);
protected:
    virtual ~HTTPLiveSource();

    virtual void onMessageReceived(const sp<AMessage> &msg);

private:
    enum Flags {
        // Don't log any URLs.
        kFlagIncognito = 1,
    };

    enum {
        kWhatSessionNotify,
        kWhatFetchSubtitleData,
        kWhatFetchMetaData,
    };

    sp<IMediaHTTPService> mHTTPService;
    AString mURL;
    KeyedVector<String8, String8> mExtraHeaders;
    uint32_t mFlags;
    status_t mFinalResult;
    off64_t mOffset;
    sp<ALooper> mLiveLooper;
    sp<AmLiveSession> mLiveSession;
    int32_t mFetchSubtitleDataGeneration;
    int32_t mFetchMetaDataGeneration;
    bool mHasMetadata;
    bool mMetadataSelected;

    interruptcallback mInterruptCallback;
    android_thread_id_t mParentThreadId;
    bool mBuffering;

    void onSessionNotify(const sp<AMessage> &msg);
    void pollForRawData(
            const sp<AMessage> &msg, int32_t currentGeneration,
            AmLiveSession::StreamType fetchType, int32_t pushWhat);

    DISALLOW_EVIL_CONSTRUCTORS(HTTPLiveSource);
};

}  // namespace android

#endif  // HTTP_LIVE_SOURCE_H_
