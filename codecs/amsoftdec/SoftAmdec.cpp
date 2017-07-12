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
 *
 * coded by senbai.tao@amlogic.com
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "AmSoftDec"
#include <utils/Log.h>
#include <fcntl.h>

#include "AmFFmpegCodec.h"
#include "SoftAmdec.h"

#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/AmMediaDefsExt.h>
#include <media/IOMX.h>

#include <OMX_VideoExt.h>

#define OMX_MAX_COMPONENT_STRING_SIZE 128

namespace android {

template<class T>
static void InitOMXParams(T *params) {
    params->nSize = sizeof(T);
    params->nVersion.s.nVersionMajor = 1;
    params->nVersion.s.nVersionMinor = 0;
    params->nVersion.s.nRevision = 0;
    params->nVersion.s.nStep = 0;
}

/**
  * need to support more mime, just add here
**/
static const char * ConvertRoleToMime(const char * role) {
    CHECK(role);
    if (!strcasecmp(role, "vp6")) {
        return const_cast<char *>(MEDIA_MIMETYPE_VIDEO_VP6);
    } else if (!strcasecmp(role, "vp6a")) {
        return const_cast<char *>(MEDIA_MIMETYPE_VIDEO_VP6A);
    } else if (!strcasecmp(role, "vp6f")) {
        return const_cast<char *>(MEDIA_MIMETYPE_VIDEO_VP6F);
    } else if (!strcasecmp(role, "vp8")) {
        return const_cast<char *>(MEDIA_MIMETYPE_VIDEO_VPX);
    } else if (!strcasecmp(role, "h264")) {
        return const_cast<char *>(MEDIA_MIMETYPE_VIDEO_AVC);
#if PLATFORM_SDK_VERSION > 22
    } else if (!strcasecmp(role, "rm10")) {
        return const_cast<char *>(MEDIA_MIMETYPE_VIDEO_RM10);
    } else if (!strcasecmp(role, "rm20")) {
        return const_cast<char *>(MEDIA_MIMETYPE_VIDEO_RM20);
    } else if (!strcasecmp(role, "rm30")) {
        return const_cast<char *>(MEDIA_MIMETYPE_VIDEO_RM30);
    } else if (!strcasecmp(role, "rm40")) {
        return const_cast<char *>(MEDIA_MIMETYPE_VIDEO_RM40);
#endif
    } else if (!strcasecmp(role, "wmv2")) {
        return const_cast<char *>(MEDIA_MIMETYPE_VIDEO_WMV2);
    } else if (!strcasecmp(role, "wmv1")) {
        return const_cast<char *>(MEDIA_MIMETYPE_VIDEO_WMV1);
    } else if (!strcasecmp(role, "mpeg4s")) {
        return const_cast<char *>(MEDIA_MIMETYPE_VIDEO_MPEG4);
    } else {
        ALOGE("Not support %s yet, need to add! sdk:%d\n", role ,PLATFORM_SDK_VERSION );
        return "NA";
    }
}

static OMX_VIDEO_CODINGTYPE FindMatchingOMXVideoType(const char * mime) {
    if (!strcmp(mime, MEDIA_MIMETYPE_VIDEO_VP6)
            || !strcmp(mime, MEDIA_MIMETYPE_VIDEO_VP6A)
            || !strcmp(mime, MEDIA_MIMETYPE_VIDEO_VP6F)) {
        return static_cast<OMX_VIDEO_CODINGTYPE>(OMX_VIDEO_CodingVPX);
    } else if (!strcmp(mime, MEDIA_MIMETYPE_VIDEO_VPX)) {
        return OMX_VIDEO_CodingVP8;
    } else if (!strcmp(mime, MEDIA_MIMETYPE_VIDEO_AVC)) {
        return OMX_VIDEO_CodingAVC;
#if PLATFORM_SDK_VERSION > 22
    } else if (!strcmp(mime, MEDIA_MIMETYPE_VIDEO_RM10)) {
        return static_cast<OMX_VIDEO_CODINGTYPE>(OMX_VIDEO_CodingRV10);
    } else if (!strcmp(mime, MEDIA_MIMETYPE_VIDEO_RM20)) {
        return static_cast<OMX_VIDEO_CODINGTYPE>(OMX_VIDEO_CodingRV20);
    } else if (!strcmp(mime, MEDIA_MIMETYPE_VIDEO_RM30)) {
        return static_cast<OMX_VIDEO_CODINGTYPE>(OMX_VIDEO_CodingRV30);
    } else if (!strcmp(mime, MEDIA_MIMETYPE_VIDEO_RM40)) {
        return static_cast<OMX_VIDEO_CODINGTYPE>(OMX_VIDEO_CodingRV40);
#endif
    } else if (!strcmp(mime, MEDIA_MIMETYPE_VIDEO_WMV2) || !strcmp(mime, MEDIA_MIMETYPE_VIDEO_WMV1)) {
        return OMX_VIDEO_CodingWMV;
    } else if (!strcmp(mime, MEDIA_MIMETYPE_VIDEO_MPEG4)) {
        return OMX_VIDEO_CodingMPEG4;
    } else {
        ALOGE("Not support yet, OMX_VIDEO_CodingUnused\n");
        return OMX_VIDEO_CodingUnused;
    }
}
static const CodecProfileLevel kProfileLevels[] = {
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel1  },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel1b },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel11 },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel12 },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel13 },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel2  },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel21 },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel22 },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel3  },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel31 },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel32 },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel4  },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel41 },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel42 },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel5  },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel51 },
};

