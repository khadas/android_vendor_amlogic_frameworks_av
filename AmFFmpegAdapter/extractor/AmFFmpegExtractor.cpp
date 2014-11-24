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

//#define LOG_NDEBUG 0
#define LOG_TAG "AmFFmpegExtractor"
#include <utils/Log.h>

#include <AmFFmpegExtractor.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avio.h>
#include <libavformat/avformat.h>
#undef CodecType
}

#include <media/stagefright/DataSource.h>
#include <media/stagefright/MediaBuffer.h>
#include <media/stagefright/MediaBufferGroup.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/Utils.h>
#include <media/stagefright/foundation/ABase.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/hexdump.h>
#include <media/stagefright/AmMediaDefsExt.h>
#include <media/stagefright/AmMetaDataExt.h>
#include <media/stagefright/MediaDefs.h>

#include "AmPTSPopulator.h"
#include "AmFFmpegByteIOAdapter.h"
#include "AmFFmpegUtils.h"
#include "formatters/StreamFormatter.h"

namespace android {

static const uint32_t kInvalidSourceIdx = 0xFFFFFFFF;

static const size_t kDefaultFrameBufferSize = 512 * 1024;
static const size_t kMaxFrameBufferSize = 8 * 1024 * 1024;

struct AmFFmpegSource : public MediaSource {
    AmFFmpegSource(
            AmFFmpegExtractor *extractor,
            AVStream *stream,
            AVInputFormat *inputFormat,
            sp<AmPTSPopulator> &ptsPopulator,
            bool seekable,
            int64_t startTimeUs);

    virtual status_t start(MetaData *params = NULL);
    virtual status_t stop();
    virtual sp<MetaData> getFormat();

    virtual status_t read(
            MediaBuffer **buffer, const ReadOptions *options = NULL);

    // Callee retains the ownership of the packet.
    status_t queuePacket(AVPacket *packet);
    status_t clearPendingPackets();

private:
    wp<AmFFmpegExtractor> mExtractor;
    sp<MetaData> mMeta;
    sp<StreamFormatter> mFormatter;
    sp<AmPTSPopulator> mPTSPopulator;
    AVStream *mStream;

    Mutex mPacketQueueLock;
    List<AVPacket *> mPacketQueue;

    bool mStarted;
    bool mFirstPacket;
    bool mStartRead;
    MediaBufferGroup *mGroup;

    int64_t mStartTimeUs;

    int32_t mTimeBase;
    int32_t mNumerator;
    int32_t mDenominator;

    const char * mMime;

    // If there are both audio and video streams, only the video stream
    // will be seekable, otherwise the single stream will be seekable.
    bool mSeekable;

    int64_t mLastValidPts;
    int64_t mLastValidDts;

    virtual ~AmFFmpegSource();
    AVPacket *dequeuePacket();
    status_t init(
            AVStream *stream, AVInputFormat *inputFormat,
            AmFFmpegExtractor *extractor);
    int64_t convertStreamTimeToUs(int64_t timeInStreamTime);
    void resetBufferGroup(size_t size);

