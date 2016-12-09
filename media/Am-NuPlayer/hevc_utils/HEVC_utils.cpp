/*
 * Copyright (C) 2010 The Android Open Source Project
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
#define LOG_TAG "HEVC_utils"
#include <utils/Log.h>

#include "include/HEVC_utils.h"

#include <media/stagefright/foundation/ABitReader.h>
#include <media/stagefright/MediaErrors.h>

extern "C" {
#ifndef UINT64_C
#define  UINT64_C(x) (x)
#endif

#include "libavutil/atomic.h"
#include "libavutil/attributes.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/md5.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include <libavcodec/hevc.h>
#include <libavcodec/avcodec.h>

}
namespace android {

status_t HEVC_getNextNALUnit(
        const uint8_t **_data, size_t *_size,
        const uint8_t **nalStart, size_t *nalSize,
        bool startCodeFollows) {
    const uint8_t *data = *_data;
    size_t size = *_size;

    *nalStart = NULL;
    *nalSize = 0;

    if (size == 0) {
        return -EAGAIN;
    }

    // Skip any number of leading 0x00.

    size_t offset = 0;
    while (offset < size && data[offset] == 0x00) {
        ++offset;
    }

    if (offset == size) {
        return -EAGAIN;
    }

    // A valid startcode consists of at least two 0x00 bytes followed by 0x01.

    if (offset < 2 || data[offset] != 0x01) {
        return ERROR_MALFORMED;
    }

    ++offset;

    size_t startOffset = offset;

    for (;;) {
        while (offset < size && data[offset] != 0x01) {
            ++offset;
        }

        if (offset == size) {
            if (startCodeFollows) {
                offset = size + 2;
                break;
            }

            return -EAGAIN;
        }

        if (data[offset - 1] == 0x00 && data[offset - 2] == 0x00) {
            break;
        }

        ++offset;
    }

    size_t endOffset = offset - 2;
    while (endOffset > startOffset + 1 && data[endOffset - 1] == 0x00) {
        --endOffset;
    }

    *nalStart = &data[startOffset];
    *nalSize = endOffset - startOffset;

    if (offset + 2 < size) {
        *_data = &data[offset - 2];
        *_size = size - offset + 2;
    } else {
        *_data = NULL;
        *_size = 0;
    }

    return OK;
}

sp<ABuffer> HEVC_FindNAL(
        const uint8_t *data, size_t size, unsigned nalType,
        size_t *stopOffset) {
    const uint8_t *nalStart;
    size_t nalSize;
    while (HEVC_getNextNALUnit(&data, &size, &nalStart, &nalSize, true) == OK) {
		ALOGE("HEVC_FindNAL sps failed nal type =%d\n",((nalStart[0]>>1) & 0x3f));
        if (((nalStart[0]>>1) & 0x3f) == nalType) {
            sp<ABuffer> buffer = new ABuffer(nalSize);
            memcpy(buffer->data(), nalStart, nalSize);
            return buffer;
        }
    }

    return NULL;
}

int HEVC_decode_SPS(const uint8_t *buf,int size,struct hevc_info*info)
{
	HEVCContext s;
	AVCodecContext avctx;
	HEVCLocalContext HEVClc;
	memset(&s,0,sizeof(s));
	memset(&avctx,0,sizeof(avctx));
	memset(&HEVClc,0,sizeof(HEVClc));
	s.avctx=&avctx;
	s.HEVClc=&HEVClc;
	HEVCNAL nal;
	memset(&nal,0,sizeof(nal));
	ff_hevc_extract_rbsp(&s,buf,size,&nal);
	init_get_bits8(&s.HEVClc->gb,nal.data, nal.size);
	int err=ff_hevc_decode_nal_sps(&s);

	if (s.sps_list[0])
	{
		HEVCSPS *sps = (HEVCSPS*)s.sps_list[0]->data;
		ALOGI("ff_hevc_decode_nal_sps= %d,%d\n",sps->width,sps->height);
		info->mwidth=sps->width;
		info->mheight=sps->height;
	}else{
		return -1;
	}
	return 0;
}

int HEVC_parse_keyframe(const uint8_t *buf,int size) {
    avcodec_register_all();
    AVCodecParserContext *parser = av_parser_init(AV_CODEC_ID_HEVC);
    if (!parser) {
        ALOGE("Couldn't get hevc parser!\n");
        return -1;
    }
    AVPacket out_pkt = { 0 };
    parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;
    AVCodec * codec= avcodec_find_decoder(AV_CODEC_ID_HEVC);
    AVCodecContext * ctx = avcodec_alloc_context3(codec);
    int tmp = av_parser_parse2(parser, ctx, &out_pkt.data, &out_pkt.size, buf, size, 0, 0, 0);
    int keyframe = parser->key_frame;
    av_parser_close(parser);
    avcodec_close(ctx);
    return keyframe;
}

// extract vps/sps/pps from input packet.
// simple process, maybe have special case.
int32_t HEVCCastSpecificData(uint8_t * data, int32_t size) {
    if (size < 4) {
        return -1;
    }

    int32_t rsize = 0;
    int32_t ps_count = 0;
    while (rsize < size) {
        if (data[rsize] == 0 && data[rsize+1] == 0 && data[rsize+2] == 0 && data[rsize+3] == 1) {
	     if (ps_count == 3) {
	         break;
	     }
            rsize += 3;
            ps_count++;
        }
        rsize++;
    }

    if (ps_count != 3) {
        return -1;
    }

    return rsize;
}

}

