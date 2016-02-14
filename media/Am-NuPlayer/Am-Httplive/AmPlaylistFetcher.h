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

#ifndef PLAYLIST_FETCHER_H_

#define PLAYLIST_FETCHER_H_

#include <media/stagefright/foundation/AHandler.h>

#include "AmATSParser.h"
#include "AmLiveSession.h"

namespace android {

struct ABuffer;
struct AmAnotherPacketSource;
struct DataSource;
struct HTTPBase;
struct AmLiveDataSource;
struct AmM3UParser;
struct MediaExtractor;
struct MediaBuffer;
struct String8;

struct AmPlaylistFetcher : public AHandler {
    static const int64_t kMinBufferedDurationUs;
    static const int32_t kDownloadBlockSize;

    enum {
        kWhatStarted,
        kWhatPaused,
        kWhatStopped,
        kWhatError,
        kWhatDurationUpdate,
        kWhatTemporarilyDoneFetching,
        kWhatPrepared,
        kWhatPreparationFailed,
        kWhatStartedAt,
        kWhatCodecSpecificData,
    };

    AmPlaylistFetcher(
            const sp<AMessage> &notify,
            const sp<AmLiveSession> &session,
            const sp<AmM3UParser> &playlist,
            const char *uri,
            int32_t subtitleGeneration);

    sp<DataSource> getDataSource();

    void startAsync(
            const sp<AmAnotherPacketSource> &audioSource,
            const sp<AmAnotherPacketSource> &videoSource,
            const sp<AmAnotherPacketSource> &subtitleSource,
            int64_t startTimeUs = -1ll,         // starting timestamps
            int64_t segmentStartTimeUs = -1ll, // starting position within playlist
            // startTimeUs!=segmentStartTimeUs only when playlist is live
            int32_t startDiscontinuitySeq = 0,
            bool adaptive = false);

    void pauseAsync();

    void stopAsync(bool clear = true);

    void changeURI(AString uri);
    uint32_t getStreamTypeMask();
    void setStreamTypeMask(uint32_t streamMask);

    void resumeUntilAsync(const sp<AMessage> &params);

    uint32_t getStreamTypeMask() const {
        return mStreamTypeMask;
    }

    int32_t getSeqNumberForTime(int64_t timeUs) const;
    // Returns the media time in us of the segment specified by seqNumber.
    // This is computed by summing the durations of all segments before it.
    int64_t getSegmentStartTimeUs(int32_t seqNumber) const;

protected:
    virtual ~AmPlaylistFetcher();
    virtual void onMessageReceived(const sp<AMessage> &msg);

private:
    enum {
        kMaxNumRetries         = 5,
    };

    enum {
        kWhatStart          = 'strt',
        kWhatPause          = 'paus',
        kWhatStop           = 'stop',
        kWhatMonitorQueue   = 'moni',
        kWhatResumeUntil    = 'rsme',
        kWhatDownloadNext   = 'dlnx',
    };

    static const AString DumpPath;
    int32_t mDumpMode; // 1: one whole file; 2: independent file
    FILE * mDumpHandle;

    int64_t mSegmentBytesPerSec;

    int64_t mFailureAnchorTimeUs;
    int64_t mOpenFailureRetryUs;

    static const int64_t kMaxMonitorDelayUs;
    static const int32_t kNumSkipFrames;

    static bool bufferStartsWithTsSyncByte(const sp<ABuffer>& buffer);
    static bool bufferStartsWithWebVTTMagicSequence(const sp<ABuffer>& buffer);

    // notifications to mSession
    sp<AMessage> mNotify;
    sp<AMessage> mStartTimeUsNotify;

    sp<AmLiveSession> mSession;
    AString mURI;

    uint32_t mStreamTypeMask;
    int64_t mStartTimeUs;

    // Start time relative to the beginning of the first segment in the initial
    // playlist. It's value is initialized to a non-negative value only when we are
    // adapting or switching tracks.
    int64_t mSegmentStartTimeUs;

    ssize_t mDiscontinuitySeq;
    bool mStartTimeUsRelative;
    sp<AMessage> mStopParams; // message containing the latest timestamps we should fetch.

    KeyedVector<AmLiveSession::StreamType, sp<AmAnotherPacketSource> >
        mPacketSources;

    KeyedVector<AString, sp<ABuffer> > mAESKeyForURI;