AmSoftDec::AmSoftDec(
    const char *name,
    const OMX_CALLBACKTYPE *callbacks,
    OMX_PTR appData,
    OMX_COMPONENTTYPE **component)
    : SimpleSoftOMXComponent(name, callbacks, appData, component),
      mWidth(320),
      mHeight(240),
      mTimeStampIdxIn(0),
      mTimeStampIdxOut(0),
      mEOSStatus(INPUT_DATA_AVAILABLE),
      mExtraData(NULL),
      mSkippedFrameNum(0),
      mHasNoEosData(false),
      mDecodedRightFrame(false),
      mOutputPortSettingsChange(NONE) {

    int32_t status;
    int32_t len = strlen(name);
    int32_t len1 = strlen("OMX.google.");
    int32_t len2 = strlen(".decoder");
    char role[OMX_MAX_COMPONENT_STRING_SIZE] = {0};
    strncpy(role, name+len1, len-len1-len2);
    mMimeType = ConvertRoleToMime(role);
    ALOGI("get mimetype : %s\n", mMimeType);
    initPorts();

    memset(&mVideoInfo, 0, sizeof(VIDEO_INFO_T));

    memset(mTimeStamps, 0, sizeof(mTimeStamps));

    mDecInit = false;

    CHECK_EQ(initDecoder(), (status_t)OK);
}

AmSoftDec::~AmSoftDec() {
    mCodec->decode_close();
    if (mExtraData)
        free(mExtraData);
}

