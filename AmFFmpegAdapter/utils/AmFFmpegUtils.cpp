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

#define LOG_NDEBUG 0
#define LOG_TAG "AmFFmpegUtils"
#include <utils/Log.h>

#include "AmFFmpegUtils.h"

extern "C" {
#include <libavformat/avio.h>
#include <libavformat/avformat.h>
#undef CodecType
}

#include <media/stagefright/AmMediaDefsExt.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MetaData.h>
#include <cutils/properties.h>
#include <utils/misc.h>
#include <utils/threads.h>

#include "AmFFmpegByteIOAdapter.h"

namespace android {

static int32_t kMicroSecPerSec = 1000000;

static const char* kDummyFileName = "dummy_file_name";
static const uint32_t kProbeStartBufSize = 4 * 1024;
static const uint32_t kProbeMaxBufSize = 512 * 1024;
static const uint8_t kCommonNALStartCode[4] = { 0x00, 0x00, 0x00, 0x01 };

struct KeyMap {
    const char *mime;
    AVCodecID key;
};
static const KeyMap kKeyMap[] = {
    { MEDIA_MIMETYPE_VIDEO_AVC, AV_CODEC_ID_H264 },
    { MEDIA_MIMETYPE_VIDEO_HEVC, AV_CODEC_ID_HEVC},
    { MEDIA_MIMETYPE_VIDEO_SORENSON_SPARK, AV_CODEC_ID_FLV1 },
    { MEDIA_MIMETYPE_VIDEO_H263, AV_CODEC_ID_H263 },
    { MEDIA_MIMETYPE_VIDEO_AVC, AV_CODEC_ID_H264 },
    { MEDIA_MIMETYPE_VIDEO_MJPEG, AV_CODEC_ID_MJPEG },
    // Setting MPEG2 mime type for MPEG1 video intentionally because
    // All standards-compliant MPEG-2 Video decoders are fully capable of
    // playing back MPEG-1 Video streams conforming to the CPB.
    { MEDIA_MIMETYPE_VIDEO_MPEG2, AV_CODEC_ID_MPEG1VIDEO },
    { MEDIA_MIMETYPE_VIDEO_MPEG2, AV_CODEC_ID_MPEG2VIDEO },
    { MEDIA_MIMETYPE_VIDEO_MPEG4, AV_CODEC_ID_MPEG4 },
    { MEDIA_MIMETYPE_VIDEO_MSMPEG4, AV_CODEC_ID_MSMPEG4V3 },
    { MEDIA_MIMETYPE_VIDEO_VC1, AV_CODEC_ID_VC1 },
    { MEDIA_MIMETYPE_VIDEO_VP6, AV_CODEC_ID_VP6 },
    { MEDIA_MIMETYPE_VIDEO_VP6A, AV_CODEC_ID_VP6A },
    { MEDIA_MIMETYPE_VIDEO_VP6F, AV_CODEC_ID_VP6F },
    { MEDIA_MIMETYPE_VIDEO_VPX, AV_CODEC_ID_VP8 },
    { MEDIA_MIMETYPE_VIDEO_VP9, AV_CODEC_ID_VP9 },
    { MEDIA_MIMETYPE_VIDEO_WMV3, AV_CODEC_ID_WMV3 /* WMV9 */ },
    { MEDIA_MIMETYPE_VIDEO_RM10, AV_CODEC_ID_RV10 },
    { MEDIA_MIMETYPE_VIDEO_RM20, AV_CODEC_ID_RV20 },
    { MEDIA_MIMETYPE_VIDEO_RM40, AV_CODEC_ID_RV40 },
    { MEDIA_MIMETYPE_VIDEO_WMV2, AV_CODEC_ID_WMV2 },
    { MEDIA_MIMETYPE_VIDEO_WMV1, AV_CODEC_ID_WMV1 },

    { MEDIA_MIMETYPE_AUDIO_AAC, AV_CODEC_ID_AAC },
    { MEDIA_MIMETYPE_AUDIO_AC3, AV_CODEC_ID_AC3 },
    { MEDIA_MIMETYPE_AUDIO_EC3, AV_CODEC_ID_EAC3 },
    // TODO: check if AMR works fine once decoder supports it.
    { MEDIA_MIMETYPE_AUDIO_AMR_NB, AV_CODEC_ID_AMR_NB },
    { MEDIA_MIMETYPE_AUDIO_AMR_WB, AV_CODEC_ID_AMR_WB },
    { MEDIA_MIMETYPE_AUDIO_DTS, AV_CODEC_ID_DTS },
    { MEDIA_MIMETYPE_AUDIO_G711_ALAW, AV_CODEC_ID_PCM_ALAW },
    { MEDIA_MIMETYPE_AUDIO_G711_MLAW, AV_CODEC_ID_PCM_MULAW },
    { MEDIA_MIMETYPE_AUDIO_MPEG_LAYER_I, AV_CODEC_ID_MP1 },
    { MEDIA_MIMETYPE_AUDIO_MPEG_LAYER_II, AV_CODEC_ID_MP2 },
    { MEDIA_MIMETYPE_AUDIO_MPEG, AV_CODEC_ID_MP3 },
    // TODO: add more PCM codecs including A/MuLAW.
    { MEDIA_MIMETYPE_AUDIO_RAW, AV_CODEC_ID_PCM_S16LE },
    { MEDIA_MIMETYPE_AUDIO_RAW, AV_CODEC_ID_PCM_S16BE },
    { MEDIA_MIMETYPE_AUDIO_RAW, AV_CODEC_ID_PCM_U16LE },
    { MEDIA_MIMETYPE_AUDIO_RAW, AV_CODEC_ID_PCM_U16BE },
    { MEDIA_MIMETYPE_AUDIO_RAW, AV_CODEC_ID_PCM_S8 },
    { MEDIA_MIMETYPE_AUDIO_RAW, AV_CODEC_ID_PCM_U8 },
    { MEDIA_MIMETYPE_AUDIO_RAW, AV_CODEC_ID_PCM_S24LE },
    { MEDIA_MIMETYPE_AUDIO_RAW, AV_CODEC_ID_PCM_S24BE },
    { MEDIA_MIMETYPE_AUDIO_RAW, AV_CODEC_ID_PCM_U24LE },
    { MEDIA_MIMETYPE_AUDIO_RAW, AV_CODEC_ID_PCM_U24BE },
    { MEDIA_MIMETYPE_AUDIO_FFMPEG, AV_CODEC_ID_PCM_BLURAY },
    { MEDIA_MIMETYPE_AUDIO_VORBIS, AV_CODEC_ID_VORBIS },
    { MEDIA_MIMETYPE_AUDIO_WMA, AV_CODEC_ID_WMAV1 },
    { MEDIA_MIMETYPE_AUDIO_WMA, AV_CODEC_ID_WMAV2 },
    { MEDIA_MIMETYPE_AUDIO_WMA, AV_CODEC_ID_WMAVOICE },
    { MEDIA_MIMETYPE_AUDIO_WMAPRO, AV_CODEC_ID_WMAPRO },
    { MEDIA_MIMETYPE_AUDIO_WMA, AV_CODEC_ID_WMALOSSLESS },
    { MEDIA_MIMETYPE_AUDIO_APE, AV_CODEC_ID_APE},
    { MEDIA_MIMETYPE_AUDIO_TRUEHD, AV_CODEC_ID_TRUEHD},        
    { MEDIA_MIMETYPE_TEXT_3GPP, AV_CODEC_ID_MOV_TEXT },
    { MEDIA_MIMETYPE_AUDIO_FFMPEG, AV_CODEC_ID_COOK },
    { MEDIA_MIMETYPE_AUDIO_FFMPEG, AV_CODEC_ID_FLAC },
    { MEDIA_MIMETYPE_AUDIO_FFMPEG, AV_CODEC_ID_AAC_LATM },
    { MEDIA_MIMETYPE_AUDIO_FFMPEG, AV_CODEC_ID_ADPCM_MS},
    { MEDIA_MIMETYPE_AUDIO_FFMPEG, AV_CODEC_ID_ADPCM_IMA_WAV},

    //add for subtitle
    { "subtitle/dvd",           AV_CODEC_ID_DVD_SUBTITLE },
    { "subtitle/dvb",           AV_CODEC_ID_DVB_SUBTITLE },
    { "subtitle/text",          AV_CODEC_ID_TEXT },
    { "subtitle/xsub",          AV_CODEC_ID_XSUB },
    { "subtitle/ssa",           AV_CODEC_ID_SSA },
    { "subtitle/pgs",           AV_CODEC_ID_HDMV_PGS_SUBTITLE },
    { "subtitle/teletext",      AV_CODEC_ID_DVB_TELETEXT },
    { "subtitle/srt",           AV_CODEC_ID_SRT },
    { "subtitle/microdvd",      AV_CODEC_ID_MICRODVD },
    { "subtitle/eia608",        AV_CODEC_ID_EIA_608 },
    { "subtitle/jacosub",       AV_CODEC_ID_JACOSUB },
    { "subtitle/sami",          AV_CODEC_ID_SAMI },
    { "subtitle/realtext",      AV_CODEC_ID_REALTEXT },
    { "subtitle/subviewer",     AV_CODEC_ID_SUBVIEWER },
    { "subtitle/subrip",        AV_CODEC_ID_SUBRIP },
    { "subtitle/webvtt",        AV_CODEC_ID_WEBVTT },
    { "subtitle/ssa",           AV_CODEC_ID_ASS },
    { "subtitle/stl",           AV_CODEC_ID_STL },
    { "subtitle/subviewer1",    AV_CODEC_ID_SUBVIEWER1 },
    { "subtitle/mpl2",          AV_CODEC_ID_MPL2 },
    { "subtitle/vplayer",       AV_CODEC_ID_VPLAYER },
    { "subtitle/pjs",           AV_CODEC_ID_PJS },
    { "subtitle/hdmv_text_subtitle",    AV_CODEC_ID_HDMV_TEXT_SUBTITLE },
};

static const size_t kNumEntries = NELEM(kKeyMap);

AVInputFormat *probeFormat(const sp<DataSource> &source) {
    AVInputFormat *format = NULL;
    
    for (uint32_t bufSize = kProbeStartBufSize;
            bufSize <= kProbeMaxBufSize; bufSize *= 2) {
        // TODO: use av_probe_input_buffer() once we upgrade FFmpeg library
        //       instead of having a loop here.
        AVProbeData probe_data;
        probe_data.filename = kDummyFileName;
        probe_data.buf = new unsigned char[bufSize+FF_INPUT_BUFFER_PADDING_SIZE];
        //probe_data.s = NULL;
        if (NULL == probe_data.buf) {
            ALOGE("failed to allocate memory for probing file format.");
            return NULL;
        }
        int32_t amountRead = source->readAt(0, probe_data.buf, bufSize);
        probe_data.buf_size = amountRead;

        int32_t score = 0;
        format = av_probe_input_format2(&probe_data, 1, &score);
        delete[] probe_data.buf;

        if (format != NULL && score > AVPROBE_SCORE_MAX / 4) {
            break;
        }
    }
    return format;
}

AVFormatContext* openAVFormatContext(
        AVInputFormat *inputFormat, AmFFmpegByteIOAdapter *adapter) {
    CHECK(inputFormat != NULL);
    CHECK(adapter != NULL);
    AVFormatContext* context = avformat_alloc_context();
    if (context == NULL) {
        ALOGE("Failed to allocate AVFormatContext.");
        return NULL;
    }

    context->pb = adapter->getContext();
    int res = avformat_open_input(
            &context,
            kDummyFileName,  // need to pass a filename
            inputFormat,  // probe the container format.
            NULL);  // no special parameters
    if (res < 0) {
        ALOGE("Failed to open the input stream.");
        avformat_free_context(context);
        return NULL;
    }

    res = avformat_find_stream_info(context, NULL);
    if (res < 0 && strcmp(inputFormat->name, "hevc")) {
        ALOGE("Failed to find stream information.");
        avformat_close_input(&context);
        return NULL;
    }
    return context;
}

const char *convertCodecIdToMimeType(AVCodecContext *codec) {
    CHECK(codec);

    for (size_t i = 0; i < kNumEntries; ++i) {
        if (kKeyMap[i].key == codec->codec_id) {
            return kKeyMap[i].mime;
        }
    }
    ALOGW("Unsupported codec id : %u", codec->codec_id);
    return NULL;
}

int32_t convertMimeTypetoCodecId(const char *mime) {
    CHECK(mime);

    for (size_t i = 0; i < kNumEntries; ++i) {
        if (!strcmp(kKeyMap[i].mime, mime)) {
            return kKeyMap[i].key;
        }
    }
    ALOGW("Unsupported mime : %s", mime);
    return 0;
}

bool getPcmFormatFromCodecId(AVCodecContext *codec, int32_t *bitsPerSample,
        OMX_ENDIANTYPE *dataEndian, OMX_NUMERICALDATATYPE *dataSigned) {
    switch (codec->codec_id) {
        case AV_CODEC_ID_PCM_S16LE:
            *bitsPerSample = 16;
            *dataEndian = OMX_EndianLittle;
            *dataSigned = OMX_NumericalDataSigned;
            break;
        case AV_CODEC_ID_PCM_S16BE:
            *bitsPerSample = 16;
            *dataEndian = OMX_EndianBig;
            *dataSigned = OMX_NumericalDataSigned;
            break;
        case AV_CODEC_ID_PCM_U16LE:
            *bitsPerSample = 16;
            *dataEndian = OMX_EndianLittle;
            *dataSigned = OMX_NumericalDataUnsigned;
            break;
        case AV_CODEC_ID_PCM_U16BE:
            *bitsPerSample = 16;
            *dataEndian = OMX_EndianLittle;
            *dataSigned = OMX_NumericalDataUnsigned;
            break;
        case AV_CODEC_ID_PCM_S8:
            *bitsPerSample = 8;
            *dataEndian = OMX_EndianLittle;
            *dataSigned = OMX_NumericalDataSigned;
            break;
        case AV_CODEC_ID_PCM_U8:
            *bitsPerSample = 8;
            *dataEndian = OMX_EndianLittle;
            *dataSigned = OMX_NumericalDataUnsigned;
            break;
        case AV_CODEC_ID_PCM_MULAW:
        case AV_CODEC_ID_PCM_ALAW:
            *bitsPerSample = 8;
            *dataEndian = OMX_EndianLittle;
            *dataSigned = OMX_NumericalDataSigned;
            break;
        case AV_CODEC_ID_PCM_S24LE:
            *bitsPerSample = 24;
            *dataEndian = OMX_EndianLittle;
            *dataSigned = OMX_NumericalDataSigned;
            break;
        case AV_CODEC_ID_PCM_S24BE:
            *bitsPerSample = 24;
            *dataEndian = OMX_EndianBig;
            *dataSigned = OMX_NumericalDataSigned;
            break;
        case AV_CODEC_ID_PCM_U24LE:
            *bitsPerSample = 24;
            *dataEndian = OMX_EndianLittle;
            *dataSigned = OMX_NumericalDataUnsigned;
            break;
        case AV_CODEC_ID_PCM_U24BE:
            *bitsPerSample = 24;
            *dataEndian = OMX_EndianBig;
            *dataSigned = OMX_NumericalDataUnsigned;
            break;
        case AV_CODEC_ID_PCM_BLURAY:
            *bitsPerSample = codec->bits_per_coded_sample;
            *dataEndian = OMX_EndianBig;
            *dataSigned = OMX_NumericalDataSigned;
            break;
        case AV_CODEC_ID_COOK:
        case AV_CODEC_ID_FLAC:
            *bitsPerSample = 16;
            *dataEndian = OMX_EndianLittle;
            *dataSigned = OMX_NumericalDataUnsigned;
            break;
        default:
            return false;
    }
    return true;
}

static void initInputFormatToMimeTypeMap(
        KeyedVector<AVInputFormat *, const char *> &map) {
    map.add(av_find_input_format("mp4"), MEDIA_MIMETYPE_CONTAINER_MPEG4);
    map.add(av_find_input_format("asf"), MEDIA_MIMETYPE_CONTAINER_ASF);
    map.add(av_find_input_format("avi"), MEDIA_MIMETYPE_CONTAINER_AVI);
    map.add(av_find_input_format("mpegts"), MEDIA_MIMETYPE_CONTAINER_MPEG2TS);
    map.add(av_find_input_format("mpeg"), MEDIA_MIMETYPE_CONTAINER_MPEG2PS);
    map.add(av_find_input_format("flv"), MEDIA_MIMETYPE_CONTAINER_FLV);
    map.add(av_find_input_format("wav"), MEDIA_MIMETYPE_CONTAINER_WAV);
    //map.add(av_find_input_format("ogg"), MEDIA_MIMETYPE_CONTAINER_OGG);
    map.add(av_find_input_format("mp3"), MEDIA_MIMETYPE_AUDIO_MPEG);
    map.add(av_find_input_format("mp2"), MEDIA_MIMETYPE_AUDIO_MPEG_LAYER_II);
    map.add(av_find_input_format("matroska"), MEDIA_MIMETYPE_CONTAINER_MATROSKA);
    map.add(av_find_input_format("ac3"), MEDIA_MIMETYPE_AUDIO_AC3);
    map.add(av_find_input_format("eac3"), MEDIA_MIMETYPE_AUDIO_EAC3);
    map.add(av_find_input_format("dts"), MEDIA_MIMETYPE_AUDIO_DTS);
    map.add(av_find_input_format("hevc"), MEDIA_MIMETYPE_VIDEO_HEVC);
    map.add(av_find_input_format("ape"), MEDIA_MIMETYPE_AUDIO_APE);
    map.add(av_find_input_format("rm"), MEDIA_MIMETYPE_VIDEO_RM10);
    map.add(av_find_input_format("rm"), MEDIA_MIMETYPE_VIDEO_RM20);
    map.add(av_find_input_format("rm"), MEDIA_MIMETYPE_VIDEO_RM40);
    map.add(av_find_input_format("wmv1"), MEDIA_MIMETYPE_VIDEO_WMV1);
    map.add(av_find_input_format("aiff"), MEDIA_MIMETYPE_CONTAINER_AIFF);
}

const char *convertInputFormatToMimeType(AVInputFormat *inputFormat) {
    static Mutex lock;
    static KeyedVector<AVInputFormat *, const char *> inputFormatToMimeType;
    static bool initCheck = false;
    {
        Mutex::Autolock autoLock(lock);
        if (!initCheck) {
            initInputFormatToMimeTypeMap(inputFormatToMimeType);
            initCheck = true;
        }
    }
    ssize_t index = inputFormatToMimeType.indexOfKey(inputFormat);
    if (0 <= index) {
        return inputFormatToMimeType.valueAt(index);
    }
    return NULL;
}

int64_t convertTimeBaseToMicroSec(int64_t time) {
    return time * kMicroSecPerSec / AV_TIME_BASE;
}

int64_t convertMicroSecToTimeBase(int64_t time) {
    return time * AV_TIME_BASE / kMicroSecPerSec;
}

void addESDSFromCodecPrivate(
        const sp<MetaData> &meta,
        bool isAudio, const void *priv, size_t privSize) {
    // For detail structure of ES_Descriptor, please refer 14496-1 7.2.6.5
    static const uint8_t kStaticESDS[] = {
        0x03, 22,
        0x00, 0x00,     // ES_ID
        0x00,           // streamDependenceFlag, URL_Flag, OCRstreamFlag

        0x04, 17,
        0x40,           // ObjectTypeIndication
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,

        0x05,
        // CodecSpecificInfo (with size prefix) follows
    };

    // Make sure all sizes can be coded in a single byte.
    if (privSize + 22 - 2 < 128) {
    size_t esdsSize = sizeof(kStaticESDS) + privSize + 1;
    uint8_t *esds = new uint8_t[esdsSize];
    memcpy(esds, kStaticESDS, sizeof(kStaticESDS));
    uint8_t *ptr = esds + sizeof(kStaticESDS);
    *ptr++ = privSize;
    memcpy(ptr, priv, privSize);

    // Increment by codecPrivateSize less 2 bytes that are accounted for
    // already in lengths of 22/17
    esds[1] += privSize - 2;
    esds[6] += privSize - 2;

    // Set ObjectTypeIndication.
    esds[7] = isAudio ? 0x40   // Audio ISO/IEC 14496-3
                      : 0x20;  // Visual ISO/IEC 14496-2

    meta->setData(kKeyESDS, 0, esds, esdsSize);

    delete[] esds;
    esds = NULL;
    }
}

// extract vps/sps/pps from input packet.
// simple process, maybe have special case.
int32_t castHEVCSpecificData(uint8_t * data, int32_t size) {
    if(size < 4) {
        return -1;
    }

    int32_t rsize = 0;
    int32_t ps_count = 0;
    while(rsize < size) {
        if(data[rsize] == 0 && data[rsize+1] == 0 && data[rsize+2] == 0 && data[rsize+3] == 1) {
	     if(ps_count == 3) {
	         break;
	     }
            if(((data[rsize+4]>>1) & 0x3f) == 32
		|| ((data[rsize+4]>>1) & 0x3f) == 33
		|| ((data[rsize+4]>>1) & 0x3f) == 34) {
                ps_count++;
            }
            rsize += 3;
        }
        rsize++;
    }

    if(ps_count != 3) {
        return -1;
    }

    return rsize;
}

float AmPropGetFloat(const char* str, float def)
{ 
	char value[PROPERTY_VALUE_MAX];
	float ret=def;
	if(property_get(str, value, NULL)>0)
	{
		if ((sscanf(value,"%f",&ret))>0)
		{
			ALOGI("%s is set to %f\n",str, ret);
			return ret;
		}
	}
	ALOGI("%s is not set used def=%f\n",str, ret);
	return ret;
}

}  // namespace android