    DISALLOW_EVIL_CONSTRUCTORS(AmFFmpegSource);
};

AmFFmpegSource::AmFFmpegSource(
        AmFFmpegExtractor *extractor,
        AVStream *stream,
        AVInputFormat *inputFormat,
        sp<AmPTSPopulator> &ptsPopulator,
        bool seekable,
        int64_t startTimeUs)
    : mExtractor(extractor),
      mPTSPopulator(ptsPopulator),
      mStarted(false),
      mFirstPacket(true),
      mStartRead(false),
      mGroup(NULL),
      mStream(stream),
      mMime(NULL),
      mStartTimeUs(startTimeUs),
      mTimeBase(0),
      mNumerator(0),
      mDenominator(0),
      mSeekable(seekable) {
    init(stream, inputFormat, extractor);
}

AmFFmpegSource::~AmFFmpegSource() {
    if (mStarted) {
        stop();
    }
    clearPendingPackets();
}

status_t AmFFmpegSource::init(
        AVStream *stream, AVInputFormat *inputFormat,
        AmFFmpegExtractor *extractor) {
    CHECK(stream);
    CHECK(inputFormat);

    mTimeBase = AV_TIME_BASE;
    mNumerator = stream->time_base.num;
    mDenominator = stream->time_base.den;

    mMeta = new MetaData;
    sp<MetaData> parent = extractor->getMetaData();
    int64_t durationUs = 0LL;
    if (parent->findInt64(kKeyDuration, &durationUs)) {
        mMeta->setInt64(kKeyDuration, durationUs);
    }

    if (stream->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
        ALOGI("demux: extradata_size = %d", stream->codec->extradata_size);
        ALOGI("demux: fmt = %d\n", stream->codec->sample_fmt);

        if(stream->codec->extradata_size > 0){
            mMeta->setData(kKeyExtraData, 0, 
                           stream->codec->extradata, stream->codec->extradata_size);
            mMeta->setInt32(kKeyExtraDataSize, stream->codec->extradata_size);
        }else
            mMeta->setData(kKeyExtraData, 0, 
                           stream->codec->extradata, stream->codec->extradata_size);

        ALOGI("demux: block_align = %d\n", stream->codec->block_align);
        if(stream->codec->block_align >= 0){
            mMeta->setInt32(kKeyBlockAlign, stream->codec->block_align);
        }
		
        mMeta->setInt32(kKeySampleRate, stream->codec->sample_rate);
        mMeta->setInt32(kKeyBitRate, stream->codec->bit_rate);
        mMeta->setInt32(kKeyChannelCount, stream->codec->channels);
        mMeta->setInt32(kKeyCodecID, stream->codec->codec_id);
        int32_t bitsPerSample = -1;
        OMX_ENDIANTYPE dataEndian = OMX_EndianMax;
        OMX_NUMERICALDATATYPE dataSigned = OMX_NumercialDataMax;
        getPcmFormatFromCodecId(stream->codec, &bitsPerSample,
                &dataEndian, &dataSigned);

        if (-1 != bitsPerSample) {
            mMeta->setInt32(kKeyPCMBitsPerSample, bitsPerSample);
        }
        if (OMX_EndianMax != dataEndian) {
            mMeta->setInt32(
                    kKeyPCMDataEndian, static_cast<int32_t>(dataEndian));
        }
        if (OMX_NumercialDataMax != dataSigned) {
            mMeta->setInt32(
                    kKeyPCMDataSigned, static_cast<int32_t>(dataSigned));
        }
		if(stream->codec->codec_tag==354){
		   mMeta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_WMAPRO);
		}else{
		   mMeta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_WMA);
		}
//		mMeta->setInt32(kKeyCodecID,stream->codec->codec_tag);
    } else if (stream->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
        mMeta->setInt32(kKeyWidth, stream->codec->width);
        mMeta->setInt32(kKeyHeight, stream->codec->height);
        mMeta->setInt32(kKeyCodecID, stream->codec->codec_id);
        // NOTE: this framerate value is just a guess from FFmpeg. Decoder
        //       should get and use the real framerate from decoding.
        AVRational *rationalFramerate = NULL;
        if (stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0) {
            rationalFramerate = &(stream->avg_frame_rate);
        } else if (stream->r_frame_rate.num > 0 && stream->r_frame_rate.den > 0) {
            rationalFramerate = &(stream->r_frame_rate);
        }
        if (rationalFramerate != NULL) {
            uint64_t framerate = rationalFramerate->num;
            framerate <<= 16;  // convert to Q16 value.
            framerate /= rationalFramerate->den;
            mMeta->setInt32(kKeyFrameRateQ16, static_cast<int32_t>(framerate));
        }
		if(stream->codec->extradata_size > 0) {
	        mMeta->setData(kKeyExtraData, 0, (char*)stream->codec->extradata, stream->codec->extradata_size);
		    mMeta->setInt32(kKeyExtraDataSize, stream->codec->extradata_size);
		}
		if(stream->codec->block_align > 0)
		    mMeta->setInt32(kKeyBlockAlign, stream->codec->block_align);
    } else if (stream->codec->codec_type == AVMEDIA_TYPE_SUBTITLE) {
        if (stream->codec->codec_id == AV_CODEC_ID_MOV_TEXT) {
            // Add ISO-14496-12 atom header (BigEndian size + FOURCC tx3g),
            // to make output compatible with MPEG4Extractor output
            size_t size = stream->codec->extradata_size + 16;
            uint8_t *buffer = new uint8_t[size];
            memset(buffer, 0, 16);
            buffer[0] = size / 0x1000000;
            buffer[1] = size / 0x10000;
            buffer[2] = size / 0x100;
            buffer[3] = size % 0x100;
            buffer[4] = 't';
            buffer[5] = 'x';
            buffer[6] = '3';
            buffer[7] = 'g';
            memcpy(buffer + 16, stream->codec->extradata,
                    stream->codec->extradata_size);
            mMeta->setData(kKeyTextFormatData, 0, buffer, size);
            delete[] buffer;

            AVDictionaryEntry *lang =
                    av_dict_get(stream->metadata, "language", NULL, 0);
            mMeta->setCString(kKeyMediaLanguage, lang->value);

        }
    } else {
        ALOGW("Unsupported track type %u", stream->codec->codec_type);
        return ERROR_UNSUPPORTED;
    }

    mMime= convertCodecIdToMimeType(stream->codec);
    if (mMime) {
        mMeta->setCString(kKeyMIMEType, mMime);
    } else {
        return ERROR_UNSUPPORTED;
    }

    /** SoftAAC2 cannot decode adts extracted from ffmpeg because of no extradata, to use SoftADTS */
    if(!strcmp(mMime, MEDIA_MIMETYPE_AUDIO_AAC)
	&& (inputFormat == av_find_input_format("mpegts") ||
	    inputFormat == av_find_input_format("rm"))) {
        mMeta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_ADTS_PROFILE);
    }

    mFormatter = StreamFormatter::Create(stream->codec, inputFormat);
    mFormatter->addCodecMeta(mMeta);
    return OK;
}