void AmSoftDec::initPorts() {
    OMX_PARAM_PORTDEFINITIONTYPE def;
    InitOMXParams(&def);

    def.nPortIndex = 0;
    def.eDir = OMX_DirInput;
    def.nBufferCountMin = kInputBuffersNum;
    def.nBufferCountActual = def.nBufferCountMin;
    if (!strcmp(mMimeType, MEDIA_MIMETYPE_VIDEO_AVC)) {
        def.nBufferSize = kInputBufferSize4avc;
    } else if (!strcmp(mMimeType, MEDIA_MIMETYPE_VIDEO_VPX)) {
        def.nBufferSize = kInputBufferSize4vp8;
    } else {
        def.nBufferSize = kInputBufferSize4vp6;
    }
    def.bEnabled = OMX_TRUE;
    def.bPopulated = OMX_FALSE;
    def.eDomain = OMX_PortDomainVideo;
    def.bBuffersContiguous = OMX_FALSE;
    def.nBufferAlignment = 1;

    def.format.video.cMIMEType = const_cast<char *>(mMimeType);
    def.format.video.pNativeRender = NULL;
    def.format.video.nFrameWidth = mWidth;
    def.format.video.nFrameHeight = mHeight;
    def.format.video.nStride = def.format.video.nFrameWidth;
    def.format.video.nSliceHeight = def.format.video.nFrameHeight;
    def.format.video.nBitrate = 0;
    def.format.video.xFramerate = 0;
    def.format.video.bFlagErrorConcealment = OMX_FALSE;
    def.format.video.eCompressionFormat = FindMatchingOMXVideoType(mMimeType);
    def.format.video.eColorFormat = OMX_COLOR_FormatUnused;
    def.format.video.pNativeWindow = NULL;

    addPort(def);

    def.nPortIndex = 1;
    def.eDir = OMX_DirOutput;
    def.nBufferCountMin = kOutputBuffersNum;
    def.nBufferCountActual = def.nBufferCountMin;
    def.bEnabled = OMX_TRUE;
    def.bPopulated = OMX_FALSE;
    def.eDomain = OMX_PortDomainVideo;
    def.bBuffersContiguous = OMX_FALSE;
    def.nBufferAlignment = 2;

    def.format.video.cMIMEType = const_cast<char *>(MEDIA_MIMETYPE_VIDEO_RAW);
    def.format.video.pNativeRender = NULL;
    def.format.video.nFrameWidth = mWidth;
    def.format.video.nFrameHeight = mHeight;
    def.format.video.nStride = def.format.video.nFrameWidth;
    def.format.video.nSliceHeight = def.format.video.nFrameHeight;
    def.format.video.nBitrate = 0;
    def.format.video.xFramerate = 0;
    def.format.video.bFlagErrorConcealment = OMX_FALSE;
    def.format.video.eCompressionFormat = OMX_VIDEO_CodingUnused;
    def.format.video.eColorFormat = OMX_COLOR_FormatYUV420Planar;
    def.format.video.pNativeWindow = NULL;

    def.nBufferSize =
        (def.format.video.nFrameWidth * def.format.video.nFrameHeight * 3) / 2;

    addPort(def);
}

status_t AmSoftDec::initDecoder() {
    mCodec = AmFFmpegCodec::CreateCodec(mMimeType);
    return OK;
}

OMX_ERRORTYPE AmSoftDec::internalGetParameter(
    OMX_INDEXTYPE index, OMX_PTR params) {
    switch (index) {
    case OMX_IndexParamVideoPortFormat:
    {
        OMX_VIDEO_PARAM_PORTFORMATTYPE *formatParams =
            (OMX_VIDEO_PARAM_PORTFORMATTYPE *)params;

        if (formatParams->nPortIndex > 1) {
            return OMX_ErrorUndefined;
        }

        if (formatParams->nIndex != 0) {
            return OMX_ErrorNoMore;
        }

        if (formatParams->nPortIndex == 0) {
            formatParams->eCompressionFormat = FindMatchingOMXVideoType(mMimeType);
            formatParams->eColorFormat = OMX_COLOR_FormatUnused;
            formatParams->xFramerate = 0;
        } else {
            CHECK_EQ(formatParams->nPortIndex, 1u);

            formatParams->eCompressionFormat = OMX_VIDEO_CodingUnused;
            formatParams->eColorFormat = OMX_COLOR_FormatYUV420Planar;
            formatParams->xFramerate = 0;
        }

        return OMX_ErrorNone;
    }

    case OMX_IndexParamVideoProfileLevelQuerySupported:
        /*
        {
            OMX_VIDEO_PARAM_PROFILELEVELTYPE *profileLevel =
                    (OMX_VIDEO_PARAM_PROFILELEVELTYPE *) params;

            if (profileLevel->nPortIndex != 0) {
                ALOGE("Invalid port index: %ld", profileLevel->nPortIndex);
                return OMX_ErrorUnsupportedIndex;
            }
            if (profileLevel->nProfileIndex == 0)
            {
                profileLevel->eProfile = OMX_VIDEO_AVCProfileBaseline;
                profileLevel->eLevel   = OMX_VIDEO_AVCLevel4;

            }
            else if (profileLevel->nProfileIndex == 1)
            {
                profileLevel->eProfile = OMX_VIDEO_AVCProfileMain;
                profileLevel->eLevel   = OMX_VIDEO_AVCLevel4;
            }
            else if (profileLevel->nProfileIndex == 2)
            {
                profileLevel->eProfile = OMX_VIDEO_AVCProfileHigh;
                profileLevel->eLevel   = OMX_VIDEO_AVCLevel4;
            }
            else
            {
                 ALOGE("get_parameter: OMX_IndexParamVideoProfileLevelQuerySupported nProfileIndex ret NoMore %d\n",
                    profileLevel->nProfileIndex);

                return OMX_ErrorNoMore;
            }
            return OMX_ErrorNone;
        }
        */
    {
        OMX_VIDEO_PARAM_PROFILELEVELTYPE *profileLevel =
            (OMX_VIDEO_PARAM_PROFILELEVELTYPE *) params;

        if (profileLevel->nPortIndex != 0) {
            ALOGE("Invalid port index: %ld", profileLevel->nPortIndex);
            return OMX_ErrorUnsupportedIndex;
        }

        size_t index = profileLevel->nProfileIndex;
        size_t nProfileLevels =
            sizeof(kProfileLevels) / sizeof(kProfileLevels[0]);
        if (index >= nProfileLevels) {
            return OMX_ErrorNoMore;
        }

        profileLevel->eProfile = kProfileLevels[index].mProfile;
        profileLevel->eLevel = kProfileLevels[index].mLevel;
        return OMX_ErrorNone;
    }

    default:
        return SimpleSoftOMXComponent::internalGetParameter(index, params);
    }
}

