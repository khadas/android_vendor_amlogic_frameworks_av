/******************************************************
 **               dolby truehd demux
 *******************************************************/

#define LOG_NDEBUG 0
#define LOG_TAG "THDExtractor"
#include <utils/Log.h>

#include "include/THDExtractor.h"

#include <media/stagefright/DataSource.h>
#include <media/stagefright/MediaBufferGroup.h>
#include <media/stagefright/foundation/ADebug.h>

#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MetaData.h>
#include <utils/String8.h>


namespace android {

struct THDSource : public MediaSource {
    THDSource(
            const sp<DataSource> &dataSource,
            const sp<MetaData> &meta,
            const Vector<uint64_t> &offset_vector,
            int64_t frame_duration_us);

    virtual status_t start(MetaData *params = NULL);
    virtual status_t stop();
    virtual sp<MetaData> getFormat();

    virtual status_t read(
            MediaBuffer **buffer, const ReadOptions *options = NULL);

protected:
    virtual ~THDSource();

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
	int64_t mFrameDurationUs;

    THDSource(const THDSource &);
    THDSource &operator=(const THDSource &);
};

THDExtractor::THDExtractor(const sp<DataSource> &source)
    :mDataSource(source){
    mInitCheck = init();
}

THDExtractor::~THDExtractor() {
}


sp<MetaData> THDExtractor::getMetaData() {
    sp<MetaData> meta = new MetaData;

    if (mInitCheck != OK) {
        return meta;
    }

    meta->setCString(kKeyMIMEType, "audio/truehd");

    return meta;
}

size_t THDExtractor::countTracks() {
    return mInitCheck == OK ? 1 : 0;
}

sp<MediaSource> THDExtractor::getTrack(size_t index) {
    if (mInitCheck != OK || index > 0) {
        return NULL;
    }

    return new THDSource(mDataSource, mTrackMeta, mOffsetVector, mFrameDurationUs);
}

sp<MetaData> THDExtractor::getTrackMetaData(
        size_t index, uint32_t flags) {
    if (mInitCheck != OK || index > 0) {
        return NULL;
    }

    return mTrackMeta;
}

status_t THDExtractor::init() {
   
    off64_t streamSize = 0; 
	off64_t offset = 0;
	int numframes = 0;
    if(mDataSource->getSize(&streamSize) == OK){
        if(streamSize < 16){
			ALOGI("streamSize is not enough\n");
			return NO_INIT;
		}
	}else{
	    ALOGI("getSize failed\n");
        return NO_INIT;
	}

	mTrackMeta = new MetaData;

	char ptr[16] = {'0'};
	uint32_t sync_read = 0;
	uint32_t au_length = 0;
	char index = 0;
	int sample_rate = 0;
	int lock = 0;
	int one_samples = 0; 
	int total_samples = 0;
	
	while(offset < streamSize){
		if(lock == 0){
			if(mDataSource->readAt(offset, ptr, 16) < 16){
                ALOGI("reat data at %ld failed.\n", offset);
			    return NO_INIT;
		    }
			sync_read = (ptr[4] << 24) | (ptr[5] << 16) | (ptr[6] << 8) | ptr[7];
			if(FORMATSYNC_FBA == sync_read){
				ALOGI("main sync ok\n");
				au_length = ((((ptr[0] << 8) | ptr[1]) & 0x0fff)<<1);
				if(au_length < 6 || au_length > 4000){
                    ALOGI("au length is failed. \n");
					return NO_INIT;
				}
				ALOGI("au_length = %u \n", au_length);
				
				index = (ptr[8] >> 4) & 0xf;
				if(index == 0xf){
                    ALOGI("sample rate parse failed.\n");
					return NO_INIT;
				}else{
                    sample_rate = ((index & 8 ? 44100 : 48000) << (index & 7));
					one_samples = (40 << (index & 7));
					total_samples ++;
				}

				
				mOffsetVector.push(offset);
                lock = 1;
				offset += au_length;
			}else{
                ALOGI("find main sync failed.\n");
				return NO_INIT;
			}
		}else {
		    if(mDataSource->readAt(offset, ptr, 4) < 4){
                ALOGI("reat data at %ld failed.\n", offset);
			    return NO_INIT;
		    }
			au_length = ((((ptr[0] << 8) | ptr[1]) & 0x0fff) << 1);
			if(au_length < 6 || au_length > 4000){
                ALOGI("au length is failed. \n");
				return NO_INIT;
			}
			ALOGI("au_length = %u \n", au_length);
			total_samples ++;
			mOffsetVector.push(offset);
			offset += au_length;
		}

		
	}

	ALOGI("total = %d, sr = %d\n", total_samples, sample_rate);
	int64_t duration = 0;
	if(sample_rate != 0){
	   mFrameDurationUs = (one_samples * 1000000ll + (sample_rate - 1)) / sample_rate;
	   duration = total_samples * mFrameDurationUs;
	   ALOGI("mFrameDurationUs = %lld, duration = %lld\n", mFrameDurationUs, duration);
	}else{
       ALOGI("sample rate equals 0\n");
	   return NO_INIT;
	}
	
	mTrackMeta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_TRUEHD);
    mTrackMeta->setInt64(kKeyDuration, duration);
	mTrackMeta->setInt32(kKeyChannelCount, 2);
    mTrackMeta->setInt32(kKeySampleRate, sample_rate);
	

