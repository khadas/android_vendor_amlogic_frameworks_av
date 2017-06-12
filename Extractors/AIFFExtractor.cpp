/*
 * Copyright (C) 2009 The Android Open Source Project
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
#define LOG_TAG "AIFFExtractor"
#include <utils/Log.h>
#include <math.h>  

#include "include/AIFFExtractor.h"
#ifdef WITH_AMLOGIC_MEDIA_EX_SUPPORT
#include <media/stagefright/AmMediaDefsExt.h>
#endif

#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/MediaBufferGroup.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MetaData.h>
#include <utils/String8.h>
#include <cutils/bitops.h>

namespace android {

#define AIFF                    0
#define AIFF_C_VERSION1         0xA2805140

#define	Clip(acc,min,max)	((acc) > max ? max : ((acc) < min ? min : (acc)))

static uint16_t U16_LE_AT(const uint8_t *ptr) {
    return ptr[1] << 8 | ptr[0];
}

static uint32_t U32_LE_AT(const uint8_t *ptr) {
    return ptr[3] << 24 | ptr[2] << 16 | ptr[1] << 8 | ptr[0];
}

static uint64_t U64_LE_AT(const uint8_t *ptr) {
 	uint64_t val;
    val = (uint64_t)U32_LE_AT(&ptr[4]) << 32;
    val |= (uint64_t)U32_LE_AT(ptr);
    return val;
}

static uint32_t U16_RE_AT(const uint8_t *ptr) {
    return ptr[0] << 8 | ptr[1];
}

static uint32_t U32_RE_AT(const uint8_t *ptr) {
    return ptr[0] << 24 | ptr[1] << 16 | ptr[2] << 8 | ptr[3];
}

static uint64_t U64_RE_AT(const uint8_t *ptr) {
    uint64_t val;
    val = (uint64_t)U32_RE_AT(&ptr[4]);
    val |= (uint64_t)U32_RE_AT(ptr) << 32;
    return val;
}

static float FloatSwap(float f)
{
	union{
		float f;
		unsigned char b[4];
	} dat1, dat2;

	dat1.f = f;
	dat2.b[0] = dat1.b[3];
	dat2.b[1] = dat1.b[2];
	dat2.b[2] = dat1.b[1];
	dat2.b[3] = dat1.b[0];
	return dat2.f;
} 

//--------------------------------------------------------------------
struct AIFFSource : public MediaSource {
		AIFFSource(
				const sp<DataSource> &dataSource,
				const sp<MetaData> &meta,
				uint16_t waveFormat,
				int32_t bitsPerSample,
				off64_t offset, size_t size,
				uint32_t BlockAlign,
				uint32_t SampleRate,
				uint16_t NumChannels);
	
		virtual status_t start(MetaData *params = NULL);
		virtual status_t stop();
		virtual sp<MetaData> getFormat();
		virtual status_t read(
				MediaBuffer **buffer, const ReadOptions *options = NULL);
	
	protected:
		virtual ~AIFFSource();
	
	private:
		static const size_t kMaxFrameSize;
		
		sp<DataSource> mDataSource;
		sp<MetaData> mMeta;

		uint16_t mNumChannels;
		uint32_t mNumSampleFrame;
		uint16_t mBitsPerSample;	
		uint32_t mSampleRate;
	
		uint16_t mWaveFormat;
		uint32_t mBlockAlign;
		
		off64_t  mOffset;
		size_t   mSize;
		bool     mStarted;
		
		MediaBufferGroup *mGroup;
		off64_t mCurrentPos;
		
		AIFFSource(const AIFFSource &);
		AIFFSource &operator=(const AIFFSource &);
};

//-----------------------------------------------------------------------------

static enum AVCodecID aiff_codec_get_id(int bps)
{
    if (bps <= 8)
        return AV_CODEC_ID_PCM_S8;
    if (bps <= 16)
        return AV_CODEC_ID_PCM_S16BE;
    if (bps <= 24)
        return AV_CODEC_ID_PCM_S24BE;
    if (bps <= 32)
        return AV_CODEC_ID_PCM_S32BE;

    return AV_CODEC_ID_NONE;
}
enum AVCodecID ff_codec_get_id(const AVCodecTag *tags, unsigned int tag)
{
    int i;
    for(i=0; tags[i].id != AV_CODEC_ID_NONE;i++) {
        if(tag == tags[i].tag)
            return tags[i].id;
    }
    return AV_CODEC_ID_NONE;
}

int av_get_exact_bits_per_sample(enum AVCodecID codec_id)
{
    switch (codec_id) {
	case AV_CODEC_ID_ADPCM_IMA_QT:
		return 4;
    case AV_CODEC_ID_PCM_ALAW:
    case AV_CODEC_ID_PCM_MULAW:
	case AV_CODEC_ID_PCM_G711_ALAW:
	case AV_CODEC_ID_PCM_G711_MULAW:
    case AV_CODEC_ID_PCM_S8:
    case AV_CODEC_ID_PCM_U8:
        return 8;
    case AV_CODEC_ID_PCM_S16BE:
    case AV_CODEC_ID_PCM_S16LE:
        return 16;
    case AV_CODEC_ID_PCM_S24BE:
        return 24;
    case AV_CODEC_ID_PCM_S32BE:
    case AV_CODEC_ID_PCM_F32BE:
        return 32;
    case AV_CODEC_ID_PCM_F64BE:
        return 64;
    default:
        return 0;
    }
}

status_t AIFFExtractor::init() {

    uint8_t header[12];
    if (mDataSource->readAt(0, header, sizeof(header)) < (ssize_t)sizeof(header)) {
        ALOGV("AIFF header size is not correct! \n");
        return NO_INIT;
    }

    if (memcmp(header, "FORM", 4) || (memcmp(&header[8], "AIFF", 4) && memcmp(&header[8], "AIFC", 4))) {
        ALOGV("AIFF header is invalid data! \n");
        return NO_INIT;
    }

    uint32_t totalSize = U32_RE_AT(&header[4]);
    ALOGI("TotalSize of the file is %u byte !\n", totalSize);

    unsigned version;
    if (memcmp(&header[8], "AIFF", 4) == 0) {
        version = AIFF;
        ALOGI(" [AIFF] header \n");
    } else {
        version = AIFF_C_VERSION1;
        ALOGI(" [AIFC] header \n");
    }

    off64_t offset = 12;
    uint32_t remainingSize = totalSize;
    uint32_t chunkSize;
    uint8_t chunkHeader[8];
    unsigned int codec_tag;
    enum AVCodecID codec_id;

    mTrackMeta = new MetaData;
    mBlockAlign = 0;

    while (remainingSize >= 8) {

        if (mDataSource->readAt(offset, chunkHeader, 8) < 8) {
            ALOGV("AIFF chunk header size is not correct! \n");
            return NO_INIT;
        }

        remainingSize -= 8;
        offset += 8;
        codec_tag = U32_LE_AT(chunkHeader);
        chunkSize = U32_RE_AT(&chunkHeader[4]);

        if (chunkSize > remainingSize) {
            ALOGV("AIFF chunk header size is not correct! left_filesize = %u byte, chunksize = %u byte !\n",
            remainingSize,chunkSize);
            return NO_INIT;
        }

        switch (codec_tag) {
            case MKTAG('C', 'O', 'M', 'M'):{
                // Common chunk
                if (chunkSize < 18) {
                    return NO_INIT;
                }

                uint8_t commonChunk[chunkSize];
                mDataSource->readAt(offset, commonChunk, chunkSize);

                mNumChannels = U16_RE_AT(commonChunk);
                mNumSampleFrame = U32_RE_AT(&commonChunk[2]);
                mBitsPerSample = U16_RE_AT(&commonChunk[6]);
                int exp;
                uint64_t val;
                double sample_rate;
                exp = U16_RE_AT(&commonChunk[8]);
                val = U64_RE_AT(&commonChunk[10]);
                sample_rate = ldexp(val, exp - 16383 - 63);
                mSampleRate = (uint32_t)sample_rate;

                ALOGI("AIFF Channel = %d, SampleRate = %d, BitsPerSample = %d \n",
                mNumChannels, mSampleRate, mBitsPerSample);

                if (version == AIFF_C_VERSION1) {
                    codec_tag = U32_LE_AT(&commonChunk[18]);
                    codec_id  = ff_codec_get_id(ff_codec_aiff_tags, codec_tag);

                    ALOGI("Compression type: [ %c%c%c%c ] \n Compression discription: [ %s ] \n",
                    commonChunk[18],commonChunk[19],commonChunk[20],commonChunk[21],&commonChunk[22]);
                }

                if (version != AIFF_C_VERSION1 || codec_id == AV_CODEC_ID_PCM_S16BE) {

                    codec_id = aiff_codec_get_id(mBitsPerSample);

                    ALOGI("AIFFExtractor::Waveformt is %d bit PCM_RAW, codec_id = [ %d ] \n", mBitsPerSample, codec_id);
                } else {
                    switch (codec_id) {
                        case AV_CODEC_ID_PCM_S16LE:
                            ALOGI("AIFFExtractor::Waveformt is 16 bit LE PCM_RAW, codec_id = [ %d ] \n", codec_id);
                            break;
                        case AV_CODEC_ID_PCM_F32BE:
                            break;
                        case AV_CODEC_ID_PCM_F64BE:
                            ALOGI("Float 64 bit PCM are not supported!\n");
                            break;
                        case AV_CODEC_ID_PCM_G711_ALAW:
                            ALOGI("AIFFExtractor::Waveformt is PCM_G711_ALAW \n");
                            mBlockAlign = 1;
                            break;
                        case AV_CODEC_ID_PCM_G711_MULAW:
                            ALOGI("AIFFExtractor::Waveformt is PCM_G711_MULAW \n");
                            mBlockAlign = 1;
                            break;
                        case AV_CODEC_ID_PCM_ALAW:
                        case AV_CODEC_ID_PCM_MULAW:
                            ALOGI("AIFFExtractor::Waveformt is PCM_ALAW/MULAW are not not supported \n");
                            break;
                        case AV_CODEC_ID_ADPCM_IMA_QT:
                            mBlockAlign = 34*mNumChannels;
                            ALOGI("AIFFExtractor::Waveformt is ADPCM_IMA_QT \n");
                            break;
                        case AV_CODEC_ID_MACE3:
                            mBlockAlign = 2*mNumChannels;
                            ALOGI("MACE3 are not supported!\n");
                            break;
                        case AV_CODEC_ID_MACE6:
                            mBlockAlign = 1*mNumChannels;
                            ALOGI("MACE6 are not supported!\n");
                            break;
                        case AV_CODEC_ID_GSM:
                            mBlockAlign = 33;
                            ALOGI("GSM are not supported!\n");
                            break;
                        case AV_CODEC_ID_QCELP:
                            mBlockAlign = 35;
                            ALOGI("AIFFExtractor::Waveformt is QCELP \n");
                            break;
                        default:
                            ALOGI("Unknown Waveformt!\n");
                            return NO_INIT;
                    }
                }

                mBitsPerSample = av_get_exact_bits_per_sample(codec_id);

                if (!mBlockAlign)
                    mBlockAlign = (mBitsPerSample * mNumChannels) >> 3;

                ALOGI("AIFFExtractor::mBlockAlign=%d, mBitsPerSample = %d \n", mBlockAlign,mBitsPerSample);

                remainingSize -= chunkSize;
                offset += chunkSize;

                mValidFormat = true;

                break;
            }
            case MKTAG('F', 'V', 'E', 'R'):{
                // Version chunk
                uint8_t VersionChunk[chunkSize];
                mDataSource->readAt(offset, VersionChunk, chunkSize);

                unsigned versiontype = U32_RE_AT(VersionChunk);

                ALOGV("The Version [ %x ]\n", versiontype);

                remainingSize -= chunkSize;
                offset += chunkSize;

                break;
            }
            case MKTAG('(', 'c', ')', ' '):{
                // Copyright chunk
                char CopyrightChunk[chunkSize];
                mDataSource->readAt(offset, CopyrightChunk, chunkSize);

                ALOGV("The Copyright [ %s ]\n", CopyrightChunk);

                remainingSize -= chunkSize;
                offset += chunkSize;

                break;
            }
            case MKTAG('A', 'N', 'N', 'O'):{
                // Annotation chunk
                char AnnotationChunk[chunkSize];
                mDataSource->readAt(offset, AnnotationChunk, chunkSize);

                ALOGV("The Annotation [ %s ]\n", AnnotationChunk);

                remainingSize -= chunkSize;
                offset += chunkSize;

                break;
            }
            case MKTAG('N', 'A', 'M', 'E'):{
                // Sample name chunk
                char TitleChunk[chunkSize];
                mDataSource->readAt(offset, TitleChunk, chunkSize);

                mTrackMeta->setCString(kKeyTitle, TitleChunk);

                ALOGV("The Title [ %s ]\n", TitleChunk);

                remainingSize -= chunkSize;
                offset += chunkSize;

                break;
            }
            case MKTAG('A', 'U', 'T', 'H'):{
                // Author chunk
                char AuthorChunk[chunkSize];
                mDataSource->readAt(offset, AuthorChunk, chunkSize);

                mTrackMeta->setCString(kKeyAuthor, AuthorChunk);

                ALOGV("The Author [ %s ]\n", AuthorChunk);

                remainingSize -= chunkSize;
                offset += chunkSize;

                break;
            }
            case MKTAG('S', 'S', 'N', 'D'):{
                //Data chunk
                if (mValidFormat) {

                    uint8_t DataChunk[8];
                    mDataSource->readAt(offset, DataChunk, 8);

                    uint32_t data_offset = U32_RE_AT(DataChunk);

                    mDataOffset = offset + data_offset + 8;
                    mDataSize = chunkSize;
                    int64_t durationUs = 0;

                    uint32_t bytesPerSample = mBitsPerSample >> 3;

                    ALOGI("AIFFExtractor::mDataSize [%u] byte data, mDataOffset = [%llu], data_offset = [%u] \n",
                    mDataSize, mDataOffset,data_offset);

                    switch (codec_id) {
                    case AV_CODEC_ID_PCM_S8:
                    case AV_CODEC_ID_PCM_S16BE:
                    case AV_CODEC_ID_PCM_S24BE:
                    case AV_CODEC_ID_PCM_S32BE:
                    case AV_CODEC_ID_PCM_S16LE:
                    case AV_CODEC_ID_PCM_F32BE:
                        if (mBitsPerSample != 8 && mBitsPerSample != 16 && mBitsPerSample != 24
                            && mBitsPerSample != 32) {
                            return ERROR_UNSUPPORTED;
                        } else {
                            mTrackMeta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_RAW);
                            durationUs = 1000000LL * (mDataSize / (mNumChannels * bytesPerSample)) / mSampleRate;
                        }
                        break;
                    case AV_CODEC_ID_ADPCM_IMA_QT:

                        if (mBitsPerSample != 4) {
                            ALOGE("%d BitsPerSample ADPCM_IMA_QT are not supported ! \n",mBitsPerSample);
                            return ERROR_UNSUPPORTED;
                        } else {
                        //mTrackMeta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_ADPCM_IMA);

                        if (mNumChannels == 1)
                            durationUs = 1000000LL * (mDataSize * 2) / mSampleRate;
                            if (mNumChannels == 2)
                                durationUs = 1000000LL * mDataSize /mSampleRate;
                        }
                        ALOGE("ADPCM_IMA_QT are not supported ! \n");
                        break;
                    case AV_CODEC_ID_PCM_G711_ALAW:

                        if (mBitsPerSample != 8) {
                            ALOGE("%d BitsPerSample PCM_ALAW are not supported ! \n",mBitsPerSample);
                            return ERROR_UNSUPPORTED;
                        } else {
                            mTrackMeta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_G711_ALAW);
                            durationUs = 1000000LL * (mDataSize / (mNumChannels * bytesPerSample)) / mSampleRate;
                        }
                        break;
                    case AV_CODEC_ID_PCM_G711_MULAW:

                        if (mBitsPerSample != 8) {
                            ALOGE("%d BitsPerSample PCM_ALAW are not supported ! \n",mBitsPerSample);
                            return ERROR_UNSUPPORTED;
                        } else {
                            mTrackMeta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_G711_MLAW);
                            durationUs = 1000000LL * (mDataSize / (mNumChannels * bytesPerSample)) / mSampleRate;
                        }
                        break;
                    case AV_CODEC_ID_QCELP:
                        mTrackMeta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_QCELP);
                        break;
                    case AV_CODEC_ID_PCM_F64BE:
                        ALOGE("64 bitsPerSample float PCM are not supported ! \n");
                        return ERROR_UNSUPPORTED;
                    case AV_CODEC_ID_MACE3:
                    case AV_CODEC_ID_MACE6:
                    case AV_CODEC_ID_GSM:
                    case AV_CODEC_ID_QDM2:
                    case AV_CODEC_ID_ADPCM_G726:
                        ALOGE("AV_CODEC_ID_MACE3/AV_CODEC_ID_MACE6/AV_CODEC_ID_GSM/AV_CODEC_ID_QDM2/AV_CODEC_ID_ADPCM_G726 are not supported ! \n");
                        return ERROR_UNSUPPORTED;
                    default:
                        ALOGV("Unknown AIFF decode %d! \n", codec_id);
                        return ERROR_UNSUPPORTED;
                    }

                    mWaveFormat = codec_id;

                    ALOGI("mTrackMeta::mNumChannels=%d\n", mNumChannels);
                    mTrackMeta->setInt32(kKeyChannelCount, mNumChannels);

                    ALOGI("mTrackMeta::mSampleRate=%d\n", mSampleRate);
                    mTrackMeta->setInt32(kKeySampleRate, mSampleRate);

                    ALOGI("mTrackMeta::mBlockAlign=%d\n", mBlockAlign);
                    mTrackMeta->setInt32(kKeyBlockAlign, mBlockAlign);

                    ALOGI("mTrackMeta::durationUs=%lld us\n", durationUs);
                    mTrackMeta->setInt64(kKeyDuration, durationUs);

                    return OK;
                }
            }
            default:{
                //other Chunks
                ALOGV("The Unknown Chunk Name [%s] \n", chunkHeader);

                remainingSize -= chunkSize;
                offset += chunkSize;

                break;
            }
        }
        if (offset & 1) { /* Always even aligned */
            offset++;
            remainingSize--;
        }
    }
    return NO_INIT;

}