OMX_ERRORTYPE AmSoftDec::internalSetParameter(
    OMX_INDEXTYPE index, const OMX_PTR params) {
    switch (index) {
    case OMX_IndexParamStandardComponentRole:
    {
        const OMX_PARAM_COMPONENTROLETYPE *roleParams =
            (const OMX_PARAM_COMPONENTROLETYPE *)params;

        return OMX_ErrorNone;
    }

    case OMX_IndexParamVideoPortFormat:
    {
        OMX_VIDEO_PARAM_PORTFORMATTYPE *formatParams =
            (OMX_VIDEO_PARAM_PORTFORMATTYPE *)params;

        if (formatParams->nPortIndex > 1) {
            return OMX_ErrorUndefined;
        }

        if (formatParams->nIndex != 0) {
            return OMX_ErrorNoMore;
        }

        return OMX_ErrorNone;
    }

    case OMX_IndexParamPortDefinition:
    {
        OMX_PARAM_PORTDEFINITIONTYPE *defParams = (OMX_PARAM_PORTDEFINITIONTYPE *)params;
        OMX_VIDEO_PORTDEFINITIONTYPE* videodef = &(defParams->format.video);
        //update the width & height info
        mWidth = videodef->nFrameWidth;
        mHeight = videodef->nFrameHeight;
        updatePortDefinitions(true /* updateCrop */, true /* updateInputSize */);

        //update buffer size
        defParams->nBufferSize =
            (defParams->format.video.nFrameWidth * defParams->format.video.nFrameHeight * 3) / 2;

        OMX_ERRORTYPE rtn = SimpleSoftOMXComponent::internalSetParameter(index, params);
        return rtn;
    }

    case OMX_IndexParamVideoInfo:
    {
        OMX_VIDEO_INFO *video_info = ( OMX_VIDEO_INFO *)params;
#if (PLATFORM_SDK_VERSION < 24)
        mVideoInfo.extra_data = video_info->mExtraData;
        mVideoInfo.extra_data_size = video_info->nExtraDataSize;
        if (!mVideoInfo.extra_data || mVideoInfo.extra_data_size < 0) {
            mVideoInfo.extra_data = NULL;
            mVideoInfo.extra_data_size = 0;
        }
        mVideoInfo.width = video_info->width;
        mVideoInfo.height = video_info->height;
        return OMX_ErrorNone;
#else
        if (mExtraData)
            free(mExtraData);
        mVideoInfo.extra_data = NULL;
        if (video_info->nExtraDataSize > 0) {
            mExtraData = (uint8_t *)malloc(video_info->nExtraDataSize);
            memcpy(mExtraData, video_info->mExtraData, video_info->nExtraDataSize);
            mVideoInfo.extra_data = mExtraData;
        }
        mVideoInfo.extra_data_size = video_info->nExtraDataSize;
        mVideoInfo.width = video_info->width;
        mVideoInfo.height = video_info->height;
        return OMX_ErrorNone;
#endif
    }
    default:
        return SimpleSoftOMXComponent::internalSetParameter(index, params);
    }
}

