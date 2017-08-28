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
#include <cutils/properties.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avio.h>
#include <libavformat/avformat.h>
#include "libavutil/mastering_display_metadata.h"
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
#include <media/stagefright/foundation/ColorUtils.h>
#include <media/stagefright/foundation/hexdump.h>
#include <media/stagefright/AmMediaDefsExt.h>
#include <media/stagefright/AmMetaDataExt.h>
#include <media/stagefright/MediaDefs.h>

#include "AmPTSPopulator.h"
#include "AmFFmpegByteIOAdapter.h"
#include "AmFFmpegUtils.h"
#include "formatters/StreamFormatter.h"

#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/foundation/ABitReader.h>

#include "include/avc_utils.h"


namespace android {

static const uint32_t kInvalidSourceIdx = 0xFFFFFFFF;

static const size_t kDefaultFrameBufferSize = 512 * 1024;
static const size_t kMaxFrameBufferSize = 8 * 1024 * 1024;

struct AmFFmpegSource : public MediaSource {
    AmFFmpegSource(
            const sp<AmFFmpegExtractor> &extractor,
            AVStream *stream,
            AVProgram *program,
            AVInputFormat *inputFormat,
            sp<AmPTSPopulator> &ptsPopulator,
            bool seekable,
            int64_t startTimeUs);

    virtual status_t start(MetaData *params = NULL);
    virtual status_t stop();
    virtual sp<MetaData> getFormat();

    virtual status_t read(
            MediaBuffer **buffer, const ReadOptions *options = NULL);
    virtual status_t pause();

    // Callee retains the ownership of the packet.
    status_t queuePacket(AVPacket *packet);
    status_t clearPendingPackets();

    sp<StreamFormatter> mFormatter;
    AVStream *mStream;

    int32_t mTimeBase;
    int32_t mNumerator;
    int32_t mDenominator;
    int64_t convertStreamTimeToUs(int64_t timeInStreamTime);

private:
    Mutex mLock;

    sp<AmFFmpegExtractor> mExtractor;
    sp<MetaData> mMeta;
    sp<AmPTSPopulator> mPTSPopulator;

    AVProgram *mProgram;
    Mutex mPacketQueueLock;
    List<AVPacket *> mPacketQueue;

    bool mStarted;
    bool mFirstPacket;
    bool mStartRead;
    int mCheckHEVCHDR;
    MediaBufferGroup *mGroup;

    int64_t mStartTimeUs;

    const char * mMime;
    const AVInputFormat *mInputFormat;

    // If there are both audio and video streams, only the video stream
    // will be seekable, otherwise the single stream will be seekable.
    bool mSeekable;

    int64_t mLastValidPts = 0;
    int64_t mLastValidDts = 0;

    virtual ~AmFFmpegSource();
    AVPacket *dequeuePacket();
    status_t init(
            AVStream *stream, AVInputFormat *inputFormat,
            const sp<AmFFmpegExtractor> &extractor);

    void resetBufferGroup(size_t size);

