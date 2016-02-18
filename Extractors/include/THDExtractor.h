/***************************************************
 **       dolby truehd demux
 **       Copyright (C) by Amlogic INC.
 ***************************************************/

#ifndef THD_EXTRACTOR_H_
#define THD_EXTRACTOR_H_

#include <utils/Errors.h>
#include <media/stagefright/MediaExtractor.h>
#include <utils/Vector.h>

#define FORMATSYNC_FBB      0xf8726fbb
#define FORMATSYNC_FBA      0xf8726fba


namespace android {

struct AMessage;
class DataSource;
class String8;

class THDExtractor : public MediaExtractor {
public:
    // Extractor assumes ownership of "source".
    THDExtractor(const sp<DataSource> &source);

    virtual size_t countTracks();
    virtual sp<MediaSource> getTrack(size_t index);
    virtual sp<MetaData> getTrackMetaData(size_t index, uint32_t flags);

    virtual sp<MetaData> getMetaData();

protected:
    virtual ~THDExtractor();

private:
    sp<DataSource> mDataSource;
    status_t mInitCheck;
    uint16_t mNumChannels;
    uint32_t mSampleRate;
    uint16_t mBitsPerSample;
    off64_t mDataOffset;
    size_t mDataSize;
    sp<MetaData> mTrackMeta;

	Vector<uint64_t> mOffsetVector;
	int64_t mFrameDurationUs;

    status_t init();

    THDExtractor(const THDExtractor &);
    THDExtractor &operator=(const THDExtractor &);
};

bool SniffTHD(
        const sp<DataSource> &source, String8 *mimeType, float *confidence,
        sp<AMessage> *);

} 
#endif  

