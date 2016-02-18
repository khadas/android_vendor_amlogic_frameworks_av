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

#define LOG_TAG "ADIFExtractor"
#include <utils/Log.h>

#include "include/ADIFExtractor.h"

#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/MediaBufferGroup.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MetaData.h>
#include <utils/String8.h>


namespace android {

struct ADIFSource : public MediaSource {
    ADIFSource(
			const sp<DataSource> &dataSource,
            const sp<MetaData> &meta,
            uint16_t adifHead,
            int32_t bitsPerSample,
            off64_t offset, size_t size,Vector<uint64_t> SeekOffsetVec);

    virtual status_t start(MetaData *params = NULL);
    virtual status_t stop();
    virtual sp<MetaData> getFormat();

    virtual status_t read(
            MediaBuffer **buffer, const ReadOptions *options = NULL);

protected:
    virtual ~ADIFSource();

private:
    static const size_t kMaxFrameSize;
	
	
    sp<DataSource> mDataSource;
    sp<MetaData> mMeta;
	uint16_t mAdifHead;
    int32_t mSampleRate;
	int32_t mBitRate;
    int32_t mNumChannels;
    int32_t mBitsPerSample;
    off64_t mOffset;
    size_t mSize;
    bool mStarted;
    MediaBufferGroup *mGroup;
    off64_t mCurrentPos;
    Vector<uint64_t> mSeekOffsetVec;
   