    DISALLOW_EVIL_CONSTRUCTORS(AmFFmpegSource);
};

AmFFmpegSource::AmFFmpegSource(
        const sp<AmFFmpegExtractor> &extractor,
        AVStream *stream,
        AVProgram *program,
        AVInputFormat *inputFormat,
        sp<AmPTSPopulator> &ptsPopulator,
        bool seekable,
        int64_t startTimeUs)
    : mExtractor(extractor),
      mPTSPopulator(ptsPopulator),
      mStarted(false),
      mFirstPacket(true),
      mStartRead(false),
      mCheckHEVCHDR(0),
      mGroup(NULL),
      mStream(stream),
      mProgram(program),
      mMime(NULL),
      mStartTimeUs(startTimeUs),
      mTimeBase(0),
      mNumerator(0),
      mDenominator(0),
      mSeekable(seekable) {
      ALOGI(" [%s %d]", __FUNCTION__, __LINE__);
    init(stream, inputFormat, extractor);
}

AmFFmpegSource::~AmFFmpegSource() {
    if (mStarted) {
        stop();
    }

    ALOGI(" [%s %d]", __FUNCTION__, __LINE__);
    clearPendingPackets();
}

status_t AmFFmpegSource::init(
        AVStream *stream, AVInputFormat *inputFormat,
        const sp<AmFFmpegExtractor> &extractor) {
    CHECK(stream);
    CHECK(inputFormat);
    ALOGI(" [%s %d]", __FUNCTION__, __LINE__);

    mTimeBase = AV_TIME_BASE;
    mNumerator = stream->time_base.num;
    mDenominator = stream->time_base.den;

    mMeta = new MetaData;
    sp<MetaData> parent = extractor->getMetaData();
    int64_t durationUs = 0LL;
    if (parent->findInt64(kKeyDuration, &durationUs)) {
        mMeta->setInt64(kKeyDuration, durationUs);
    }
    if ((stream->start_time != AV_NOPTS_VALUE) && (llabs(mStartTimeUs - convertStreamTimeToUs(stream->start_time)) > 5000000)) {
        /* start time diff > 5s use stream start time */
        mStartTimeUs = convertStreamTimeToUs(stream->start_time);
    }

    if (stream->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
        ALOGI("audio:demux: extradata_size = %d", stream->codec->extradata_size);
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
        if (stream->codec->codec_tag == 354) {
            mMeta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_WMAPRO);
        } else {
            mMeta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_WMA);
        }
        //mMeta->setInt32(kKeyCodecID,stream->codec->codec_tag);
        //add for aac gapless playback
        if (stream->codec->codec_id == AV_CODEC_ID_AAC) {
            AVDictionaryEntry *lang =
                av_dict_get(stream->metadata, "iTunSMPB", NULL, 0);
            if (lang != NULL) {
                int encoderDelay, encoderPadding, samples;
                if (sscanf(lang->value, "%*X %X %X %X", &encoderDelay, &encoderPadding, &samples) == 3) {
                    mMeta->setInt32(kKeyEncoderDelay, encoderDelay);
                    mMeta->setInt32(kKeyEncoderPadding, encoderPadding);
                }
            }
        }
    } else if (stream->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
        ALOGI("video:demux: extradata_size = %d", stream->codec->extradata_size);
        mMeta->setInt32(kKeyWidth, stream->codec->width);
        mMeta->setInt32(kKeyHeight, stream->codec->height);
        mMeta->setInt32(kKeyCodecID, stream->codec->codec_id);
        mMeta->setInt32(kKeyBitRate, stream->codec->bit_rate);
        // NOTE: this framerate value is just a guess from FFmpeg. Decoder
        //       should get and use the real framerate from decoding.
        AVRational *rationalFramerate = NULL;
        if (stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0) {
            rationalFramerate = &(stream->avg_frame_rate);
        } else if (stream->r_frame_rate.num > 0 && stream->r_frame_rate.den > 0) {
            rationalFramerate = &(stream->r_frame_rate);
        }
        if (rationalFramerate != NULL) {
            float framerate = (rationalFramerate->num*1.0)/rationalFramerate->den;
            mMeta->setFloat('frRa', framerate);
            ALOGI("set frame-rate %.2f   rationalFramerate num %d den %d\n", framerate,rationalFramerate->num,rationalFramerate->den);
        }
        if (inputFormat == av_find_input_format("webm")) {//set decodec param
            if (stream->codec->codec_id == AV_CODEC_ID_MPEG4 || stream->codec->codec_id == AV_CODEC_ID_VP9
                || (stream->codec->codec_id == AV_CODEC_ID_H264 && rationalFramerate != NULL &&
                (!av_cmp_q((AVRational){ 2997, 125 },*rationalFramerate) ||
                !av_cmp_q((AVRational){ 24000, 1001 },*rationalFramerate) ) ) ) {
                //AVRational  = (AVRational){ 2997, 125 };  /* default: 23.976 fps */
                //{fps=24000/1001}  29.97fps
                mMeta->setInt32('depa', 0x40);//player_priv.h UNSTABLE_PTS
            }
        }
        if (stream->codec->extradata_size > 0) {
            mMeta->setData(kKeyExtraData, 0, (char*)stream->codec->extradata, stream->codec->extradata_size);
            mMeta->setInt32(kKeyExtraDataSize, stream->codec->extradata_size);
        } else {
            mMeta->setData(kKeyExtraData, 0, 0, 0);
            mMeta->setInt32(kKeyExtraDataSize, 0);
        }
        if (stream->codec->block_align > 0)
            mMeta->setInt32(kKeyBlockAlign, stream->codec->block_align);

        AVDictionaryEntry *lang =
                av_dict_get(stream->metadata, "rotate", NULL, 0);
        if (lang != NULL && lang->value != NULL) {
            int rotate = atoi(lang->value);
            ALOGD("rotate %d", rotate);
            mMeta->setInt32(kKeyRotation, rotate);
            if (rotate != 0) {
                ALOGD("set 4k osd"); // the 4k video with rotate use osd
                mMeta->setInt32(kKey4kOSD, 1);
            }
        }
        if (mProgram) {
            lang = av_dict_get(mProgram->metadata, "service_name", NULL, 0);
            if (lang != NULL && lang->value != NULL) {
                ALOGD("service_name %s", lang->value);
                const int name_len = 64;
                char pname[name_len];
                strncpy(pname, lang->value, name_len - 1);
                mMeta->setCString(kKeyProgramName, pname);
            } else {
                mMeta->setCString(kKeyProgramName, "unknown");
            }
        }
        if (stream->codec->codec_tag == MKTAG('M', 'V', 'C', ' ')) {
            ALOGD("is h264 mvc");
            mMeta->setInt32(kKeyIsMVC, true);
        }

        if (stream->codec->codec_tag == MKTAG('d', 'v', 'h', 'e') || stream->codec->codec_tag == MKTAG('D', 'O', 'V', 'I') ) {
            mMeta->setInt32(KKeyIsDV, 1);
            ALOGI("is dobyvison\n");
        }

        if (stream->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            //dts convert to us ratio, also dts convert accuracy
            mMeta->setInt32(MKTAG('s', 't', 't', 'u'), convertStreamTimeToUs(1));
        }

        if (stream->nb_side_data) {
            for (int i = 0; i < stream->nb_side_data; i++) {
                AVPacketSideData sd = stream->side_data[i];
                if (sd.type = AV_PKT_DATA_MASTERING_DISPLAY_METADATA) {
                    AVMasteringDisplayMetadata* metadata = (AVMasteringDisplayMetadata*)sd.data;
                    HDRStaticInfo info, nullInfo; // nullInfo is a fully unspecified static info
                    memset(&info, 0, sizeof(info));
                    memset(&nullInfo, 0, sizeof(nullInfo));

                    //info.sType1.mMaxContentLightLevel = metadata->max_cll;
                    //info.sType1.mMaxFrameAverageLightLevel = metadata->max_pall;
                    info.sType1.mMaxDisplayLuminance = metadata->max_luminance.num;
                    info.sType1.mMinDisplayLuminance = metadata->min_luminance.num;
                    info.sType1.mW.x = metadata->white_point[0].num;
                    info.sType1.mW.y = metadata->white_point[1].num;
                    info.sType1.mR.x = metadata->display_primaries[0][0].num;
                    info.sType1.mR.y = metadata->display_primaries[0][1].num;
                    info.sType1.mG.x = metadata->display_primaries[1][0].num;
                    info.sType1.mG.y = metadata->display_primaries[1][1].num;
                    info.sType1.mB.x = metadata->display_primaries[2][0].num;
                    info.sType1.mB.y = metadata->display_primaries[2][1].num;

                    // Only advertise static info if at least one of the groups have been specified.
                    if (memcmp(&info, &nullInfo, sizeof(info)) != 0) {
                        info.mID = HDRStaticInfo::kType1;
                        mMeta->setData(kKeyHdrStaticInfo, 'hdrS', &info, sizeof(info));
                    }
                }
            }
        }

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

    if (mProgram) {
        mMeta->setInt32(kKeyProgramNum, mProgram->program_num);
    }

    mInputFormat = inputFormat;
    mMime= convertCodecIdToMimeType(stream->codec);
    if (mMime) {
        mMeta->setCString(kKeyMIMEType, mMime);
    } else {
        return ERROR_UNSUPPORTED;
    }

    /** SoftAAC2 cannot decode adts extracted from ffmpeg because of no extradata, to use SoftADTS
       and aac profile main also not supported*/
    if (!strcmp(mMime, MEDIA_MIMETYPE_AUDIO_AAC)) {
        uint8_t profile = 0, header[2];
        if (stream->codec->extradata && stream->codec->extradata_size > 0) {
            memcpy(header,stream->codec->extradata,2);
            profile = (header[0] >> 3) & 0x1f;
            ALOGI("aac profile:%d",profile);
        }
        if ( profile == 1 || inputFormat == av_find_input_format("mpegts") ||
            inputFormat == av_find_input_format("rm") ) {
            mMeta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_ADTS_PROFILE);
        }
    }

    mFormatter = StreamFormatter::Create(stream->codec, inputFormat);
    if (stream->codec->codec_id != AV_CODEC_ID_MP3) {
        mFormatter->addCodecMeta(mMeta);
    }

    //wxl add for debug info apk 20170829
    switch (stream->codec->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            mExtractor->prepareAudioInfo(mMeta);
            break;
        case AVMEDIA_TYPE_VIDEO:
            mExtractor->prepareVideoInfo(mMeta);
            break;
        case AVMEDIA_TYPE_SUBTITLE:
            mExtractor->prepareSubtitleInfo(mMeta);
            break;
        default:
            break;
    }
    return OK;
}

