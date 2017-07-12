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

#ifndef AM_SOFT_DEC_H_
#define AM_SOFT_DEC_H_

#include "SimpleSoftOMXComponent.h"

class AmFFmpegCodec;

namespace android {

struct AmSoftDec : public SimpleSoftOMXComponent {
    AmSoftDec(const char *name,
              const OMX_CALLBACKTYPE *callbacks,
              OMX_PTR appData,
              OMX_COMPONENTTYPE **component);

protected:
    virtual ~AmSoftDec();

    virtual OMX_ERRORTYPE internalGetParameter(
        OMX_INDEXTYPE index, OMX_PTR params);

    virtual OMX_ERRORTYPE internalSetParameter(
        OMX_INDEXTYPE index, const OMX_PTR params);

    virtual void onQueueFilled(OMX_U32 portIndex);
    virtual void onPortFlushCompleted(OMX_U32 portIndex);
    virtual void onPortEnableCompleted(OMX_U32 portIndex, bool enabled);

    virtual void onReset();

    virtual OMX_ERRORTYPE getConfig(
        OMX_INDEXTYPE index, OMX_PTR params);
private:

    enum {
        kInputBuffersNum = 4,
        kOutputBuffersNum = 4,
        kInputBufferSize4vp6 = 512 * 1024,
        kInputBufferSize4vp8 = 3 * 1024 * 1024,
        kInputBufferSize4avc = 2 * 1024 * 1024,
    };

    enum {
        kNumBuffers = 32
    };

    int32_t mWidth;
    int32_t mHeight;
    const char * mMimeType;

    sp<AmFFmpegCodec> mCodec;
    uint32_t mCropLeft, mCropTop, mCropWidth, mCropHeight;

    OMX_TICKS mTimeStamps[kNumBuffers];
    uint8_t mTimeStampIdxIn;
    uint8_t mTimeStampIdxOut;

    enum {
        NONE,
        AWAITING_DISABLED,
        AWAITING_ENABLED
    } mOutputPortSettingsChange;

    enum {
        INPUT_DATA_AVAILABLE,  // VPX component is ready to decode data.
        INPUT_EOS_SEEN,        // VPX component saw EOS and is flushing On2 decoder.
        OUTPUT_FRAMES_FLUSHED  // VPX component finished flushing On2 decoder.
    } mEOSStatus;

    void initPorts();
    status_t initDecoder();

    virtual void updatePortDefinitions(bool updateCrop = true, bool updateInputSize = false);

    void resetDecoder();

    VIDEO_INFO_T mVideoInfo;

    uint8_t* mExtraData;

    int mSkippedFrameNum;

    bool mHasNoEosData;

    bool mDecodedRightFrame;

    bool mDecInit;
    DISALLOW_EVIL_CONSTRUCTORS(AmSoftDec);
};

}  // namespace android

#endif  // AM_SOFT_DEC_H_
