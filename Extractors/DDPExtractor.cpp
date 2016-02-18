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
#define LOG_TAG "DDPExtractor"
#include <utils/Log.h>

#include "include/DDPExtractor.h"

#include <media/stagefright/DataSource.h>
#include <media/stagefright/MediaBufferGroup.h>
//#include <media/stagefright/MediaDebug.h>
#include <media/stagefright/foundation/ADebug.h>

#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MetaData.h>
#include <utils/String8.h>

namespace android {

struct DDPSource : public MediaSource {
    DDPSource(
            const sp<DataSource> &dataSource,
            const sp<MetaData> &meta,
            const Vector<uint64_t> &offset_vector,
            const Vector<uint64_t> &length_vector,
            int64_t frame_duration_us);

    virtual status_t start(MetaData *params = NULL);
    virtual status_t stop();
    virtual sp<MetaData> getFormat();

    virtual status_t read(
            MediaBuffer **buffer, const ReadOptions *options = NULL);

protected:
    virtual ~DDPSource();

private:
    static const size_t kMaxFrameSize;
    sp<DataSource> mDataSource;
    sp<MetaData> mMeta;
	
    int32_t mSampleRate;
    int32_t mNumChannels;
    int32_t mBitsPerSample;
	
    off64_t mOffset;
	int64_t mCurrentTimeUs;
    bool mStarted;
    MediaBufferGroup *mGroup;

	Vector<uint64_t> mOffsetVector;
	Vector<uint64_t> mFrameLVector;
	int64_t mFrameDurationUs;