status_t AmFFmpegSource::start(MetaData *params) {
    CHECK(!mStarted);

    resetBufferGroup(kDefaultFrameBufferSize);

    mStarted = true;

    return OK;
}

status_t AmFFmpegSource::stop() {
    CHECK(mStarted);

    delete mGroup;
    mGroup = NULL;
    mStarted = false;

    return OK;
}

sp<MetaData> AmFFmpegSource::getFormat() {
    return mMeta;
}

status_t AmFFmpegSource::read(
        MediaBuffer **out, const ReadOptions *options) {
    ALOGV("%s %d", __FUNCTION__, __LINE__);
    *out = NULL;

    sp<AmFFmpegExtractor> extractor = mExtractor.promote();
    if (NULL == extractor.get()) {
        // The client should hold AmFFmpegExtractor while it is using source.
        ALOGE("AmFFmpegExtractor has been released before stop using sources.");
        return UNKNOWN_ERROR;
    }

    int64_t seekTimeUs;
    ReadOptions::SeekMode seekMode;
    AVPacket *packet = NULL;
    if (mSeekable && options && options->getSeekTo(&seekTimeUs, &seekMode)) {
        // hevc decoder may fail because of no extradata when seek instantly after start.
        if(!strcmp(mMime, MEDIA_MIMETYPE_VIDEO_HEVC) && mStartRead == false
	    && mStream->codec->extradata_size == 0) {
	     packet = dequeuePacket();
            while (packet == NULL) {
                if (ERROR_END_OF_STREAM == extractor->feedMore()) {
                    return ERROR_END_OF_STREAM;
                }
                packet = dequeuePacket();
            }
	     int32_t cast_size = castHEVCSpecificData(packet->data, packet->size);
	     if(cast_size > 0) {
                av_shrink_packet(packet, cast_size);
            }
            ALOGI("Need send hevc specific data first, size : %d", packet->size);
	 }

        extractor->seekTo(seekTimeUs + mStartTimeUs, seekMode);
    }

    mStartRead = true;

    if(packet == NULL) {
        packet = dequeuePacket();
        while (packet == NULL) {
            if (ERROR_END_OF_STREAM == extractor->feedMore()) {
                return ERROR_END_OF_STREAM;
            }
            packet = dequeuePacket();
        }
    }

    MediaBuffer *buffer = NULL;
    status_t ret = mGroup->acquire_buffer(&buffer);
    if (ret != OK) {
        return ret;
    }

    uint32_t requiredLen =
            mFormatter->computeNewESLen(packet->data, packet->size);

    int32_t hevc_header_size = 0;
    if(mFirstPacket && !strcmp(mMime, MEDIA_MIMETYPE_VIDEO_HEVC) && mStream->codec->extradata_size > 0) {
        hevc_header_size = 10 + mStream->codec->extradata_size;
        requiredLen += hevc_header_size;
    }
    if (buffer->size() < requiredLen) {
        size_t newSize = buffer->size();
        while (newSize < requiredLen) {
            newSize = 2 * newSize;
            if (newSize > kMaxFrameBufferSize) {
                break;
            }
        }
        buffer->release();
        buffer = NULL;
        if (newSize > kMaxFrameBufferSize) {
            return ERROR_BUFFER_TOO_SMALL;
        }
        resetBufferGroup(newSize);
        status_t ret = mGroup->acquire_buffer(&buffer);
        if (ret != OK) {
            return ret;
        }
    }

    int32_t filledLength = 0;
    if(mFirstPacket && !strcmp(mMime, MEDIA_MIMETYPE_VIDEO_HEVC) && hevc_header_size > 0) {
        const char * tag = "extradata";
        memcpy(static_cast<uint8_t *>(buffer->data()), tag, 9);
	 static_cast<uint8_t *>(buffer->data())[9] = mStream->codec->extradata_size;
	 memcpy(static_cast<uint8_t *>(buffer->data()) + 10, static_cast<uint8_t *>(mStream->codec->extradata), mStream->codec->extradata_size);
        filledLength = mFormatter->formatES(
            packet->data, packet->size,
            static_cast<uint8_t *>(buffer->data()) + hevc_header_size, buffer->size());
	 filledLength += hevc_header_size;
    } else {
        filledLength = mFormatter->formatES(
                packet->data, packet->size,
                static_cast<uint8_t *>(buffer->data()), buffer->size());
    }
    mFirstPacket = false;
    if (filledLength <= 0) {
        ALOGE("Failed to format packet data.");
        buffer->release();
        buffer = NULL;
        return ERROR_MALFORMED;
    }

	if(AV_NOPTS_VALUE == packet->pts) {
        packet->pts = mLastValidPts;
        packet->dts = mLastValidDts;
        ALOGE("meet invalid pts, set last pts to current frame pts:%lld dts:%lld", mLastValidPts, mLastValidDts);
    } else {
        mLastValidPts = packet->pts;
        mLastValidDts = packet->dts;
    }

    buffer->set_range(0, filledLength);
    const bool isKeyFrame = (packet->flags & AV_PKT_FLAG_KEY) != 0;
    const int64_t ptsFromFFmpeg =
            (packet->pts == static_cast<int64_t>(AV_NOPTS_VALUE))
            ? kUnknownPTS : convertStreamTimeToUs(packet->pts);
    const int64_t dtsFromFFmpeg =
            (packet->dts == static_cast<int64_t>(AV_NOPTS_VALUE))
            ? kUnknownPTS : convertStreamTimeToUs(packet->dts);
    const int64_t predictedPTSInUs = mPTSPopulator->computePTS(
            packet->stream_index, ptsFromFFmpeg, dtsFromFFmpeg, isKeyFrame);
    const int64_t normalizedPTSInUs = (predictedPTSInUs == kUnknownPTS)?
            dtsFromFFmpeg - mStartTimeUs : ((predictedPTSInUs - mStartTimeUs < 0
            && predictedPTSInUs - mStartTimeUs > -10) ? 0 : predictedPTSInUs - mStartTimeUs); // starttime may exceed pts a little in some ugly streams.

    buffer->meta_data()->setInt64(kKeyPTSFromContainer, ptsFromFFmpeg);
    buffer->meta_data()->setInt64(kKeyDTSFromContainer, dtsFromFFmpeg);
    buffer->meta_data()->setInt64(kKeyMediaTimeOffset, -mStartTimeUs);

    // TODO: Make decoder know that this sample has no timestamp by setting
    // OMX_BUFFERFLAG_TIMESTAMPINVALID flag once we move to OpenMax IL 1.2.
    buffer->meta_data()->setInt64(kKeyTime, normalizedPTSInUs);
    buffer->meta_data()->setInt32(kKeyIsSyncFrame, isKeyFrame ? 1 : 0);
    *out = buffer;
    av_free_packet(packet);
    delete packet;
    return OK;
}