//static int in_num = 0;
//static int out_num = 0;

void AmSoftDec::onQueueFilled(OMX_U32 portIndex) {
    int status;
    if (mDecInit == false)
    {
        if (mVideoInfo.width == 0 && mVideoInfo.height == 0 &&
            mWidth && mHeight) {
            ALOGE("set resolution to mVideoInfo [%d:%d]\n", mWidth, mHeight);
            mVideoInfo.width = mWidth;
            mVideoInfo.height = mHeight;
        }
        status = mCodec->video_decode_init(mMimeType, &mVideoInfo);
        ALOGE("[%s %d] status:%d, width:%d, height:%d\n",
              __FUNCTION__, __LINE__, status, mVideoInfo.width, mVideoInfo.height);
        if (status == OK)
            mDecInit = true;
    }

    if (mOutputPortSettingsChange != NONE || mEOSStatus == OUTPUT_FRAMES_FLUSHED) {
        return;
    }

    List<BufferInfo *> &inQueue = getPortQueue(0);
    List<BufferInfo *> &outQueue = getPortQueue(1);

    VIDEO_FRAME_WRAPPER_T * picture = (VIDEO_FRAME_WRAPPER_T *)malloc(sizeof(VIDEO_FRAME_WRAPPER_T));
    PACKET_WRAPPER_T pkt;
    mCodec->video_decode_alloc_frame();
    if (!picture) {
        ALOGE("Could not allocate video frame!\n");
        mCodec->video_decode_free_frame();
        free(picture);
        return;
    }
    int32_t got_picture = 0;
    bool hasEosData = false;

    while ((mEOSStatus == INPUT_EOS_SEEN || !inQueue.empty())
            && !outQueue.empty()) {
        BufferInfo *inInfo = *inQueue.begin();
        OMX_BUFFERHEADERTYPE *inHeader = NULL;

        BufferInfo *outInfo = *outQueue.begin();
        OMX_BUFFERHEADERTYPE *outHeader = outInfo->mHeader;

        if (mEOSStatus == INPUT_DATA_AVAILABLE)
            inHeader = inInfo->mHeader;

        if ((mEOSStatus != INPUT_EOS_SEEN) && inHeader->nFlags & OMX_BUFFERFLAG_EOS) {
            mEOSStatus = INPUT_EOS_SEEN;
            if (inHeader && inHeader->nFilledLen > 0)
                hasEosData = true;
            else
                mHasNoEosData = true;
        }

        if (hasEosData || (mEOSStatus != INPUT_EOS_SEEN)) {
            pkt.data = inHeader->pBuffer + inHeader->nOffset;
            pkt.size  = inHeader->nFilledLen;
            mTimeStamps[mTimeStampIdxIn] = inHeader->nTimeStamp;
            mTimeStampIdxIn = (mTimeStampIdxIn + 1) % kNumBuffers;
            //ALOGI("in_num:%d\n", ++in_num);
        } else {
            pkt.data = NULL;
            pkt.size  = 0;
        }

        int32_t len = mCodec->video_decode_frame(picture, &got_picture, &pkt);
        //ALOGI("mSkippedFrameNum:%d, mHasNoEosData:%d, mEOSStatus:%d, len:%x, got_picture:%d\n", mSkippedFrameNum, mHasNoEosData, mEOSStatus, len, got_picture);
        if (!mDecodedRightFrame && got_picture) {
            mDecodedRightFrame = true;
        }
        if ((mEOSStatus == INPUT_EOS_SEEN)) {
            if (!got_picture && (mSkippedFrameNum < 0 || (mHasNoEosData == true && mSkippedFrameNum == 0))) {
                outHeader->nFilledLen = 0;
                outHeader->nFlags = OMX_BUFFERFLAG_EOS;
                outQueue.erase(outQueue.begin());
                outInfo->mOwnedByUs = false;
                notifyFillBufferDone(outHeader);
                mCodec->video_decode_free_frame();
                free(picture);
                mEOSStatus = OUTPUT_FRAMES_FLUSHED;
                return;
            } else {
                mSkippedFrameNum--;
            }
        }

        if (len < 0 || !got_picture) {
            ALOGE("Cannot decode this frame!\n");
            if (inInfo) {
                inInfo->mOwnedByUs = false;
                inQueue.erase(inQueue.begin());
                inInfo = NULL;
            }
            notifyEmptyBufferDone(inHeader);
            inHeader = NULL;
            if (len >= 0 && !mDecodedRightFrame)
                mSkippedFrameNum++;
            if (mEOSStatus != INPUT_EOS_SEEN) {
                mCodec->video_decode_free_frame();
                free(picture);
                return;
            } else {
                hasEosData = false;
                continue;
            }
        }

        if (hasEosData || (mEOSStatus != INPUT_EOS_SEEN)) {
            if (inInfo) {
                inInfo->mOwnedByUs = false;
                inQueue.erase(inQueue.begin());
                inInfo = NULL;
            }
            notifyEmptyBufferDone(inHeader);
            inHeader = NULL;
            hasEosData = false;
        }

        CHECK_EQ(picture->format, 0/*AV_PIX_FMT_YUV420P*/);

        int32_t width = picture->width;
        int32_t height = picture->height;

        if (width == 0 || height == 0) {
            mCodec->video_decode_get_display(&width, &height);
        }

        if (width != mWidth || height != mHeight) {
            mWidth = width;
            mHeight = height;

            updatePortDefinitions(true);

            notify(OMX_EventPortSettingsChanged, 1, 0, NULL);
            mOutputPortSettingsChange = AWAITING_DISABLED;
            mCodec->video_decode_free_frame();
            free(picture);
            return;
        }
        const uint8_t *srcLine = (const uint8_t *)picture->data[0];
        uint8_t *dst = outHeader->pBuffer;
        for (size_t i = 0; i < height; ++i) {
            memcpy(dst, srcLine, width);
            srcLine += picture->linesize[0];
            dst += width;
        }

        srcLine = (const uint8_t *)picture->data[1];
        for (size_t i = 0; i < height / 2; ++i) {
            memcpy(dst, srcLine, width / 2);
            srcLine += picture->linesize[1];
            dst += width / 2;
        }

        srcLine = (const uint8_t *)picture->data[2];
        for (size_t i = 0; i < height / 2; ++i) {
            memcpy(dst, srcLine, width / 2);
            srcLine += picture->linesize[2];
            dst += width / 2;
        }

        outHeader->nOffset = 0;
        outHeader->nFilledLen = (width * height * 3) / 2;
        if ((mEOSStatus == INPUT_EOS_SEEN) && (mSkippedFrameNum < 0)) {
            outHeader->nFlags = OMX_BUFFERFLAG_EOS;
            mEOSStatus = OUTPUT_FRAMES_FLUSHED;
        } else {
            outHeader->nFlags = 0;
        }
        outHeader->nTimeStamp = mTimeStamps[mTimeStampIdxOut];
        mTimeStampIdxOut = (mTimeStampIdxOut + 1) % kNumBuffers;

        outInfo->mOwnedByUs = false;
        outQueue.erase(outQueue.begin());
        outInfo = NULL;
        //ALOGI("out_num:%d\n", ++out_num);

        notifyFillBufferDone(outHeader);

        outHeader = NULL;

    }
    mCodec->video_decode_free_frame();
    free(picture);
}