    ADIFSource(const ADIFSource &);
    ADIFSource &operator=(const ADIFSource &);
};

////////////////////////////////////////////////////////////////////////////////



ADIFExtractor::ADIFExtractor(const sp<DataSource> &source)
    : mDataSource(source),
      mInitCheck(NO_INIT){
	mInitCheck = init_adif();
}
  

ADIFExtractor::~ADIFExtractor() {
}

sp<MetaData> ADIFExtractor::getMetaData() {
    sp<MetaData> meta = new MetaData;

    if (mInitCheck != OK) {
        return meta;
    }

    meta->setCString(kKeyMIMEType, "audio/aac-adif");

    return meta;
}

size_t ADIFExtractor::countTracks() {
    return mInitCheck == OK ? 1 : 0;
}

sp<MediaSource> ADIFExtractor::getTrack(size_t index) {
    if (mInitCheck != OK || index > 0) {
        return NULL;
    }

    return new ADIFSource(mDataSource, mTrackMeta,
            mAdifHead, mBitsPerSample, mDataOffset, mDataSize,mOffsetVector);
}

sp<MetaData> ADIFExtractor::getTrackMetaData(size_t index, uint32_t flags) {
    if (mInitCheck != OK || index > 0) {
        return NULL;
    }

    return mTrackMeta;
}

status_t ADIFExtractor::init_adif() {
	
	size_t buflen = 768;
	unsigned char buffer[buflen];
	off64_t streamSize = 0;
	int64_t length = 0;
	int bitrate = 0;
	
	if (mDataSource->getSize(&streamSize) != OK){
		ALOGI("file size equal zero\n");
		return NO_INIT;
	}
	ALOGI("streamSize=%lld\n", streamSize);
	
	if (mDataSource->readAt(0, buffer, buflen) < buflen) {
        return NO_INIT;
    }
	if (memcmp(buffer, "ADIF", 4)) {
        return NO_INIT;
    }else{
		int skip_size = (buffer[4] & 0x80) ? 9 : 0;
		bitrate = ((unsigned int)(buffer[4 + skip_size] & 0x0F)<<19) |
			((unsigned int)buffer[5 + skip_size]<<11) |
			((unsigned int)buffer[6 + skip_size]<<3) |
			((unsigned int)buffer[7 + skip_size] & 0xE0);
	
		length = (int64_t)streamSize;
		if (bitrate != 0)
		{
			length = 1000000ll*(length * 8)/(bitrate);
		}
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
	
	ALOGI("[%s %d]AdifSize/%d\n",__FUNCTION__,__LINE__,bread);
	{
	   #define AAC_ADIF_MIN_STREAMSIZE 768
	   #define AAC_ADIF_CHANNELS       8
	   unsigned int file_offset=bread;
	   unsigned int  inlen=AAC_ADIF_MIN_STREAMSIZE*AAC_ADIF_CHANNELS;
	   unsigned char input[AAC_ADIF_MIN_STREAMSIZE*AAC_ADIF_CHANNELS]={0};
	   int byte_need,i;

	   byte_need=inlen;
	   NeAACDecInit_EnableParseOnly(hDecoder);;
	   while((file_offset+byte_need<=streamSize) &&
	          mDataSource->readAt(file_offset, input+inlen-byte_need,byte_need)==byte_need
	        )
	   {
	      file_offset+=byte_need;
	      NeAACDecDecode(hDecoder, &frameInfo,input,inlen);
	      if ((frameInfo.error == 0) && (frameInfo.samples > 0))
	      {
	         mOffsetVector.push(file_offset-inlen);
	      }
	      //ALOGI("frameInfo.error/%d bytesconsumed/%d offset/%d\n",frameInfo.error,frameInfo.bytesconsumed,file_offset-inlen);
	      byte_need=frameInfo.bytesconsumed;
	      memmove(input,input+frameInfo.bytesconsumed,inlen-frameInfo.bytesconsumed);
	   }
	   ALOGI("mOffsetVector.size()/%d\n",mOffsetVector.size());
	}

	mAdifHead = (uint16_t)bread;
	mDataSize = (size_t)streamSize;
    mDataOffset = 0;

	mNumChannels = (uint16_t)channels;
	mSampleRate = (uint32_t)samplerate;
	mBitRate = bitrate;
	mBitsPerSample = 16;
                
    mTrackMeta = new MetaData;
    mTrackMeta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_AAC_ADIF);
    mTrackMeta->setInt32(kKeyChannelCount, mNumChannels);
    mTrackMeta->setInt32(kKeySampleRate, mSampleRate);
	mTrackMeta->setInt32(kKeyBitRate, bitrate);
    mTrackMeta->setInt64(kKeyDuration, length);

	NeAACDecClose(hDecoder);

    return OK;

}


////////////////////////////////////////////////////////////////////////////////


const size_t ADIFSource::kMaxFrameSize = 768*8;

ADIFSource::ADIFSource(
        const sp<DataSource> &dataSource,
        const sp<MetaData> &meta,
        uint16_t adifHead,
        int32_t bitsPerSample,
        off64_t offset, size_t size,Vector<uint64_t> SeekOffsetVec)
    : mDataSource(dataSource),
      mMeta(meta),
      mAdifHead(adifHead),
      mSampleRate(0),
      mBitRate(0),
      mNumChannels(0),
      mBitsPerSample(bitsPerSample),
      mOffset(offset),
      mSize(size),
      mStarted(false),
      mGroup(NULL)  {
      CHECK(mMeta->findInt32(kKeySampleRate, &mSampleRate));
	  CHECK(mMeta->findInt32(kKeyBitRate, &mBitRate));
      CHECK(mMeta->findInt32(kKeyChannelCount, &mNumChannels));
	  
	   mMeta->setInt32(kKeyMaxInputSize, kMaxFrameSize);
	   mSeekOffsetVec=SeekOffsetVec;
}

ADIFSource::~ADIFSource() {
    if (mStarted) {
        stop();
    }
}

status_t ADIFSource::start(MetaData *params) {
    CHECK(!mStarted);

    
    mGroup = new MediaBufferGroup;
    mGroup->add_buffer(new MediaBuffer(kMaxFrameSize));
	
	mCurrentPos = mOffset;
    mStarted = true;

    return OK;
}

status_t ADIFSource::stop() {
    CHECK(mStarted);

    delete mGroup;
    mGroup = NULL;

    mStarted = false;
    return OK;
}

sp<MetaData> ADIFSource::getFormat() {
    return mMeta;
}

status_t ADIFSource::read(
        MediaBuffer **out, const ReadOptions *options) {
        
    *out = NULL;
#if 0
	int64_t seekTimeUs;
		ReadOptions::SeekMode mode;
		if (options != NULL && options->getSeekTo(&seekTimeUs, &mode)) {
			

			ALOGI("seekTimeUs=%lld\n",seekTimeUs);
			int64_t pos = 0.15 * mSize ;
			if (pos > mSize) {
				pos = mSize;
			}
			mCurrentPos = pos + mOffset;
			unsigned char temp;
			while(1){
				if(mDataSource->readAt(mCurrentPos,&temp,1) < 1)
					return ERROR_END_OF_STREAM;
				if(temp == 0x20)
				{	
					if(mDataSource->readAt(mCurrentPos + 2,&temp,1) < 1)
						return ERROR_END_OF_STREAM;
					if(temp == 0xC)
						break;
				}
				mCurrentPos ++;
			}

		}
#endif	
		int64_t seekTimeUs;
		ReadOptions::SeekMode mode;
		if (options && options->getSeekTo(&seekTimeUs, &mode))
		{
		    int64_t seekoffset = seekTimeUs*mBitRate/1000000LL/8;
		    int i,VecNum;
		    VecNum=mSeekOffsetVec.size();
		    for(i=0;i<VecNum-1;i++){
		       if(mSeekOffsetVec.itemAt(i)<=seekoffset && seekoffset<=mSeekOffsetVec.itemAt(i+1)){
		            mCurrentPos=mSeekOffsetVec.itemAt(i);
		            break;
		       }
		    }
		    if(VecNum>0){
		       if(seekoffset<mSeekOffsetVec.itemAt(0))
		           seekoffset=mSeekOffsetVec.itemAt(0);
		       else if(seekoffset<mSeekOffsetVec.itemAt(VecNum-1))
		           seekoffset=mSeekOffsetVec.itemAt(VecNum-1);
		    }
		    ALOGI("Adif Seek:mBitRate/%d seekTimeUs/%lld seekoffset/%lld mCurrentPos/%lld\n",mBitRate,seekTimeUs,seekoffset,mSeekOffsetVec.itemAt(i));
		}

		MediaBuffer *buffer;
		status_t err = mGroup->acquire_buffer(&buffer);
		if (err != OK) {
			return err;
		}
	
		size_t maxBytesAvailable =
			(mCurrentPos - mOffset >= (off64_t)mSize)
				? 0 : mSize - (mCurrentPos - mOffset);

		size_t maxBytesToRead = kMaxFrameSize;
		if (maxBytesToRead > maxBytesAvailable) {
			maxBytesToRead = maxBytesAvailable;
		}

		ssize_t n;
		if(mCurrentPos == 0){
		    if((n = mDataSource->readAt(
				mCurrentPos, buffer->data(),
				mAdifHead)) < mAdifHead)
				
				return ERROR_END_OF_STREAM;
				
		}else{
		     n = mDataSource->readAt(
				mCurrentPos, buffer->data(),
				maxBytesToRead);
		}
	
		if (n <= 0) {
			buffer->release();
			buffer = NULL;
	
			return ERROR_END_OF_STREAM;
		}
		

		buffer->set_range(0, n);
	
		buffer->meta_data()->setInt64(
					kKeyTime,
					1000000LL * (mCurrentPos - mOffset) * 8
						/  mBitRate);
	
		buffer->meta_data()->setInt32(kKeyIsSyncFrame, 1);
		mCurrentPos += n;
		


    *out = buffer;
    return OK;
}

////////////////////////////////////////////////////////////////////////////////

bool SniffADIF(
        const sp<DataSource> &source, String8 *mimeType, float *confidence,
        sp<AMessage> *) {
		
	uint8_t header2[4];
	if(source->readAt(0, &header2, 4) != 4){
		return false;
	}
	if(memcmp(header2, "ADIF", 4)){
		return false;

	}

	*mimeType = MEDIA_MIMETYPE_AUDIO_AAC_ADIF;
    *confidence = 0.3f;
	
    return true;
}

}  // namespace android