    DDPSource(const DDPSource &);
    DDPSource &operator=(const DDPSource &);
};

DDPExtractor::DDPExtractor(const sp<DataSource> &source)
    :mDataSource(source){
    mInitCheck = init();
}

DDPExtractor::~DDPExtractor() {
}

bool IsSeeminglyValidDDPAudioHeader(const uint8_t *ptr, size_t size) {
    if (size < 2) return false;
    if (ptr[0] == 0x0b && ptr[1] == 0x77) return true;
    if (ptr[0] == 0x77 && ptr[1] == 0x0b) return true;
    return false;
}

int calc_dd_frame_size(int code)
{
    static const int FrameSize32K[] = { 96, 96, 120, 120, 144, 144, 168, 168, 192, 192, 240, 240, 288, 288, 336, 336, 384, 384, 480, 480, 576, 576, 672, 672, 768, 768, 960, 960, 1152, 1152, 1344, 1344, 1536, 1536, 1728, 1728, 1920, 1920 };
    static const int FrameSize44K[] = { 69, 70, 87, 88, 104, 105, 121, 122, 139, 140, 174, 175, 208, 209, 243, 244, 278, 279, 348, 349, 417, 418, 487, 488, 557, 558, 696, 697, 835, 836, 975, 976, 114, 1115, 1253, 1254, 1393, 1394 };
    static const int FrameSize48K[] = { 64, 64, 80, 80, 96, 96, 112, 112, 128, 128, 160, 160, 192, 192, 224, 224, 256, 256, 320, 320, 384, 384, 448, 448, 512, 512, 640, 640, 768, 768, 896, 896, 1024, 1024, 1152, 1152, 1280, 1280 };

    int fscod = (code >> 6) & 0x3;
    int frmsizcod = code & 0x3f;

    if (fscod == 0) return 2 * FrameSize48K[frmsizcod];
    if (fscod == 1) return 2 * FrameSize44K[frmsizcod];
    if (fscod == 2) return 2 * FrameSize32K[frmsizcod];

    return 0;
}

sp<MetaData> DDPExtractor::getMetaData() {
    sp<MetaData> meta = new MetaData;

    if (mInitCheck != OK) {
        return meta;
    }

    meta->setCString(kKeyMIMEType, "audio/ddp");

    return meta;
}

size_t DDPExtractor::countTracks() {
    return mInitCheck == OK ? 1 : 0;
}

sp<MediaSource> DDPExtractor::getTrack(size_t index) {
    if (mInitCheck != OK || index > 0) {
        return NULL;
    }

    return new DDPSource(mDataSource, mTrackMeta, mOffsetVector, mFrameLVector, mFrameDurationUs);
}

sp<MetaData> DDPExtractor::getTrackMetaData(
        size_t index, uint32_t flags) {
    if (mInitCheck != OK || index > 0) {
        return NULL;
    }

    return mTrackMeta;
}

status_t DDPExtractor::init() {
    uint8_t header[12];
    if (mDataSource->readAt(
                0, header, sizeof(header)) < (ssize_t)sizeof(header)) {
      ALOGI("%s[%d] NO_INIT\n", __func__, __LINE__);
        return NO_INIT;
    }

	size_t size = sizeof(header);
	uint8_t *ptr = header;
	ssize_t startOffset = -1;

	for(size_t i=0; i < size; ++i){
		if(IsSeeminglyValidDDPAudioHeader(&ptr[i], size-i)){
			startOffset = i;
			break;
		}
	}

	if(startOffset != 0){
		ALOGI("Is not dolby file!\n");
		return NO_INIT;
	}

	ALOGI("startOffset=%d\n", startOffset);

	if((ptr[0] == 0x77)&&(ptr[1] == 0x0B)){
		int i;
		uint8_t temp;
		for(i = 0; i < 10; i += 2){
			temp = ptr[i];
			ptr[i] = ptr[i+1];
			ptr[i+1] = temp;
		}
	}
	
	mTrackMeta = new MetaData;

	int fscod = (ptr[4] >> 6) & 0x3;
	uint32_t sr = 0 ;
	int blks_per_frm = 6;
	off64_t offset = 0;
    off64_t streamSize, numFrames = 0;
    size_t frameSize = 0;
    int64_t duration = 0;
	uint8_t ptr_head[6];
		
	if (fscod == 0) sr = 48000;
    else if (fscod == 1) sr = 44100;
    else if (fscod == 2) sr = 32000;

	int bsid = (ptr[5] >> 3) & 0x1f;
	
	if(bsid > 10 && bsid <= 16){

		mTrackMeta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_EC3);
        ALOGI("MEDIA_MIMETYPE_AUDIO_EC3\n");
	
		int numblkscod = (ptr[4] >> 4) & 0x3;;
		if (numblkscod == 0x3)
		{
			blks_per_frm = 6;
		}
		else
		{
			blks_per_frm = numblkscod + 1;
		}

		

    	if (mDataSource->getSize(&streamSize) == OK) {
                ALOGI("ec3 demux\n");
         	while (offset < streamSize) {
				if (mDataSource->readAt(offset, ptr_head, 6) < 6) {
					ALOGI("ec3 readAt failed !\n");
       			 	return NO_INIT;
    			}
				
				if((ptr_head[0] == 0x0B)&&(ptr_head[1] == 0x77)){
				   if ((frameSize = 2 * ((((ptr_head[2] << 8) | ptr_head[3]) & 0x7ff) + 1)) == 0) {
               			ALOGI("ec3 frame_size == 0\n");
						return NO_INIT;
            		}	
				}else if((ptr_head[0] == 0x77)&&(ptr_head[1] == 0x0B)){
					if ((frameSize = 2 * ((((ptr_head[3] << 8) | ptr_head[2]) & 0x7ff) + 1)) == 0) {
               			ALOGI("ec3 frame_size == 0\n");
						return NO_INIT;
            		}
				}
				else{
					ALOGI("ec3 demux failed !\n");
					return NO_INIT;
				}

            mOffsetVector.push(offset);
			mFrameLVector.push(frameSize);

            offset += frameSize;
            numFrames ++;
        }

        // Round up and get the duration
        mFrameDurationUs = (blks_per_frm * 256 * 1000000ll + (sr - 1)) / sr;
        duration = numFrames * mFrameDurationUs;
        mTrackMeta->setInt64(kKeyDuration, duration);
		mTrackMeta->setInt32(kKeyChannelCount, 2);
    	mTrackMeta->setInt32(kKeySampleRate, sr);
    }

		return OK;
	}else {

		//frameSize = calc_dd_frame_size(ptr[4]);
		mTrackMeta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_AC3);
        ALOGI("MEDIA_MIMETYPE_AUDIO_AC3\n");

    	if (mDataSource->getSize(&streamSize) == OK) {
			ALOGI("ac3 demux\n");
         	while (offset < streamSize) {
				if (mDataSource->readAt(offset, ptr_head, 6) < 6) {
					ALOGI("ac3 readAt failed !\n");
       			 	return NO_INIT;
    			}
				
				if((ptr_head[0] == 0x0B)&&(ptr_head[1] == 0x77)) {
					if ((frameSize = calc_dd_frame_size(ptr_head[4])) == 0) {
               			ALOGI("ac3 frame_size == 0 \n");
						return NO_INIT;
            		}
				}else if((ptr_head[0] == 0x77)&&(ptr_head[1] == 0x0B)){
					if ((frameSize = calc_dd_frame_size(ptr_head[5])) == 0) {
               			ALOGI("ac3 frame_size == 0 \n");
						return NO_INIT;
            		}
				}
				else{
					ALOGI("ac3 demux failed !\n");
					return NO_INIT;
				}
            	

            mOffsetVector.push(offset);
			mFrameLVector.push(frameSize);

            offset += frameSize;
            numFrames ++;
        }

        // Round up and get the duration
        mFrameDurationUs = (blks_per_frm * 256 * 1000000ll + (sr - 1)) / sr;
        duration = numFrames * mFrameDurationUs;
        mTrackMeta->setInt64(kKeyDuration, duration);
		mTrackMeta->setInt32(kKeyChannelCount, 2);
    	mTrackMeta->setInt32(kKeySampleRate, sr);
    }