//-----------------------------------------------------------------------------

AIFFExtractor::AIFFExtractor(const sp<DataSource> &source)
    : mDataSource(source),
    mValidFormat(false) {
    mInitCheck = init();
}

AIFFExtractor::~AIFFExtractor() {}

sp<MetaData> AIFFExtractor::getMetaData() {
    sp<MetaData> meta = new MetaData;

    if (mInitCheck != OK) {
        return meta;
    }

    meta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_CONTAINER_AIFF);

    return meta;
}

size_t AIFFExtractor::countTracks() {
    return mInitCheck == OK ? 1 : 0;
}

sp<IMediaSource> AIFFExtractor::getTrack(size_t index) {
    if (mInitCheck != OK || index > 0) {
        return NULL;
    }

    return new AIFFSource(mDataSource, mTrackMeta, mWaveFormat, mBitsPerSample, mDataOffset,
		     mDataSize, mBlockAlign, mSampleRate, mNumChannels);
}

sp<MetaData> AIFFExtractor::getTrackMetaData(size_t index, uint32_t flags) {
        
    if (mInitCheck != OK || index > 0) {
        return NULL;
    }
    return mTrackMeta;
}

//---------------------------AIFF Source-------------------------------------------

const size_t AIFFSource::kMaxFrameSize = 32768;