status_t AmFFmpegSource::start(MetaData *params) {
    Mutex::Autolock autoLock(mLock);

    CHECK(!mStarted);

    resetBufferGroup(kDefaultFrameBufferSize);

    mStarted = true;
    if (mStream->codec->codec_type != AVMEDIA_TYPE_SUBTITLE)
        clearPendingPackets();

    return OK;
}

status_t AmFFmpegSource::stop() {
    Mutex::Autolock autoLock(mLock);
    ALOGI(" [%s %d]", __FUNCTION__, __LINE__);
    if (!mStarted)
        return OK;
    delete mGroup;
    mGroup = NULL;
    mStarted = false;
    clearPendingPackets();

    mExtractor->resetInfo();
    return OK;
}

status_t AmFFmpegSource::pause(){
    Mutex::Autolock autoLock(mLock);

    mExtractor->release(this);
    return OK;
}

sp<MetaData> AmFFmpegSource::getFormat() {
    Mutex::Autolock autoLock(mLock);

    return mMeta;
}

status_t AmFFmpegSource::read(
        MediaBuffer **out, const ReadOptions *options) {
    Mutex::Autolock autoLock(mLock);

    ALOGV("%s %d", __FUNCTION__, __LINE__);
    *out = NULL;

    sp<AmFFmpegExtractor> extractor = mExtractor;
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
        int64_t nowUs = ALooper::GetNowUs();
        packet = dequeuePacket();
        while (packet == NULL) {
            if (mStream->codec->codec_type == AVMEDIA_TYPE_SUBTITLE
                && (int)(ALooper::GetNowUs() - nowUs) >= 50) {//50us time out
                return ERROR_READ_TIME_OUT;
            }
            if (ERROR_END_OF_STREAM == extractor->feedMore()) {
                return ERROR_END_OF_STREAM;
            }
            packet = dequeuePacket();
        }

        #if 0
        // seek to current position for timed text change track(cts test)
        bool skipTimedTextSeek = false;
        if (!strcmp(mMime, MEDIA_MIMETYPE_VIDEO_AVC)
                    && (mInputFormat == av_find_input_format("mp4")
                    || mInputFormat == av_find_input_format("flv")
                    || mInputFormat == av_find_input_format("matroska")
                    || mInputFormat == av_find_input_format("mpegts"))) {
            skipTimedTextSeek = true;
        }

        if (mStream->codec->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            if (options && options->getSeekTo(&seekTimeUs, &seekMode)) {
                //ALOGI("[read]packet->pts:%" PRId64 ", seekTimeUs:%" PRId64 ", seekTimeMs:%" PRId64 "\n", packet->pts, seekTimeUs, (seekTimeUs / 1000));
                if (seekTimeUs > 0 && !skipTimedTextSeek) {
                    int64_t seekTimeMs = seekTimeUs / 1000;
                    int64_t curTimeMs = (packet->pts - extractor->mFirstVpts) / 90;
                    int64_t nowUs1 = ALooper::GetNowUs();
                    while (/*packet->pts*/curTimeMs < seekTimeMs) {
                        //ALOGI("[read]curTimeMs:%" PRId64 ",packet->pts : %" PRId64 ", seekTimeMs:%" PRId64 "\n", curTimeMs, packet->size, seekTimeMs);
                        if (packet != NULL) {
                            av_free_packet(packet);
                            delete packet;
                        }
                        packet = dequeuePacket();
                        while (packet == NULL) {
                            if ((int)(ALooper::GetNowUs() - nowUs1) >= 50) {//50us time out
                                return ERROR_READ_TIME_OUT;
                            }
                            if (ERROR_END_OF_STREAM == extractor->feedMore()) {
                                return ERROR_END_OF_STREAM;
                            }
                            packet = dequeuePacket();
                        }
                        curTimeMs = (packet->pts - extractor->mFirstVpts) / 90;
                    }
                }
            }
        }
        #endif
    }

    if ((mStream->codec->codec_type == AVMEDIA_TYPE_AUDIO || mStream->codec->codec_type == AVMEDIA_TYPE_VIDEO)
        && (mStream->codec->codec  && (mStream->codec->codec->capabilities & AV_CODEC_CAP_DELAY)) || (packet && packet->size) ) {
        av_packet_split_side_data(packet);

    }

    MediaBuffer *buffer = NULL;
    status_t ret = mGroup->acquire_buffer(&buffer);
    if (ret != OK) {
        ALOGE("Failed to acquire buffer.");
        av_free_packet(packet);
        delete packet;
        return ret;
    }

    uint32_t requiredLen =
            mFormatter->computeNewESLen(packet->data, packet->size);
    if (requiredLen == 0) {//create PassthruFormatter for invalid requiredLen, ext: HVCCFormatter
        mFormatter = StreamFormatter::Create(mStream->codec, NULL);
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
            av_free_packet(packet);
            delete packet;
            return ERROR_BUFFER_TOO_SMALL;
        }
        resetBufferGroup(newSize);
        status_t ret = mGroup->acquire_buffer(&buffer);
        if (ret != OK) {
            av_free_packet(packet);
            delete packet;
            return ret;
        }
    }

    int32_t filledLength = 0;

    filledLength = mFormatter->formatES(
            packet->data, packet->size,
            static_cast<uint8_t *>(buffer->data()), buffer->size());

    if (filledLength <= 0) {
        ALOGE("Failed to format packet data.");
        buffer->release();
        buffer = NULL;
        av_free_packet(packet);
        delete packet;
        return ERROR_MALFORMED;
    }

    mFormatter->getMetaFromES((const uint8_t *)buffer->data(), filledLength, mMeta);

    if (AV_NOPTS_VALUE == packet->pts) {
        packet->pts = mLastValidPts;
        ALOGV("meet invalid pts, set last pts to current frame pts:%" PRId64 " dts:%" PRId64 "", mLastValidPts, mLastValidDts);
    } else {
        mLastValidPts = packet->pts;
    }
    if (AV_NOPTS_VALUE == packet->dts) {
        packet->dts = mLastValidDts;
        ALOGV("meet invalid dts, set last ts to current frame pts:%" PRId64 " dts:%" PRId64 "", mLastValidPts, mLastValidDts);
    } else {
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

    //dequeue accessUnit for close caption from SEI of H264
    sp<ABuffer> accessUnit;
    status_t err = mFormatter->dequeueAccessUnit(&accessUnit);
    if (err == OK) {
        sp<ABuffer> sei;
        if (accessUnit->meta()->findBuffer("sei", &sei) && sei != NULL) {
            buffer->meta_data()->setData(kKeySEI, 0, sei->data(), sei->size());
        }
    }

    if (mStream->codec->codec_type == AVMEDIA_TYPE_SUBTITLE) {
        //add for inner subtitle
        buffer->meta_data()->setInt32(kKeyStreamTimeBaseNum, mStream->time_base.num);
        buffer->meta_data()->setInt32(kKeyStreamTimeBaseDen, mStream->time_base.den);
        buffer->meta_data()->setInt64(kKeyStreamStartTime, mStream->start_time);
        buffer->meta_data()->setInt32(kKeyStreamCodecID, mStream->codec->codec_id);
        buffer->meta_data()->setInt32(kKeyStreamCodecTag, mStream->codec->codec_tag);
        buffer->meta_data()->setInt32(kKeyPktSize, packet->size);
        buffer->meta_data()->setInt64(kKeyPktPts, packet->pts);
        buffer->meta_data()->setInt64(kKeyPktDts, packet->dts);
        buffer->meta_data()->setInt32(kKeyPktDuration, packet->duration);
        buffer->meta_data()->setInt64(kKeyPktConvergenceDuration, packet->convergence_duration);
    }

    if (mStream->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
        if (packet->size > 0
            && packet->pts >= 0
            && packet->pts != AV_NOPTS_VALUE
            && extractor->mFirstVpts < 0) {
            //ALOGI("[read]extractor->mFirstVpts:%" PRId64 "\n", extractor->mFirstVpts);
            extractor->mFirstVpts = packet->pts;
            buffer->meta_data()->setInt64(kKeyPktFirstVPts, extractor->mFirstVpts);
        }
    }

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

    if (!mStarted && mStream->codec->codec_type != AVMEDIA_TYPE_SUBTITLE) {
        av_free_packet(packet);
        delete packet;
        return OK;
    }

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
      mFFmpegContext(NULL),
      mFirstVpts(-1) {
      mSeek = false;
      ALOGI(" [%s %d]", __FUNCTION__, __LINE__);
    init();
}

AmFFmpegExtractor::~AmFFmpegExtractor() {
    mSeek = false;
    if (NULL != mFFmpegContext) {
        avformat_close_input(&mFFmpegContext);
        ALOGI(" [%s %d]", __FUNCTION__, __LINE__);
    }
    mDataSource.clear();
    ALOGI(" [%s %d]", __FUNCTION__, __LINE__);
}

size_t AmFFmpegExtractor::countTracks() {
    Mutex::Autolock autoLock(mLock);
    return mSources.size();
}

sp<IMediaSource> AmFFmpegExtractor::getTrack(size_t index) {
    Mutex::Autolock autoLock(mLock);
    if (index >= mSources.size()) {
        return NULL;
    }

    mSources.editItemAt(index).mIsActive = true;
    // TODO: We may need to check and deactivate other sources which have the
    // same media type if the player plays only single track per media type.

    return mSources[index].mSource;////////////// this shoule return a soure ,not to  be ref
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
    int64_t duration_us = 0;
    char value[PROPERTY_VALUE_MAX];
    int  logLevel;
    if (property_get("media.ffmpegextractor.loglevel", value, "16") > 0) {
        if ((sscanf(value, "%d", &logLevel)) > 0) {
            ALOGI("media.ffmpegextractor.loglevel %d\n", logLevel);
        }
    }
    av_log_set_level(logLevel);

    resetInfo();
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
    ALOGI("///////////////start dump format");
    av_dump_format(mFFmpegContext,0,NULL,0);
    mPTSPopulator = new AmPTSPopulator(mFFmpegContext->nb_streams);

    mMeta = new MetaData;
    const char *mimeType = convertInputFormatToMimeType(mInputFormat);
    if (mimeType) {
        mMeta->setCString(kKeyMIMEType, mimeType);
        ALOGI("[%s %d] %s", __FUNCTION__, __LINE__,mimeType);
    }
    if (static_cast<int64_t>(AV_NOPTS_VALUE) != mFFmpegContext->duration) {
        mMeta->setInt64(kKeyDuration,
                convertTimeBaseToMicroSec(mFFmpegContext->duration));
    } else {
        //calculate duation
        int64_t filesize = mFFmpegContext->pb ? avio_size(mFFmpegContext->pb) : 0;
        if (0 == mFFmpegContext->bit_rate) {
             int bitrate_sum = 0;
            for (uint32_t i = 0; i < mFFmpegContext->nb_streams; ++i) {
                AVCodecContext *codec = mFFmpegContext->streams[i]->codec;
                bitrate_sum += codec->bit_rate;
            }
            if (0 != bitrate_sum) {
                duration_us = (int)(((filesize << 3) * 1000000) / bitrate_sum);
            }
        } else {
            duration_us = (int)(((filesize << 3) * 1000000) / mFFmpegContext->bit_rate);
        }
        if (duration_us != 0)
            mMeta->setInt64(kKeyDuration,duration_us);
    }

    // multiple track support has been added
    int32_t primaryTrack = getPrimaryStreamIndex(mFFmpegContext);
    for (uint32_t i = 0; i < mFFmpegContext->nb_streams; ++i) {
        bool streamValid = true;
        AVCodecContext *codec = mFFmpegContext->streams[i]->codec;

        if (!mFFmpegContext->streams[i]->codec_info_nb_frames
                && !checkStreamValid(codec)) {
            streamValid = false;
        }
        //programe that only have audio will discard for mpegts
        AVProgram *program = av_find_program_from_stream(mFFmpegContext, NULL, mFFmpegContext->streams[i]->index);
        if ((mInputFormat == av_find_input_format("mpegts")) && (program == NULL)) {
            streamValid = false;;
        }

        bool shouldAdd = streamValid && ((codec->codec_type == AVMEDIA_TYPE_AUDIO)
                                            || (codec->codec_type == AVMEDIA_TYPE_VIDEO)
                                            || (codec->codec_type == AVMEDIA_TYPE_SUBTITLE));

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
                    new AmFFmpegSource(this, mFFmpegContext->streams[i], program,
                            mInputFormat, mPTSPopulator, seekable, startTimeUs);
            mStreamIdxToSourceIdx.add(mSources.size() - 1);
        } else {
            mStreamIdxToSourceIdx.add(kInvalidSourceIdx);
        }
    }
}

