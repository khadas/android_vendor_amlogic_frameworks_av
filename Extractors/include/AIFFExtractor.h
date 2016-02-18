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

#ifndef AIFF_EXTRACTOR_H_

#define AIFF_EXTRACTOR_H_

#include <utils/Errors.h>
#include <media/stagefright/MediaExtractor.h>

namespace android {

struct AMessage;
class DataSource;
class String8;

#define MKTAG(a,b,c,d) ((a) | ((b) << 8) | ((c) << 16) | ((unsigned)(d) << 24))
#define MKBETAG(a,b,c,d) ((d) | ((c) << 8) | ((b) << 16) | ((unsigned)(a) << 24))

enum AVCodecID {
    AV_CODEC_ID_NONE = 0x0000,
	AV_CODEC_ID_PCM_S16BE,
    AV_CODEC_ID_PCM_S8,
    AV_CODEC_ID_PCM_U8,
   	AV_CODEC_ID_PCM_S24BE,
    AV_CODEC_ID_PCM_S32BE,
    AV_CODEC_ID_PCM_F32BE,
    AV_CODEC_ID_PCM_F64BE,
    AV_CODEC_ID_PCM_ALAW,
    AV_CODEC_ID_PCM_MULAW,
    AV_CODEC_ID_MACE3,
    AV_CODEC_ID_MACE6,
    AV_CODEC_ID_GSM,
    AV_CODEC_ID_ADPCM_G726,
    AV_CODEC_ID_PCM_S16LE,
    AV_CODEC_ID_ADPCM_IMA_QT,
    AV_CODEC_ID_QDM2,
    AV_CODEC_ID_QCELP,
    AV_CODEC_ID_PCM_G711_ALAW,
    AV_CODEC_ID_PCM_G711_MULAW,
};

typedef struct AVCodecTag {
    enum AVCodecID id;
    unsigned int tag;
} AVCodecTag;

static const AVCodecTag ff_codec_aiff_tags[] = {
    { AV_CODEC_ID_PCM_S16BE,    MKTAG('N','O','N','E') },
    { AV_CODEC_ID_PCM_S8,       MKTAG('N','O','N','E') },
    { AV_CODEC_ID_PCM_U8,       MKTAG('r','a','w',' ') },
    { AV_CODEC_ID_PCM_S24BE,    MKTAG('N','O','N','E') },
    { AV_CODEC_ID_PCM_S32BE,    MKTAG('N','O','N','E') },
    { AV_CODEC_ID_PCM_F32BE,    MKTAG('f','l','3','2') },
    { AV_CODEC_ID_PCM_F64BE,    MKTAG('f','l','6','4') },
    { AV_CODEC_ID_PCM_ALAW,     MKTAG('a','l','a','w') },
    { AV_CODEC_ID_PCM_MULAW,    MKTAG('u','l','a','w') },
    { AV_CODEC_ID_PCM_S24BE,    MKTAG('i','n','2','4') },
    { AV_CODEC_ID_PCM_S32BE,    MKTAG('i','n','3','2') },
    { AV_CODEC_ID_MACE3,        MKTAG('M','A','C','3') },
    { AV_CODEC_ID_MACE6,        MKTAG('M','A','C','6') },
    { AV_CODEC_ID_GSM,          MKTAG('G','S','M',' ') },
    { AV_CODEC_ID_ADPCM_G726,   MKTAG('G','7','2','6') },
    { AV_CODEC_ID_PCM_S16BE,    MKTAG('t','w','o','s') },
    { AV_CODEC_ID_PCM_S16LE,    MKTAG('s','o','w','t') },
    { AV_CODEC_ID_ADPCM_IMA_QT, MKTAG('i','m','a','4') },
    { AV_CODEC_ID_QDM2,         MKTAG('Q','D','M','2') },
    { AV_CODEC_ID_QCELP,        MKTAG('Q','c','l','p') },
    { AV_CODEC_ID_QDM2,         MKTAG('Q','D','M','2') },
    { AV_CODEC_ID_QCELP,        MKTAG('Q','c','l','p') },
    { AV_CODEC_ID_PCM_G711_ALAW,  MKTAG('A','L','A','W') },
    { AV_CODEC_ID_PCM_G711_MULAW, MKTAG('U','L','A','W') },
    { AV_CODEC_ID_NONE,         0 },
};

class AIFFExtractor : public MediaExtractor {
	
public:
    // Extractor assumes ownership of "source".
    AIFFExtractor(const sp<DataSource> &source);

    virtual size_t countTracks();
    virtual sp<MediaSource> getTrack(size_t index);
    virtual sp<MetaData> getTrackMetaData(size_t index, uint32_t flags);
    virtual sp<MetaData> getMetaData();

protected:
    virtual ~AIFFExtractor();

private:
    sp<DataSource> mDataSource;
    sp<MetaData> mTrackMeta;
	
    status_t mInitCheck;
	
    uint16_t mNumChannels;
	uint32_t mNumSampleFrame;
	uint16_t mBitsPerSample;	
	uint32_t mSampleRate;

	uint16_t mWaveFormat;
	uint32_t mBlockAlign;
	uint32_t mbit_rate;
		
	bool	 mValidFormat;
	
    off64_t mDataOffset;
    size_t  mDataSize;
	
	status_t init();
	
	//----------------------------------------------------
    AIFFExtractor(const AIFFExtractor &);
    AIFFExtractor &operator=(const AIFFExtractor &);
};

bool SniffAIFF(const sp<DataSource> &source, String8 *mimeType, float *confidence, sp<AMessage> *);

}  // namespace android

#endif  // WAV_EXTRACTOR_H_