AIFFSource::AIFFSource(
        const sp<DataSource> &dataSource,
        const sp<MetaData> &meta,
        uint16_t waveFormat,
        int32_t bitsPerSample,
        off64_t offset, size_t size,
        uint32_t BlockAlign,
        uint32_t SampleRate,
        uint16_t NumChannels) 
      :	mDataSource(dataSource),
        mMeta(meta),
        mWaveFormat(waveFormat),
        mSampleRate(SampleRate),
      	mNumChannels(NumChannels),
      	mBitsPerSample(bitsPerSample),
      	mOffset(offset),
      	mSize(size),
      	mBlockAlign(BlockAlign),
      	mStarted(false),
      	mGroup(NULL){
   
    	mMeta->setInt32(kKeyMaxInputSize, kMaxFrameSize);
}

AIFFSource::~AIFFSource() {
	if (mStarted) {
        stop();
    }
}

status_t AIFFSource::stop() {
    ALOGV("AIFFSource::stop \n");

    CHECK(mStarted);

    delete mGroup;
    mGroup = NULL;

    mStarted = false;

    return OK;
}

status_t AIFFSource::start(MetaData *params) {

    ALOGV("AIFFSource::start \n");

    CHECK(!mStarted);

    mGroup = new MediaBufferGroup;
    mGroup->add_buffer(new MediaBuffer(kMaxFrameSize));

    if (mBitsPerSample == 8) {
        // As a temporary buffer for 8->16 bit conversion.
        mGroup->add_buffer(new MediaBuffer(kMaxFrameSize));
    }

    mCurrentPos = mOffset;

    mStarted = true;

    return OK;
}