status_t AmFFmpegExtractor::feedMore() {
#define MAX_TRY_READ_COUNT  (50)
    Mutex::Autolock autoLock(mLock);
    status_t ret = OK;
    AVPacket *packet = new AVPacket();
    while (true) {
        int res = av_read_frame(mFFmpegContext, packet);

        uint32_t sourceIdx = kInvalidSourceIdx;
        if (static_cast<size_t>(packet->stream_index) < mStreamIdxToSourceIdx.size()) {
            sourceIdx = mStreamIdxToSourceIdx[packet->stream_index];
        }

        if (res >= 0) {
            if (sourceIdx == kInvalidSourceIdx
                    || !mSources[sourceIdx].mIsActive
                    || packet->size <= 0
                    || (packet->pts < 0 &&  packet->pts != AV_NOPTS_VALUE)) {
                av_free_packet(packet);
                ALOGI("continue to read a frame2\n");
                continue;
            }

            const int64_t ptsFromFFmpeg = (packet->pts == static_cast<int64_t>(AV_NOPTS_VALUE))
                    ? kUnknownPTS : mSources[sourceIdx].mSource->convertStreamTimeToUs(packet->pts);
            if (ptsFromFFmpeg >= mFFmpegContext->duration && mFFmpegContext->duration > 0 && (mInputFormat != av_find_input_format("mpegts"))) {
                delete packet;
                ret = ERROR_END_OF_STREAM;
                ALOGI("end of stream\n");
                break;
            }
            else {
                if (mSeek && mSources[sourceIdx].mSource->mStream  && mSources[sourceIdx].mSource->mStream->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
                    if (packet->flags & AV_PKT_FLAG_KEY) {
                        mSeek = false;
                    } else {
                        ALOGI("drop a frame");
                        av_free_packet(packet);
                        continue;
                    }
                }
                av_dup_packet(packet);
                //parse and store SEI for cc subtitle
                mSources[sourceIdx].mSource->mFormatter->checkNAL(packet->data, packet->size);
                mSources[sourceIdx].mSource->queuePacket(packet);
                mReadPktCnt = 0;
            }
        } else {
            delete packet;
            mReadPktCnt ++;
            if (mReadPktCnt > MAX_TRY_READ_COUNT) {
                ret = ERROR_END_OF_STREAM;
                ALOGI("No more packets from ffmpeg.");
            }
        }
        break;
    }
    return ret;
}