    int64_t mLastPlaylistFetchTimeUs;
    sp<AmM3UParser> mPlaylist;
    int32_t mSeqNumber;
    int32_t mDownloadedNum;
    int32_t mNumRetries;
    bool mNeedSniff;
    bool mIsTs;
    bool mFirstRefresh;
    bool mFirstTypeProbe;
    bool mStartup;
    bool mAdaptive;
    bool mFetchingNotify;
    bool mPrepared;
    bool mPostPrepared;
    int64_t mNextPTSTimeUs;

    int32_t mMonitorQueueGeneration;
    const int32_t mSubtitleGeneration;

    enum RefreshState {
        INITIAL_MINIMUM_RELOAD_DELAY,
        FIRST_UNCHANGED_RELOAD_ATTEMPT,
        SECOND_UNCHANGED_RELOAD_ATTEMPT,
        THIRD_UNCHANGED_RELOAD_ATTEMPT
    };
    RefreshState mRefreshState;

    uint8_t mPlaylistHash[16];

    sp<AmATSParser> mTSParser;

    sp<MediaExtractor> mExtractor;
    sp<MediaSource> mAudioTrack;
    sp<MediaSource> mVideoTrack;
    sp<AmAnotherPacketSource> mAudioSource;
    sp<AmAnotherPacketSource> mVideoSource;

    bool mEnableFrameRate;
    Vector<int64_t> mVecTimeUs;
    static const size_t kFrameNum;

    bool mFirstPTSValid;
    uint64_t mFirstPTS;
    int64_t mFirstTimeUs;
    int64_t mAbsoluteTimeAnchorUs;
    sp<AmAnotherPacketSource> mVideoBuffer;

    // Stores the initialization vector to decrypt the next block of cipher text, which can
    // either be derived from the sequence number, read from the manifest, or copied from
    // the last block of cipher text (cipher-block chaining).
    unsigned char mAESInitVec[16];

    // Set first to true if decrypting the first segment of a playlist segment. When
    // first is true, reset the initialization vector based on the available
    // information in the manifest; otherwise, use the initialization vector as
    // updated by the last call to AES_cbc_encrypt.
    //
    // For the input to decrypt correctly, decryptBuffer must be called on
    // consecutive byte ranges on block boundaries, e.g. 0..15, 16..47, 48..63,
    // and so on.
    status_t decryptBuffer(
            size_t playlistIndex, const sp<ABuffer> &buffer,
            bool first = true);
    status_t checkDecryptPadding(const sp<ABuffer> &buffer);

    void postMonitorQueue(int64_t delayUs = 0, int64_t minDelayUs = 0);
    void cancelMonitorQueue();

    int64_t delayUsToRefreshPlaylist() const;
    status_t refreshPlaylist();

    status_t onStart(const sp<AMessage> &msg);
    void onPause();
    void onStop(const sp<AMessage> &msg);
    void onMonitorQueue();
    void onDownloadNext();

    // Resume a fetcher to continue until the stopping point stored in msg.
    status_t onResumeUntil(const sp<AMessage> &msg);

    const sp<ABuffer> &setAccessUnitProperties(
            const sp<ABuffer> &accessUnit,
            const sp<AmAnotherPacketSource> &source,
            bool discard = false);
    size_t resyncTs(const uint8_t *data, size_t size);
    status_t extractAndQueueAccessUnitsFromTs(const sp<ABuffer> &buffer);
    status_t extractAndQueueAccessUnitsFromNonTs();
    status_t queueAccessUnits();
    status_t extractAndQueueAccessUnits(
            const sp<ABuffer> &buffer, const sp<AMessage> &itemMeta);

    void sniff(const sp<ABuffer> &buffer);
    void readFromNonTsFile();
    sp<ABuffer> mediaBufferToABuffer(MediaBuffer* mediaBuffer);

    void notifyError(status_t err);

    void queueDiscontinuity(
            AmATSParser::DiscontinuityType type, const sp<AMessage> &extra);

    int32_t getSeqNumberWithAnchorTime(int64_t anchorTimeUs) const;
    int32_t getSeqNumberForDiscontinuity(size_t discontinuitySeq) const;

    void updateDuration();

    // Before resuming a fetcher in onResume, check the remaining duration is longer than that
    // returned by resumeThreshold.
    int64_t resumeThreshold(const sp<AMessage> &msg);

    DISALLOW_EVIL_CONSTRUCTORS(AmPlaylistFetcher);
};

}  // namespace android

#endif  // PLAYLIST_FETCHER_H_

