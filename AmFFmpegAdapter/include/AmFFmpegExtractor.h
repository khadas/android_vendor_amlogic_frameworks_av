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

#ifndef AM_FFMPEG_EXTRACTOR_H_
#define AM_FFMPEG_EXTRACTOR_H_

#include <media/stagefright/foundation/ABase.h>
#include <media/stagefright/MediaExtractor.h>
#include <media/stagefright/MediaSource.h>
#include <utils/threads.h>
#include <utils/Vector.h>
#include <utils/misc.h>

#include <media/stagefright/foundation/ABuffer.h>

struct AVInputFormat;
struct AVFormatContext;
struct AVCodecContext;

namespace android {

class AmFFmpegByteIOAdapter;
class AmPTSPopulator;
struct AMessage;
class DataSource;
struct AmFFmpegSource;
class String8;

/*
 * A MediaExtractor implementation based on the FFmpeg library.
 *
 * Limitations :
 * 1. This does not support multiple track. When the source file has many
 *    audio tracks, only the first track will be exposed.
 * 2. Due to the FFmpeg's API, exposed tracks can not be used in different
 *    contexts because positions of tracks are synced internally. This means
 *    that seek position should be set for all the exposed tracks. If different
 *    position is set, the returned data from read() operation of the track will
 *    be non-deterministic.
 */
struct AmFFmpegExtractor : public MediaExtractor {
    explicit AmFFmpegExtractor(const sp<DataSource> &source);

    virtual size_t countTracks();
    virtual sp<IMediaSource> getTrack(size_t index);
    virtual sp<MetaData> getTrackMetaData(size_t index, uint32_t flags);

    virtual sp<MetaData> getMetaData();

    virtual uint32_t flags() const;

    void seekTo(
            int64_t seekTimeUs, MediaSource::ReadOptions::SeekMode mode);

private:
    friend struct AmFFmpegSource;
    struct SourceInfo {
        SourceInfo();
        sp<AmFFmpegSource> mSource;
        bool mIsActive;
    };

    sp<ABuffer> mBuffer;
    List<sp<ABuffer> > mBuffers;
    status_t appendData(const void *data, size_t size);
    sp<ABuffer> parseSei();
    void queueAccessUnit(const sp<ABuffer> &buffer);
    status_t dequeueAccessUnit(sp<ABuffer> *buffer);

    sp<MetaData> mMeta;
    sp<DataSource> mDataSource;
    AVInputFormat *mInputFormat;
    sp<AmFFmpegByteIOAdapter> mSourceAdapter;
    sp<AmPTSPopulator> mPTSPopulator;

    Mutex mLock;
    // Start of protected variables by mLock.
    Vector<SourceInfo> mSources;
    AVFormatContext *mFFmpegContext;
    Vector<uint32_t> mStreamIdxToSourceIdx;
    int mReadPktCnt;
    // End of protected variables by mLock.

    int64_t mFirstVpts;
    bool mSeek;
    virtual ~AmFFmpegExtractor();
    void init();

    status_t feedMore();
    int32_t getPrimaryStreamIndex(AVFormatContext *context);
    bool checkStreamValid(AVCodecContext *codec);
    void release(AmFFmpegSource* source);

    //for debug info apk
    void prepareAudioInfo(sp<MetaData> meta);
    void prepareVideoInfo(sp<MetaData> meta);
    void prepareSubtitleInfo(sp<MetaData> meta);
    void resetInfo();

    DISALLOW_EVIL_CONSTRUCTORS(AmFFmpegExtractor);
};

bool SniffAmFFmpeg(
        const sp<DataSource> &source, String8 *mimeType, float *confidence,
        sp<AMessage> *);

}  // namespace android

#endif  // AM_FFMPEG_EXTRACTOR_H_