		return OK;

    }

    return OK;
}
const size_t DDPSource::kMaxFrameSize = 8192;

DDPSource::DDPSource(
        const sp<DataSource> &dataSource,
        const sp<MetaData> &meta,
		const Vector<uint64_t> &offset_vector,
		const Vector<uint64_t> &length_vector,
        int64_t frame_duration_us)
    : mDataSource(dataSource),
      mMeta(meta),
      mOffset(0),
      mCurrentTimeUs(0),
      mStarted(false),
      mGroup(NULL),
      mOffsetVector(offset_vector),
      mFrameLVector(length_vector),
      mFrameDurationUs(frame_duration_us) {
}

DDPSource::~DDPSource() {
    if (mStarted) {
        stop();
    }
}

status_t DDPSource::start(MetaData *params) {
    ALOGI("DDPSource::start");

    CHECK(!mStarted);

	mOffset = 0;
    mCurrentTimeUs = 0;
    mGroup = new MediaBufferGroup;
    mGroup->add_buffer(new MediaBuffer(kMaxFrameSize));
    mStarted = true;

    return OK;
}

status_t DDPSource::stop() {
    ALOGI("DDPSource::stop");

    CHECK(mStarted);

    delete mGroup;
    mGroup = NULL;

    mStarted = false;

    return OK;
}

sp<MetaData> DDPSource::getFormat() {
    ALOGI("DDPSource::getFormat");
    return mMeta;
}

status_t DDPSource::read(
        MediaBuffer **out, const ReadOptions *options) {
    *out = NULL;

    int64_t seekTimeUs;
    ReadOptions::SeekMode mode;
    if (options != NULL && options->getSeekTo(&seekTimeUs, &mode)) {
        if (mFrameDurationUs > 0) {
            int64_t seekFrame = seekTimeUs / mFrameDurationUs;
            mCurrentTimeUs = seekFrame * mFrameDurationUs;

            mOffset = mOffsetVector.itemAt(seekFrame);
        }
    }

	uint8_t ptr_head[6];

   if (mDataSource->readAt(mOffset, ptr_head, 6) < 6) {
			ALOGI("readAt failed read!\n");
       		return ERROR_END_OF_STREAM;
    }

	if((ptr_head[0] == 0x77)&&(ptr_head[1] == 0x0B)){
		int i;
		uint8_t temp;
		for(i = 0; i < 6; i += 2){
			temp = ptr_head[i];
			ptr_head[i] = ptr_head[i+1];
			ptr_head[i+1] = temp;
		}
	}
   
	size_t frameSize;
	int bsid = (ptr_head[5] >> 3) & 0x1f;
	
	if(bsid > 10 && bsid <= 16){
		if ((frameSize = 2 * ((((ptr_head[2] << 8) | ptr_head[3]) & 0x7ff) + 1)) == 0) {
			ALOGI("read_ec3 failed!\n");
			return ERROR_END_OF_STREAM;
		}

	}else{
		if ((frameSize = calc_dd_frame_size(ptr_head[4])) == 0) {
			ALOGI("read_ac3 failed!\n");
			return ERROR_END_OF_STREAM;
		}
	}


    MediaBuffer *buffer;
    status_t err = mGroup->acquire_buffer(&buffer);
    if (err != OK) {
        return err;
    }

    if (mDataSource->readAt(mOffset, buffer->data(),
                frameSize) != frameSize) {
        buffer->release();
        buffer = NULL;

        return ERROR_IO;
    }

    buffer->set_range(0, frameSize);
	buffer->meta_data()->setInt64(kKeyTime, mCurrentTimeUs);
    buffer->meta_data()->setInt32(kKeyIsSyncFrame, 1);
	
    mOffset += frameSize;
    mCurrentTimeUs += mFrameDurationUs;
	
    *out = buffer;

    return OK;
}

////////////////////////////////////////////////////////////////////////////////

bool SniffDDP(
        const sp<DataSource> &source, String8 *mimeType, float *confidence,
        sp<AMessage> *) {
    char header[2];
    if (source->readAt(0, header, sizeof(header)) < (ssize_t)sizeof(header)) {
        return false;
    }
	if(((header[0]==0x77)&&(header[1]=0x0b))||((header[0]==0x0b)&&(header[1]==0x77))){
		ALOGI("Is ddp_dolby file\n");
		*mimeType = MEDIA_MIMETYPE_CONTAINER_DDP;
		*confidence = 0.3f;
		return true;
	}
	ALOGI("not ddp_dolby file\n");
    return false;
}

}  // namespace android