status_t AIFFSource::read(MediaBuffer **out, const ReadOptions *options) {
    *out = NULL;
    int32_t BlockAlign = mBlockAlign;

    int64_t seekTimeUs;
    ReadOptions::SeekMode mode;

    if (options != NULL && options->getSeekTo(&seekTimeUs, &mode)) {
        int64_t pos;
        if (mWaveFormat == AV_CODEC_ID_ADPCM_IMA_QT) {
            pos = (seekTimeUs * mSampleRate) / 1000000 * mNumChannels / 2;
            pos = pos - (pos % BlockAlign);
        } else {
            pos = (seekTimeUs * mSampleRate) / 1000000 * mNumChannels * (mBitsPerSample >> 3);
        }

        if (pos > mSize) {
            pos = mSize;
        }
        mCurrentPos = pos + mOffset;
    }

    MediaBuffer *buffer;
    status_t err = mGroup->acquire_buffer(&buffer);
    if (err != OK) {
        return err;
    }

    size_t maxBytesToRead = 0;
    if (mBitsPerSample >= 8) {
        maxBytesToRead = mBitsPerSample == 8 ? kMaxFrameSize / 2 : kMaxFrameSize;
        maxBytesToRead -= maxBytesToRead%(mNumChannels*mBitsPerSample>>3);
    } else if (mWaveFormat == AV_CODEC_ID_ADPCM_IMA_QT) {
        maxBytesToRead = BlockAlign;
    }

    size_t maxBytesAvailable =(mCurrentPos - mOffset >= (off64_t)mSize) ? 0 : mSize - (mCurrentPos - mOffset);

    if (maxBytesToRead > maxBytesAvailable) {
        maxBytesToRead = maxBytesAvailable;
    }

    ssize_t n = mDataSource->readAt(mCurrentPos, buffer->data(), maxBytesToRead);

    if (n <= 0) {
        buffer->release();
        buffer = NULL;
        return ERROR_END_OF_STREAM;
    }

    buffer->set_range(0, n);

    switch (mWaveFormat) {

        case AV_CODEC_ID_PCM_S16BE:{
            //Convert 16-bit signed BE samples to 16-bit signed LE samples.

            const uint8_t *src = (const uint8_t *)buffer->data();
            int16_t *dst = (int16_t *)buffer->data();

            ssize_t numSamples = n/2;

            for (size_t i = 0; i < numSamples; ++i) {
                int16_t x = (int16_t)(src[0] << 8 | src[1]);

                *dst++ = (int16_t)x;
                src += 2;
            }
            break;
        }
        case AV_CODEC_ID_PCM_S16LE:{
            //android direct output is PCM 16bit LE
            break;
        }
        case AV_CODEC_ID_PCM_S8:{
            // Convert 8-bit unsigned samples to 16-bit signed.
            MediaBuffer *tmp;
            CHECK_EQ(mGroup->acquire_buffer(&tmp), (status_t)OK);

            // The new buffer holds the sample number of samples, but each one is 2 bytes wide.
            tmp->set_range(0, 2 * n);

            int16_t *dst = (int16_t *)tmp->data();
            const int8_t *src = (const int8_t *)buffer->data();
            ssize_t numBytes = n;

            for (size_t i = 0; i < numBytes; i++) {
                *dst++ = (int16_t)(*src++) * 256;
            }

            buffer->release();
            buffer = tmp;

            break;
        }
        case AV_CODEC_ID_PCM_S24BE:{
            // Convert 24-bit signed samples to 16-bit signed.

            const uint8_t *src =(const uint8_t *)buffer->data();
            int16_t *dst = (int16_t *)src;

            size_t numSamples = buffer->range_length() / 3;
            for (size_t i = 0; i < numSamples; ++i) {
                int32_t x = (int32_t)(src[0]<<16 | src[1] << 8 | src[2]);
                x = (x << 8) >> 8;  // sign extension

                x = x >> 8;
                *dst++ = (int16_t)x;
                src += 3;
            }
            buffer->set_range(buffer->range_offset(), 2 * numSamples);
            break;
        }
        case AV_CODEC_ID_PCM_S32BE:{
            // Convert 32-bit signed samples to 16-bit signed.

            const uint8_t *src =(const uint8_t *)buffer->data();
            int16_t *dst = (int16_t *)src;

            size_t numSamples = buffer->range_length() / 4;

            for (size_t i = 0; i < numSamples; ++i) {
                int16_t x = (int16_t)(src[0] << 8 | src[1]);

                *dst++ = (int16_t)x;
                src += 4;
            }
            buffer->set_range(buffer->range_offset(), 2 * numSamples);
            break;
        }
        case AV_CODEC_ID_PCM_F32BE:{
            // Convert 32-bit float point samples to 16-bit signed fixed point.

            const float *src =(const float *)buffer->data();
            int16_t *dst = (int16_t *)src;

            size_t numSamples = buffer->range_length() / 4;

            for (size_t i = 0; i < numSamples; ++i) {

                float x = FloatSwap(*src);

                int32_t y = (int32_t)(x * (1<<15) + 0.5f);
                y = Clip(y,-32768,32767);

                *dst++ = (int16_t)y;
                src ++;
            }
            buffer->set_range(buffer->range_offset(), 2 * numSamples);
            break;
        }
        default:
        break;
    }

    size_t bytesPerSample = mBitsPerSample >> 3;

    int64_t keytemp;
    if ((mBitsPerSample == 4) || (mWaveFormat == AV_CODEC_ID_ADPCM_IMA_QT)) {
        if (mNumChannels == 1)
            keytemp = 1000000LL * ((mCurrentPos - mOffset) * 2) / mSampleRate;
        if (mNumChannels == 2)
            keytemp = 1000000LL * (mCurrentPos - mOffset) / mSampleRate;
    }
    else {
        keytemp = 1000000LL * (mCurrentPos - mOffset) / (mNumChannels * bytesPerSample) / mSampleRate;
    }

    buffer->meta_data()->setInt64(kKeyTime, keytemp);
    buffer->meta_data()->setInt32(kKeyIsSyncFrame, 1);
    mCurrentPos += n;

    *out = buffer;

    return OK;
}

sp<MetaData> AIFFSource::getFormat() {
    ALOGV("AIFFSource::getFormat \n");
    return mMeta;
}

//--------------------------------------------------------------------------------
bool SniffAIFF(const sp<DataSource> &source, String8 *mimeType, float *confidence,
        sp<AMessage> *) {
        
    ALOGV("find Aiff format in function %s, in Line %d\n",__FUNCTION__,__LINE__);

	char header[12];
	if (source->readAt(0, header, sizeof(header)) < (ssize_t)sizeof(header)) {
		return false;
	}
	
	if (memcmp(header, "FORM", 4) || (memcmp(&header[8], "AIFF", 4) && memcmp(&header[8], "AIFC", 4))) {
        return false;
    }
	
    *mimeType = MEDIA_MIMETYPE_CONTAINER_AIFF;
    *confidence = 0.3f;

    return true;
}

}  // namespace android