int64_t AmFFmpegSource::convertStreamTimeToUs(int64_t timeInStreamTime) {
    return timeInStreamTime * mTimeBase * mNumerator / mDenominator;
}

void AmFFmpegSource::resetBufferGroup(size_t size) {
    if (mGroup != NULL) {
        delete mGroup;
    }
    mGroup = new MediaBufferGroup();
    mGroup->add_buffer(new MediaBuffer(size));
}

status_t AmFFmpegSource::queuePacket(AVPacket *packet) {
    Mutex::Autolock autoLock(mPacketQueueLock);
    mPacketQueue.push_back(packet);
    return OK;
}

AVPacket *AmFFmpegSource::dequeuePacket() {
    Mutex::Autolock autoLock(mPacketQueueLock);
    if (!mPacketQueue.empty()) {
        AVPacket *packet = *mPacketQueue.begin();
        mPacketQueue.erase(mPacketQueue.begin());
        return packet;
    }
    return NULL;
}

status_t AmFFmpegSource::clearPendingPackets() {
    Mutex::Autolock autoLock(mPacketQueueLock);
    List<AVPacket *>::iterator it = mPacketQueue.begin();
    while (it != mPacketQueue.end()) {
        AVPacket *packet = *it;
        av_free_packet(packet);
        delete packet;
        ++it;
    }
    mPacketQueue.clear();
    return OK;
}