void AmSoftDec::resetDecoder() {
    if ((mEOSStatus != OUTPUT_FRAMES_FLUSHED) && (mDecInit == true)) {
        mCodec->decode_close();
        mDecInit = false;
        initDecoder();
    }

    return;
}

void AmSoftDec::onPortFlushCompleted(OMX_U32 portIndex) {
    if (portIndex == 0) {

        ALOGI("onPortFlushCompleted.");
        resetDecoder();

        memset(mTimeStamps, 0, sizeof(mTimeStamps));
        mTimeStampIdxIn = 0;
        mTimeStampIdxOut = 0;
        mEOSStatus = INPUT_DATA_AVAILABLE;
        mSkippedFrameNum = 0;
        mHasNoEosData = false;
        mDecodedRightFrame = false;
        //in_num = 0;
        //out_num = 0;
    }
}

void AmSoftDec::onPortEnableCompleted(OMX_U32 portIndex, bool enabled) {
    if (portIndex != 1) {
        return;
    }

    switch (mOutputPortSettingsChange) {
    case NONE:
        break;

    case AWAITING_DISABLED:
    {
        CHECK(!enabled);
        mOutputPortSettingsChange = AWAITING_ENABLED;
        break;
    }

    default:
    {
        CHECK_EQ((int)mOutputPortSettingsChange, (int)AWAITING_ENABLED);
        CHECK(enabled);
        mOutputPortSettingsChange = NONE;
        break;
    }
    }
}

