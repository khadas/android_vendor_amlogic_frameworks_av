/*
 * Copyright (C) 2011 The Android Open Source Project
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

#define LOG_TAG "LATMExtractor"
#include <utils/Log.h>

#include "include/LATMExtractor.h"

#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/MediaBufferGroup.h>
#include <media/stagefright/MediaDefs.h>
#ifdef WITH_AMLOGIC_MEDIA_EX_SUPPORT
#include <media/stagefright/AmMediaDefsExt.h>
#endif

#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MetaData.h>
#include <utils/String8.h>

namespace android {

struct LATMSource : public MediaSource {
public:
	LATMSource(
			const sp<DataSource> &dataSource,
            const sp<MetaData> &meta,
            const Vector<uint64_t> &offset_vector,
            uint16_t latmHead,
            int64_t frame_duration_us,
            size_t size);

    virtual status_t start(MetaData *params = NULL);
    virtual status_t stop();
    virtual sp<MetaData> getFormat();

    virtual status_t read(
            MediaBuffer **buffer, const ReadOptions *options = NULL);

protected:
    virtual ~LATMSource();

private:
    static const size_t kMaxFrameSize;
	
	
    sp<DataSource> mDataSource;
    sp<MetaData> mMeta;
    int32_t mSampleRate;
    int32_t mNumChannels;
	uint16_t mLatmHead;
    size_t mSize;
    bool mStarted;

	Vector<uint64_t> mOffsetVector;
	int64_t mFrameDurationUs;
	
    MediaBufferGroup *mGroup;
    int64_t mCurrentTimeUs;
	size_t mOffsetPos;
	off64_t mCurrentPos;

	
    LATMSource(const LATMSource &);
    LATMSource &operator=(const LATMSource &);
};

////////////////////////////////////////////////////////////////////////////////



LATMExtractor::LATMExtractor(const sp<DataSource> &source)
    : mDataSource(source),
      mInitCheck(NO_INIT),
      mFrameDurationUs(0){
	mInitCheck = init_latm();
}
  

LATMExtractor::~LATMExtractor() {
}

sp<MetaData> LATMExtractor::getMetaData() {
    sp<MetaData> meta = new MetaData;

    if (mInitCheck != OK) {
        return meta;
    }

    meta->setCString(kKeyMIMEType, "audio/aac-latm");

    return meta;
}

size_t LATMExtractor::countTracks() {
    return mInitCheck == OK ? 1 : 0;
}

sp<IMediaSource> LATMExtractor::getTrack(size_t index) {
    if (mInitCheck != OK || index > 0) {
        return NULL;
    }

    return new LATMSource(mDataSource, mTrackMeta, mOffsetVector, mLatmHead,
		mFrameDurationUs, mDataSize);
}

sp<MetaData> LATMExtractor::getTrackMetaData(size_t index, uint32_t flags) {
    if (mInitCheck != OK || index > 0) {
        return NULL;
    }

    return mTrackMeta;
}

status_t LATMExtractor::init_latm() {

	int buflen = 768*6;
	unsigned char buffer[buflen];
	if (mDataSource->readAt(0, buffer, buflen) < buflen){
		return NO_INIT;
	}

	NeAACDecHandle hDecoder;
	NeAACDecFrameInfo frameInfo;
	NeAACDecConfigurationPtr config;

	hDecoder = NeAACDecOpen();
	config = NeAACDecGetCurrentConfiguration(hDecoder);
	config->defSampleRate = 44100;
    config->defObjectType = LC;
    config->outputFormat = FAAD_FMT_16BIT;
    config->downMatrix = 0;
    config->useOldADTSFormat = 0;
	NeAACDecSetConfiguration(hDecoder, config);
	unsigned long samplerate;
    unsigned char channels;
	int bread;
	if ((bread = NeAACDecInit(hDecoder, buffer,
        sizeof(buffer), &samplerate, &channels)) < 0){
		ALOGI("NeAACDecInit failed\n");
		NeAACDecClose(hDecoder);
		return NO_INIT;
	}
	mLatmHead = (uint16_t)bread; 
	
	off64_t offset = 0;
	off64_t streamSize = 0;
	unsigned char tempbuf[buflen];
	int readsize = 0;
	unsigned long reoff = 0;
	off64_t numFrames = 0; 
	int64_t duration = 0;
	if (mDataSource->getSize(&streamSize) == OK){
		while (offset < streamSize) {
			if((readsize = mDataSource->readAt(offset, tempbuf, buflen)) <= 0)
				break;
            if ((reoff = GetAACFrames(hDecoder,tempbuf,readsize)) < 1) {
                break;
            }
			
			mOffsetVector.push(offset);

            offset += reoff;
            numFrames ++;
       }
		mFrameDurationUs = (1024 * 1000000ll + (samplerate - 1)) / samplerate;
        duration = numFrames * mFrameDurationUs;
	   
	}
	
	mNumChannels = channels;
	mSampleRate = samplerate;
                
    mTrackMeta = new MetaData;
    mTrackMeta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_AAC_LATM);
    mTrackMeta->setInt32(kKeyChannelCount, mNumChannels);
    mTrackMeta->setInt32(kKeySampleRate, mSampleRate);

    mTrackMeta->setInt64(kKeyDuration, duration);

	NeAACDecClose(hDecoder);
	
    return OK;

}


////////////////////////////////////////////////////////////////////////////////


const size_t LATMSource::kMaxFrameSize = 768*6;

LATMSource::LATMSource(
        const sp<DataSource> &dataSource,
        const sp<MetaData> &meta,
        const Vector<uint64_t> &offset_vector,
        uint16_t latmHead,
        int64_t frame_duration_us,
        size_t size)
    : mDataSource(dataSource),
      mMeta(meta),
      mSampleRate(0),
      mNumChannels(0),
      mLatmHead(latmHead),
      mSize(size),
      mStarted(false),
      mOffsetVector(offset_vector),
      mFrameDurationUs(frame_duration_us),
      mGroup(NULL),
      mCurrentTimeUs(0) {
      CHECK(mMeta->findInt32(kKeySampleRate, &mSampleRate));
      CHECK(mMeta->findInt32(kKeyChannelCount, &mNumChannels));
	  
	   mMeta->setInt32(kKeyMaxInputSize, kMaxFrameSize);
}

LATMSource::~LATMSource() {
    if (mStarted) {
        stop();
    }
}

status_t LATMSource::start(MetaData *params) {
    CHECK(!mStarted);

    
    mGroup = new MediaBufferGroup;
    mGroup->add_buffer(new MediaBuffer(kMaxFrameSize));
	
	mCurrentTimeUs = 0;
	mCurrentPos = 0;
	mOffsetPos = 0;
    mStarted = true;
	
    return OK;
}

status_t LATMSource::stop() {
    CHECK(mStarted);

    delete mGroup;
    mGroup = NULL;

    mStarted = false;
    return OK;
}

sp<MetaData> LATMSource::getFormat() {
    return mMeta;
}

status_t LATMSource::read(
        MediaBuffer **out, const ReadOptions *options) {
    *out = NULL;

	int64_t seekTimeUs;
		ReadOptions::SeekMode mode;
		if (options && options->getSeekTo(&seekTimeUs, &mode)) {
			if (mFrameDurationUs > 0) {
				int64_t seekFrame = seekTimeUs / mFrameDurationUs;
				mCurrentTimeUs = seekFrame * mFrameDurationUs;
				
				mOffsetPos = seekFrame;
			}
		}

		if(mOffsetPos >= (mOffsetVector.size() - 1))
			return ERROR_END_OF_STREAM;

		MediaBuffer *buffer;
		status_t err = mGroup->acquire_buffer(&buffer);
		if (err != OK) {
			return err;
		}

		if(mCurrentPos == 0){
			ssize_t n = mDataSource->readAt(
					mCurrentPos, buffer->data(),
					768*2);
			if (n <= 0) {
				buffer->release();
				buffer = NULL;
	
				return ERROR_END_OF_STREAM;
			}

			buffer->set_range(0 , n);
			mCurrentPos += 768*2;
			buffer->meta_data()->setInt64(kKeyTime, 0);
			buffer->meta_data()->setInt32(kKeyIsSyncFrame, 1);
			
		}
		else{
			
			size_t maxBytesToRead = mOffsetVector.itemAt(mOffsetPos + 1) - mOffsetVector.itemAt(mOffsetPos);
			ssize_t n;
			if(mOffsetPos != 0)
	   		 	n = mDataSource->readAt(
					mOffsetVector.itemAt(mOffsetPos) - 2, buffer->data(),
					maxBytesToRead + 2);
			else
				n = mDataSource->readAt(
					mOffsetVector.itemAt(mOffsetPos), buffer->data(),
					maxBytesToRead);
	
			if (n <= 0) {
				buffer->release();
				buffer = NULL;
	
				return ERROR_END_OF_STREAM;
			}
	
			buffer->set_range(0, n);
	
			mOffsetPos += 1;

		
			buffer->meta_data()->setInt64(kKeyTime, mCurrentTimeUs);
			buffer->meta_data()->setInt32(kKeyIsSyncFrame, 1);

			mCurrentTimeUs += mFrameDurationUs;
		}


    *out = buffer;
	
    return OK;
}

////////////////////////////////////////////////////////////////////////////////

bool SniffLATM(
        const sp<DataSource> &source, String8 *mimeType, float *confidence,
        sp<AMessage> *) {
        
	NeAACDecHandle hDecoder;
	NeAACDecConfigurationPtr config;
	unsigned long samplerate;
    unsigned char channels;
	hDecoder = NeAACDecOpen();
	config = NeAACDecGetCurrentConfiguration(hDecoder);
	config->defSampleRate = 441000;
    config->defObjectType = LC;
    config->outputFormat = FAAD_FMT_16BIT;
    config->downMatrix = 0;
    config->useOldADTSFormat = 0;
	NeAACDecSetConfiguration(hDecoder, config);
	
	unsigned char buffer[768*6];
	if(source->readAt(0, &buffer, sizeof(buffer)) <=0 ){
		NeAACDecClose(hDecoder);
		return false;
	}
	
	if(NeAACDecInit(hDecoder, buffer,sizeof(buffer), &samplerate, &channels) < 0){
		NeAACDecClose(hDecoder);
		return false;
	}
	
	if(((unsigned char *)hDecoder)[2] == 1){
		ALOGI("=====LATM=====\n");
		*mimeType = MEDIA_MIMETYPE_AUDIO_AAC_LATM;
    	*confidence = 0.3f;
		return true;
	}
	NeAACDecClose(hDecoder);
    return false;
}

}  // namespace android
