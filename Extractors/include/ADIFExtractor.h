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

#ifndef ADIF_EXTRACTOR_H_

#define ADIF_EXTRACTOR_H_

#include <utils/Errors.h>
#include <media/stagefright/MediaExtractor.h>

#include "../codecs/adif/dec/include/neaacdec.h"
#include <utils/Vector.h>

namespace android {

struct AMessage;
class DataSource;
class String8;

class ADIFExtractor : public MediaExtractor {
public:
    ADIFExtractor(const sp<DataSource> &source);

    virtual size_t countTracks();
    virtual sp<MediaSource> getTrack(size_t index);
    virtual sp<MetaData> getTrackMetaData(size_t index, uint32_t flags);

    virtual sp<MetaData> getMetaData();

protected:
    virtual ~ADIFExtractor();

private:
    sp<DataSource> mDataSource;
	status_t mInitCheck;
	uint16_t mAdifHead;
	uint16_t mNumChannels;
    uint32_t mSampleRate;
	int32_t mBitRate;
    uint16_t mBitsPerSample;
    off64_t mDataOffset;
	size_t mSize;
    size_t mDataSize;
    sp<MetaData> mTrackMeta;
    Vector<uint64_t> mOffsetVector;
    status_t init_adif();

    ADIFExtractor(const ADIFExtractor &);
    ADIFExtractor &operator=(const ADIFExtractor &);
};

bool SniffADIF(
        const sp<DataSource> &source, String8 *mimeType, float *confidence,
        sp<AMessage> *);

}  // namespace android

#endif  // ADIF_EXTRACTOR_H_