void AmSoftDec::updatePortDefinitions(bool updateCrop, bool updateInputSize) {
    OMX_PARAM_PORTDEFINITIONTYPE *def = &editPortInfo(0)->mDef;
    def->format.video.nFrameWidth = mWidth;
    def->format.video.nFrameHeight = mHeight;
    def->format.video.nStride = def->format.video.nFrameWidth;
    def->format.video.nSliceHeight = def->format.video.nFrameHeight;

    def = &editPortInfo(1)->mDef;
    def->format.video.nFrameWidth = mWidth;
    def->format.video.nFrameHeight = mHeight;
    def->format.video.nStride = def->format.video.nFrameWidth;
    def->format.video.nSliceHeight = def->format.video.nFrameHeight;

    def->nBufferSize =
        (def->format.video.nFrameWidth
         * def->format.video.nFrameHeight * 3) / 2;

    if (updateCrop) {
        mCropLeft = 0;
        mCropTop = 0;
        mCropWidth = mWidth;
        mCropHeight = mHeight;
    }

    ALOGE("mCropWidth:%d, mCropHeight:%d\n", mCropWidth, mCropHeight);

}

OMX_ERRORTYPE AmSoftDec::getConfig(
    OMX_INDEXTYPE index, OMX_PTR params) {
    switch (index) {
    case OMX_IndexConfigCommonOutputCrop:
    {
        OMX_CONFIG_RECTTYPE *rectParams = (OMX_CONFIG_RECTTYPE *)params;

        if (rectParams->nPortIndex != 1) {
            return OMX_ErrorUndefined;
        }

        rectParams->nLeft = mCropLeft;
        rectParams->nTop = mCropTop;
        rectParams->nWidth = mCropWidth;
        rectParams->nHeight = mCropHeight;

        return OMX_ErrorNone;
    }

    default:
        return OMX_ErrorUnsupportedIndex;
    }
}

void AmSoftDec::onReset() {
    ALOGI("onReset.");
    resetDecoder();

    memset(mTimeStamps, 0, sizeof(mTimeStamps));
    mTimeStampIdxIn = 0;
    mTimeStampIdxOut = 0;
    mEOSStatus = INPUT_DATA_AVAILABLE;
    mSkippedFrameNum = 0;
    mHasNoEosData = false;
    mDecodedRightFrame = false;
    //in_num = 0;
    //out_num = 0;

}
}  // namespace android

android::SoftOMXComponent *createSoftOMXComponent(
    const char *name, const OMX_CALLBACKTYPE *callbacks,
    OMX_PTR appData, OMX_COMPONENTTYPE **component) {
    return new android::AmSoftDec(name, callbacks, appData, component);
}
