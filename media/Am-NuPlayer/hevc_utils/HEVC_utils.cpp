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

#include "HEVC_utils.h"
#include <dlfcn.h>
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
#include <libavcodec/hevcdec.h>
#include <libavcodec/hevc_ps.h>
#include <libavcodec/avcodec.h>

}
namespace android {

typedef int HevcDecodeNalSpsFunc(GetBitContext *gb, AVCodecContext *avctx, HEVCParamSets *ps, int apply_defdispwin);
typedef int HevcExtractRbspFunc(const uint8_t *src, int length, H2645NAL *nal, int small_padding);


static HevcDecodeNalSpsFunc* decodeNalSps = NULL;
static HevcExtractRbspFunc* extractRbsp = NULL;

static int HEVC_initFunc()
{
    void * mLibHandle = dlopen("libamffmpeg.so", RTLD_NOW);

    if (mLibHandle == NULL) {
        ALOGE("Unable to locate libamffmpeg.so\n");
        return -1;
    }
    ALOGI("get HEVC func\n");

    decodeNalSps = (HevcDecodeNalSpsFunc*)dlsym(mLibHandle, "ff_hevc_decode_nal_sps");
    extractRbsp = (HevcExtractRbspFunc*)dlsym(mLibHandle, "ff_h2645_extract_rbsp");

    if (decodeNalSps == NULL  || extractRbsp == NULL)
        return -1;
    return 0;
}


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
    HEVCParamSets ps;
    HEVCVPS VPS;
    H2645NAL nal;

    if (decodeNalSps == NULL) {
        int ret = HEVC_initFunc();
        if (ret < 0) {
            return -1;
        }
    }

    memset(&s,0,sizeof(s));
    memset(&avctx,0,sizeof(avctx));
    memset(&HEVClc,0,sizeof(HEVClc));
    memset(&ps,0,sizeof(ps));

    ps.vps_list[0] = (AVBufferRef *)&VPS;

    s.avctx=&avctx;
    s.HEVClc=&HEVClc;
    memset(&nal,0,sizeof(nal));
    extractRbsp(buf, size, &nal, 1);
    init_get_bits8(&HEVClc.gb,nal.data, nal.size);
    decodeNalSps(&HEVClc.gb, &avctx, &ps, 1);
    if (ps.sps_list[0])
    {
        HEVCSPS *sps = (HEVCSPS*)ps.sps_list[0]->data;
        ALOGI("ff_hevc_decode_nal_sps= %d,%d\n",sps->width,sps->height);
        info->mwidth=sps->width;
        info->mheight=sps->height;
    }else{
        return -1;
    }
    return 0;
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