void AmFFmpegExtractor::seekTo(
        int64_t seekTimeUs, MediaSource::ReadOptions::SeekMode mode) {
    int seekFlag = 0;
    mSeek = false;
    switch (mode) {
        case MediaSource::ReadOptions::SEEK_CLOSEST_SYNC:
            ALOGW("Seek to the closest sync frame is not supported. "
                    "Trying to seek to the previous sync frame...");
            // Fall-through
        case MediaSource::ReadOptions::SEEK_PREVIOUS_SYNC:
            seekFlag |= AVSEEK_FLAG_FRAME;//AVSEEK_FLAG_BACKWARD;
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
    mSeek = true;
    if (av_seek_frame(mFFmpegContext, -1 /* default stream */,
            seekPosition, seekFlag) < 0) {
        ALOGE("Failed to seek to %" PRId64 "", seekPosition);
        return;
    }

    for (uint32_t i = 0; i < mSources.size(); ++i) {
        if (mSources[i].mIsActive) {
            mSources[i].mSource->clearPendingPackets();
        }
    }
    ALOGI("Seeking to %" PRId64 " was successful. seekflag %d ", seekPosition ,seekFlag);
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
void AmFFmpegExtractor::release(AmFFmpegSource* source){
    ALOGI(" [%s %d]", __FUNCTION__, __LINE__);
    for (uint32_t i = 0; i < mSources.size(); ++i) {
        if (source != mSources[i].mSource.get())
            continue;
        ALOGI("source addr %p",source);
        if (mSources[i].mIsActive) {
            mSources[i].mSource->clearPendingPackets();
        }
        mSources.erase(mSources.begin());
    }

}

void AmFFmpegExtractor::prepareAudioInfo(sp<MetaData> meta) {
    //codec
    const char *mimestr;
    CHECK(meta->findCString(kKeyMIMEType, &mimestr));
    property_set("media.debuginfo.acodec", mimestr);

    //hardware accelerated
    property_set("media.debuginfo.ahwaccelerated", "NO");

    //bitrate
    int32_t bitrate;
    char bitratestr[16] = {0};
    CHECK(meta->findInt32(kKeyBitRate, &bitrate));
    sprintf(bitratestr, "%d", bitrate);
    property_set("media.debuginfo.abitrate", bitratestr);

    //secure
    property_set("media.debuginfo.asecure", "NO");
}

void AmFFmpegExtractor::prepareVideoInfo(sp<MetaData> meta) {
    //codec
    const char *mimestr;
    CHECK(meta->findCString(kKeyMIMEType, &mimestr));
    property_set("media.debuginfo.vcodec", mimestr);

    //bitrate
    int32_t bitrate;
    char bitratestr[16] = {0};
    CHECK(meta->findInt32(kKeyBitRate, &bitrate));
    sprintf(bitratestr, "%d", bitrate);
    property_set("media.debuginfo.vbitrate", bitratestr);

    //width &height
    int32_t width;
    int32_t height;
    char widthstr[16] = {0};
    char heightstr[16] = {0};
    char resolutionstr[32] = {0};
    CHECK(meta->findInt32(kKeyWidth, &width));
    CHECK(meta->findInt32(kKeyHeight, &height));
    sprintf(widthstr, "%d", width);
    sprintf(heightstr, "%d", height);
    strcpy(resolutionstr, widthstr);
    strcat(resolutionstr, "x");
    strcat(resolutionstr, heightstr);
    property_set("media.debuginfo.vresolution", resolutionstr);

    //frame rate
    float framerate;
    char frameratestr[16] = {0};
    CHECK(meta->findFloat('frRa',&framerate));
    sprintf(frameratestr, "%f", framerate);
    property_set("media.debuginfo.vframerate", frameratestr);

    property_set("media.debuginfo.vcolorstandard", "BT709");
}

void AmFFmpegExtractor::prepareSubtitleInfo(sp<MetaData> meta) {

}

void AmFFmpegExtractor::resetInfo() {
    ALOGE("[resetInfo]");
    //for audio
    property_set("media.debuginfo.acodec", "UNKNOW");
    property_set("media.debuginfo.ahwaccelerated", "NO");
    property_set("media.debuginfo.abitrate", "0");
    property_set("media.debuginfo.asecure", "NO");

    //for video
    property_set("media.debuginfo.vcodec", "UNKNOW");
    property_set("media.debuginfo.vhwaccelerated", "NO");
    property_set("media.debuginfo.vbitrate", "0");
    property_set("media.debuginfo.vsecure", "NO");
    property_set("media.debuginfo.vframsdropped", "0");
    property_set("media.debuginfo.vresolution", "0x0");
    property_set("media.debuginfo.vframerate", "0");
    property_set("media.debuginfo.vcolorstandard", "UNKNOW");

    //for subtitle
}

uint32_t AmFFmpegExtractor::flags() const {
    uint32_t flags = CAN_PAUSE;

    off64_t dummy = 0;
    if (mDataSource->getSize(&dummy) != ERROR_UNSUPPORTED) {
        flags |= CAN_SEEK_FORWARD | CAN_SEEK_BACKWARD | CAN_SEEK;
    }

    return flags;
}

bool AmFFmpegExtractor::checkStreamValid(AVCodecContext *codec) {
    bool ret = false;
    switch (codec->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            if ((codec->codec_id == AV_CODEC_ID_AAC) ||
                (codec->codec_id == AV_CODEC_ID_AC3) ||
                (codec->codec_id == AV_CODEC_ID_DTS)) {
                ret = true;
            } else {
                ret = (codec->sample_rate > 0) && (codec->channels > 0);
            }
            break;
        case AVMEDIA_TYPE_VIDEO:
            ret = codec->width > 0;
            break;
        default:
            ret = true;
        break;
    }
    return ret;
}

AmFFmpegExtractor::SourceInfo::SourceInfo()
    : mIsActive(false) { }

//add ffmpeg audio ret 2
int get_codec_id(const sp<DataSource> &source, AVInputFormat *inputFormat){

    int ret = -1;
    sp<AmFFmpegByteIOAdapter> adapter = new AmFFmpegByteIOAdapter();
    AVFormatContext* context = avformat_alloc_context();

    if (adapter == NULL || context == NULL) {
        ALOGE("Failed to allocate AVFormatContext or new AmFFmpegByteIOAdapter.");
        return ret;
    }

    adapter->init(source);
    context->pb = adapter->getContext();
    int res = avformat_open_input(
            &context,
            "dummy_file_name",  // need to pass a filename
            inputFormat,  // probe the container format.
            NULL);  // no special parameters
    if (res < 0) {
        ALOGE("Failed to open the input stream.");
        avformat_free_context(context);
       return ret;
    }

    int hasvideo = 0;
    int hasaudio = 0;
    int is_ffmpeg_audio = 0;
    for (uint32_t i = 0; i < context->nb_streams; i++) {
        if (context->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
           hasvideo ++;
        }else if(context->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
           if (context->streams[i]->codec->codec_id == AV_CODEC_ID_PCM_S16LE) {
               hasaudio ++;
            } else if (context->streams[i]->codec->codec_id == AV_CODEC_ID_COOK ||
                    context->streams[i]->codec->codec_id == AV_CODEC_ID_FLAC) {
                //android mkv extrator do not support cook and flac, use ffmpeg parse
                is_ffmpeg_audio = 1;
            }
        }
    }

    if (hasvideo == 0 && hasaudio == context->nb_streams) {
        ret = 1;
    }
    if (is_ffmpeg_audio) {
        ret = 2;
    }

    avformat_free_context(context);
    adapter.clear();
    return ret;
}
bool SniffAmFFmpeg(
        const sp<DataSource> &source, String8 *mimeType, float *confidence,
        sp<AMessage> *) {
    av_register_all();
    AVInputFormat *inputFormat = probeFormat(source);
    if (NULL != inputFormat) {
        const char *mimeDetected = convertInputFormatToMimeType(inputFormat);
        if (NULL != mimeDetected) {
            if (!strcmp(mimeDetected,MEDIA_MIMETYPE_CONTAINER_MATROSKA)) {
                int ret = get_codec_id(source, inputFormat);
                if (ret == 1) {
                    ALOGI("using MatroskaExtractor\n");
                    return false;
                } else if (ret == 2) {
                    *mimeType = mimeDetected;
                    *confidence = 0.8;
                    return true;
                }
            }
            *mimeType = mimeDetected;
            // only available when stagefright not support
            *confidence = 0.05f;
            if (!strcmp(mimeDetected,MEDIA_MIMETYPE_VIDEO_RM10) || !strcmp(mimeDetected,MEDIA_MIMETYPE_VIDEO_RM20) || !strcmp(mimeDetected,MEDIA_MIMETYPE_VIDEO_RM40)) {
                *confidence = 0.21f;
            }
            return true;
        }
    }
    return false;
}

}  // namespace android