////////////////////////////////////////////////////////////////////////////////

AmFFmpegExtractor::AmFFmpegExtractor(const sp<DataSource> &source)
    : mDataSource(source),
      mInputFormat(NULL),
      mPTSPopulator(NULL),
      mFFmpegContext(NULL) {
    init();
}

AmFFmpegExtractor::~AmFFmpegExtractor() {
    if (NULL != mFFmpegContext) {
        avformat_close_input(&mFFmpegContext);
    }
}

size_t AmFFmpegExtractor::countTracks() {
    Mutex::Autolock autoLock(mLock);
    return mSources.size();
}

sp<MediaSource> AmFFmpegExtractor::getTrack(size_t index) {
    Mutex::Autolock autoLock(mLock);
    if (index >= mSources.size()) {
        return NULL;
    }

    mSources.editItemAt(index).mIsActive = true;
    // TODO: We may need to check and deactivate other sources which have the
    // same media type if the player plays only single track per media type.

    return mSources[index].mSource;
}

sp<MetaData> AmFFmpegExtractor::getTrackMetaData(
        size_t index, uint32_t flags) {
    Mutex::Autolock autoLock(mLock);
    if (index >= mSources.size()) {
        return NULL;
    }

    return mSources[index].mSource->getFormat();
}

sp<MetaData> AmFFmpegExtractor::getMetaData() {
    return mMeta;
}

void AmFFmpegExtractor::init() {
    Mutex::Autolock autoLock(mLock);
    av_register_all();

    mSourceAdapter = new AmFFmpegByteIOAdapter();
    mSourceAdapter->init(mDataSource);

    mInputFormat = probeFormat(mDataSource);
    if (mInputFormat == NULL) {
        ALOGE("Failed to probe the input stream.");
        return;
    }

    if (!(mFFmpegContext = openAVFormatContext(
            mInputFormat, mSourceAdapter.get()))) {
        ALOGE("Failed to open FFmpeg context.");
        return;
    }

    mPTSPopulator = new AmPTSPopulator(mFFmpegContext->nb_streams);

    mMeta = new MetaData;
    const char *mimeType = convertInputFormatToMimeType(mInputFormat);
    if (mimeType) {
        mMeta->setCString(kKeyMIMEType, mimeType);
    }
    if (static_cast<int64_t>(AV_NOPTS_VALUE) != mFFmpegContext->duration) {
        mMeta->setInt64(kKeyDuration,
                convertTimeBaseToMicroSec(mFFmpegContext->duration));
    }

    // TODO: to support multiple track.
    bool audioAdded = false;
    bool videoAdded = false;
    int32_t primaryTrack = getPrimaryStreamIndex(mFFmpegContext);
    for (uint32_t i = 0; i < mFFmpegContext->nb_streams; ++i) {
        AVCodecContext *codec = mFFmpegContext->streams[i]->codec;
        bool shouldAdd =
                ((codec->codec_type == AVMEDIA_TYPE_AUDIO) && !audioAdded)
                || ((codec->codec_type == AVMEDIA_TYPE_VIDEO) && !videoAdded)
                || ((codec->codec_type == AVMEDIA_TYPE_SUBTITLE));
        const char *mimeType =
                convertCodecIdToMimeType(mFFmpegContext->streams[i]->codec);
        if (shouldAdd && NULL != mimeType) {
            bool seekable = (primaryTrack == static_cast<int32_t>(i));
            mSources.add();
            int64_t startTimeUs = 0;
            if (static_cast<int64_t>(AV_NOPTS_VALUE) !=
                    mFFmpegContext->start_time) {
                startTimeUs = mFFmpegContext->start_time;
            }
            mSources.editTop().mSource =
                    new AmFFmpegSource(this, mFFmpegContext->streams[i],
                            mInputFormat, mPTSPopulator, seekable, startTimeUs);
            mStreamIdxToSourceIdx.add(mSources.size() - 1);
            if (codec->codec_type == AVMEDIA_TYPE_AUDIO) {
                audioAdded = true;
            } else if (codec->codec_type == AVMEDIA_TYPE_VIDEO) {
                videoAdded = true;
            }
        } else {
            mStreamIdxToSourceIdx.add(kInvalidSourceIdx);
        }
    }
}

