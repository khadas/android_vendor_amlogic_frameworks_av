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

//#define LOG_NDEBUG 0
#define LOG_TAG "HVCCFormatter"
#include <utils/Log.h>

#include "formatters/HVCCFormatter.h"

extern "C" {
#include <libavcodec/h2645_parse.h>
#include <libavcodec/hevc.h>
#include <libavcodec/hevcdec.h>
#undef CodecType
}

#include <media/stagefright/foundation/ColorUtils.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/Utils.h>


namespace android {

HVCCFormatter::HVCCFormatter(AVCodecContext* codec)
    : mHVCCFound(false),
      mHVCCSkip(false),
      mHVCC(NULL),
      mHVCCSize(0),
      mCheckHDR(0),
      mNALLengthSize(0) {
    parseCodecExtraData(codec);
}

HVCCFormatter::~HVCCFormatter() {
    delete[] mHVCC;
}

bool HVCCFormatter::parseCodecExtraData(AVCodecContext* codec) {
    if (NULL == codec) {
        ALOGE("NULL codec context passed to %s", __FUNCTION__);
        return false;
    }

    if (codec->extradata_size < 22) {
        ALOGE("HVCC cannot be smaller than 7.");
        return false;
    }

    if (codec->extradata[0] != 1u) {
        ALOGE("We only support configurationVersion 1, but this is %u.",codec->extradata[0]);
        mHVCCSkip = true;
        //return false;
    }

    mHVCC = new uint8_t[codec->extradata_size];
    mHVCCSize = static_cast<uint32_t>(codec->extradata_size);
    memcpy(mHVCC, codec->extradata, mHVCCSize);

    mNALLengthSize = 1 + (mHVCC[14 + 7] & 3);
    mHVCCFound = true;
    return true;
}

bool HVCCFormatter::addCodecMeta(const sp<MetaData> &meta) const {
    if (!mHVCCFound) {
        ALOGE("HVCC header has not been set.");
        return false;
    }
    if (mHVCCSkip) {
        ALOGE("HVCC header is not configurationVersion 1, skip setData kKeyHVCC.");
        return false;
    }
    meta->setData(kKeyHVCC, kTypeHVCC, mHVCC, mHVCCSize);
    return true;
}

size_t HVCCFormatter::parseNALSize(const uint8_t *data) const {
    switch (mNALLengthSize) {
        case 1:
            return *data;
        case 2:
            return U16_AT(data);
        case 3:
            return ((size_t)data[0] << 16) | U16_AT(&data[1]);
        case 4:
            return U32_AT(data);
    }

    // This cannot happen, mNALLengthSize springs to life by adding 1 to
    // a 2-bit integer.
    CHECK(!"Invalid NAL length size.");

    return 0;
}

uint32_t HVCCFormatter::computeNewESLen(
        const uint8_t* in, uint32_t inAllocLen) const {
    if (!mHVCCFound) {
        ALOGE("Can not compute new payload length, have not found an HVCC "
                "header yet.");
        return 0;
    }
    size_t srcOffset = 0;
    size_t dstOffset = 0;
    const size_t packetSize = static_cast<size_t>(inAllocLen);

    while (srcOffset < packetSize) {
        CHECK_LE(srcOffset + mNALLengthSize, packetSize);
        size_t nalLength = parseNALSize(&in[srcOffset]);
        srcOffset += mNALLengthSize;

        if (srcOffset + nalLength > packetSize) {
            ALOGE("Invalid nalLength (%zu) or packet size(%zu).",
                    nalLength, packetSize);
            return 0;
        }

        if (nalLength == 0) {
            continue;
        }

        dstOffset += 4 + nalLength;
        srcOffset += nalLength;
    }
    CHECK_EQ(srcOffset, packetSize);
    return dstOffset;
}

int32_t HVCCFormatter::formatES(
      const uint8_t* in, uint32_t inAllocLen, uint8_t* out,
      uint32_t outAllocLen) const {
    CHECK(in != out);
    if (!mHVCCFound) {
        ALOGE("HVCC header has not been set.");
        return -1;
    }

    size_t srcOffset = 0;
    size_t dstOffset = 0;
    const size_t packetSize = static_cast<size_t>(inAllocLen);

    while (srcOffset < packetSize) {
        CHECK_LE(srcOffset + mNALLengthSize, packetSize);
        size_t nalLength = parseNALSize(&in[srcOffset]);
        srcOffset += mNALLengthSize;

        if (srcOffset + nalLength > packetSize) {
            ALOGE("Invalid nalLength (%zu) or packet size(%zu).",
                    nalLength, packetSize);
            return -1;
        }

        if (nalLength == 0) {
            continue;
        }

        CHECK(dstOffset + 4 + nalLength <= outAllocLen);

        static const uint8_t kNALStartCode[4] =  { 0x00, 0x00, 0x00, 0x01 };
        memcpy(out + dstOffset, kNALStartCode, 4);
        dstOffset += 4;

        memcpy(&out[dstOffset], &in[srcOffset], nalLength);
        srcOffset += nalLength;
        dstOffset += nalLength;
    }
    CHECK_EQ(srcOffset, packetSize);

    return dstOffset;
}

int32_t HVCCFormatter::getMetaFromES(
    const uint8_t* data, uint32_t size, const sp<MetaData> &meta) {
    //check 2 package to find HDR
    if (mCheckHDR < 2)  {
        int ret = 0;
        H2645Packet pkt = { 0 };

        ret = ff_h2645_packet_split(&pkt, data,size, NULL, 0, 0, AV_CODEC_ID_HEVC, 1);
        if (ret < 0) {
            goto Done;
        }

        for (int i = 0; i < pkt.nb_nals; i++) {
            H2645NAL *nal = &pkt.nals[i];
            if (nal->type == HEVC_NAL_SEI_PREFIX) {
                AVCodecContext avctx;
                HEVCContext hc;
                HEVCLocalContext HEVClc;

                memset(&avctx, 0, sizeof(avctx));
                memset(&hc, 0, sizeof(hc));
                memset(&HEVClc, 0, sizeof(HEVClc));

                HEVClc.gb = nal->gb;
                hc.HEVClc = &HEVClc;
                hc.avctx = &avctx;
                hc.nal_unit_type = (enum HEVCNALUnitType)nal->type;
                hc.temporal_id   = nal->temporal_id;
                ret = ff_hevc_decode_nal_sei(&hc);
                if (ret < 0)
                     goto Done;
                if (hc.sei_mastering_display_info_present == 2) {
                    //ALOGD("have AV_FRAME_DATA_MASTERING_DISPLAY_METADATA");

                    HDRStaticInfo info, nullInfo; // nullInfo is a fully unspecified static info
                    memset(&info, 0, sizeof(info));
                    memset(&nullInfo, 0, sizeof(nullInfo));
                    // 1. HEVC uses a g,b,r ordering
                    // 2. max_mastering_luminance and min_mastering_luminance are 32bit, use unuse
                    // mMaxContentLightLevel and mMaxFrameAverageLightLevel to high 16bit.
                    info.sType1.mMaxContentLightLevel = (hc.max_mastering_luminance >> 16) & 0xffff;
                    info.sType1.mMaxFrameAverageLightLevel = (hc.min_mastering_luminance >> 16) & 0xfff;
                    info.sType1.mMaxDisplayLuminance = hc.max_mastering_luminance & 0xffff;
                    info.sType1.mMinDisplayLuminance = hc.min_mastering_luminance & 0xffff;
                    info.sType1.mW.x = hc.white_point[0];
                    info.sType1.mW.y = hc.white_point[1];
                    info.sType1.mG.x = hc.display_primaries[0][0];
                    info.sType1.mG.y = hc.display_primaries[0][1];
                    info.sType1.mB.x = hc.display_primaries[1][0];
                    info.sType1.mB.y = hc.display_primaries[1][1];
                    info.sType1.mR.x = hc.display_primaries[2][0];
                    info.sType1.mR.y = hc.display_primaries[2][1];

                    // Only advertise static info if at least one of the groups have been specified.
                    if (memcmp(&info, &nullInfo, sizeof(info)) != 0) {
                        info.mID = HDRStaticInfo::kType1;
                        meta->setData(kKeyHdrStaticInfo, 'hdrS', &info, sizeof(info));
                        ALOGE("set  kKeyHdrStaticInfo w[%d, %d] r[%d %d] g[%d %d] b[%d %d], maxL:%d, minL:%d",
                            info.sType1.mW.x, info.sType1.mW.y,
                            info.sType1.mR.x, info.sType1.mR.y,
                            info.sType1.mG.x, info.sType1.mG.y,
                            info.sType1.mB.x, info.sType1.mB.x,
                            info.sType1.mMaxDisplayLuminance,
                            info.sType1.mMinDisplayLuminance);

                    }
                    hc.sei_mastering_display_info_present = 0;
               }
            }
        }
    Done:
        ff_h2645_packet_uninit(&pkt);
        mCheckHDR++;
    }
    return 0;
}
}  // namespace android