    return OK;
}

const size_t THDSource::kMaxFrameSize = 4000;
THDSource::THDSource(
        const sp<DataSource> &dataSource,
        const sp<MetaData> &meta,
		const Vector<uint64_t> &offset_vector,
        int64_t frame_duration_us)
    : mDataSource(dataSource),
      mMeta(meta),
      mOffset(0),
      mCurrentTimeUs(0),
      mStarted(false),
      mGroup(NULL),
      mOffsetVector(offset_vector),
      mFrameDurationUs(frame_duration_us) {
}

THDSource::~THDSource() {
    if (mStarted) {
        stop();
    }
}

status_t THDSource::start(MetaData *params) {
    ALOGI("THDSource::start");

    CHECK(!mStarted);

	if(mOffsetVector.empty()){
        mOffset = 0;
	}else {
        mOffset = mOffsetVector.itemAt(0);
	}
    mCurrentTimeUs = 0;
    mGroup = new MediaBufferGroup;
    mGroup->add_buffer(new MediaBuffer(kMaxFrameSize));
    mStarted = true;

    return OK;
}

status_t THDSource::stop() {
    ALOGI("THDSource::stop");

    CHECK(mStarted);

    delete mGroup;
    mGroup = NULL;

    mStarted = false;

    return OK;
}

sp<MetaData> THDSource::getFormat() {
    ALOGI("THDSource::getFormat");
    return mMeta;
}

status_t THDSource::read(
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

	if(mOffset >= (mOffsetVector.size() - 1))
		return ERROR_END_OF_STREAM;
	
    MediaBuffer *buffer;
    status_t err = mGroup->acquire_buffer(&buffer);
    if (err != OK) {
        return err;
    }


			
	ssize_t maxBytesToRead = mOffsetVector.itemAt(mOffset + 1) - mOffsetVector.itemAt(mOffset);
	ssize_t n = mDataSource->readAt(
					mOffsetVector.itemAt(mOffset), buffer->data(),
					maxBytesToRead);
	
	if (n != maxBytesToRead) {
		buffer->release();
		buffer = NULL;
	
		return ERROR_END_OF_STREAM;
	}
	
	
	mOffset += 1;
	buffer->set_range(0, n);
	buffer->meta_data()->setInt64(kKeyTime, mCurrentTimeUs);
	buffer->meta_data()->setInt32(kKeyIsSyncFrame, 1);

	mCurrentTimeUs += mFrameDurationUs;
		

	
    *out = buffer;

    return OK;
}

////////////////////////////////////////////////////////////////////////////////

bool SniffTHD(
        const sp<DataSource> &source, String8 *mimeType, float *confidence,
        sp<AMessage> *) {
    char header[16];
    if (source->readAt(0, header, sizeof(header)) < (ssize_t)sizeof(header)) {
        return false;
    }
	uint32_t sync_read = 0;
	sync_read = (header[4] << 24) | (header[5] << 16) | (header[6] << 8) | header[7];
	if((FORMATSYNC_FBA == sync_read) && (((header[12]<<8)|header[13]) == 0xB752)){
		ALOGI("Is mlp for truehd file\n");
		*mimeType = MEDIA_MIMETYPE_AUDIO_TRUEHD;
		*confidence = 0.3f;
		return true;
	}
	ALOGI("not Is mlp for truehd file\n");
    return false;
}

}  // namespace android