status_t AmFFmpegExtractor::feedMore() {
    Mutex::Autolock autoLock(mLock);
    status_t ret = OK;
    AVPacket *packet = new AVPacket();
    while (true) {
        int res = av_read_frame(mFFmpegContext, packet);
        if (res >= 0) {
            uint32_t sourceIdx = kInvalidSourceIdx;
            if (static_cast<size_t>(packet->stream_index) < mStreamIdxToSourceIdx.size()) {
                sourceIdx = mStreamIdxToSourceIdx[packet->stream_index];
            }
            if (sourceIdx == kInvalidSourceIdx
                    || !mSources[sourceIdx].mIsActive || packet->size <= 0 || packet->pts < 0) {
                av_free_packet(packet);
                continue;
            }
            av_dup_packet(packet);
            mSources[sourceIdx].mSource->queuePacket(packet);
        } else {
            delete packet;
            ALOGV("No more packets from ffmpeg.");
            ret = ERROR_END_OF_STREAM;
        }
        break;
    }
    return ret;
}

void AmFFmpegExtractor::seekTo(
        int64_t seekTimeUs, MediaSource::ReadOptions::SeekMode mode) {
    int seekFlag = 0;
    switch (mode) {
        case MediaSource::ReadOptions::SEEK_CLOSEST_SYNC:
            ALOGW("Seek to the closest sync frame is not supported. "
                    "Trying to seek to the previous sync frame...");
            // Fall-through
        case MediaSource::ReadOptions::SEEK_PREVIOUS_SYNC:
            seekFlag |= AVSEEK_FLAG_BACKWARD;
            break;
        case MediaSource::ReadOptions::SEEK_NEXT_SYNC:
            break;
        case MediaSource::ReadOptions::SEEK_CLOSEST:
            seekFlag |= AVSEEK_FLAG_ANY;
            break;
        default:
            CHECK(!"Unknown seek mode.");
            break;
    }
    mPTSPopulator->reset();
    int64_t seekPosition = convertMicroSecToTimeBase(seekTimeUs);
    Mutex::Autolock autoLock(mLock);
    if (av_seek_frame(mFFmpegContext, -1 /* default stream */,
            seekPosition, seekFlag) < 0) {
        ALOGE("Failed to seek to %lld", seekPosition);
        return;
    }

    for (uint32_t i = 0; i < mSources.size(); ++i) {
        if (mSources[i].mIsActive) {
            mSources[i].mSource->clearPendingPackets();
        }
    }
    ALOGV("Seeking to %lld was successful.", seekPosition);
}

int32_t AmFFmpegExtractor::getPrimaryStreamIndex(AVFormatContext *context) {
    int firstAudioIndex = -1;

    if (context->nb_streams <= 0) {
        return -1;
    }
    for(uint32_t i = 0; i < context->nb_streams; i++) {
        AVStream *st = context->streams[i];
        if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO
                && convertCodecIdToMimeType(st->codec) != NULL) {
            return i;
        }
        if (firstAudioIndex < 0
                && st->codec->codec_type == AVMEDIA_TYPE_AUDIO
                && convertCodecIdToMimeType(st->codec) != NULL) {
            firstAudioIndex = i;
        }
    }
    return firstAudioIndex;
}

uint32_t AmFFmpegExtractor::flags() const {
    uint32_t flags = CAN_PAUSE;

    off64_t dummy = 0;
    if (mDataSource->getSize(&dummy) != ERROR_UNSUPPORTED) {
        flags |= CAN_SEEK_FORWARD | CAN_SEEK_BACKWARD | CAN_SEEK;
    }

    return flags;
}

AmFFmpegExtractor::SourceInfo::SourceInfo()
    : mIsActive(false) { }

bool SniffAmFFmpeg(
        const sp<DataSource> &source, String8 *mimeType, float *confidence,
        sp<AMessage> *) {
    av_register_all();
    AVInputFormat *inputFormat = probeFormat(source);
    if (NULL != inputFormat) {
        const char *mimeDetected = convertInputFormatToMimeType(inputFormat);
        if (NULL != mimeDetected) {
            *mimeType = mimeDetected;
            // only available when stagefright not support
            *confidence = 0.05f;
            return true;
        }
    }
    return false;
}

}  // namespace android
