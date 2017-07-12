/*
 * Copyright (C) 2010 Amlogic Corporation.
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



/****************************************************************************
 *
 *
 *                     amlogic soft audio decoder
 *
 *
 ***************************************************************************/

#ifndef COM_SOFT_AMADEC_H_
#define COM_SOFT_AMADEC_H_

#include "SimpleSoftOMXComponent.h"

class AmFFmpegCodec;
struct AUDIO_INFO;

namespace android {

struct SoftAmadec : public SimpleSoftOMXComponent {
    SoftAmadec(const char *name,
               const OMX_CALLBACKTYPE *callbacks,
               OMX_PTR appData,
               OMX_COMPONENTTYPE **component);

protected:
    virtual ~SoftAmadec();

    virtual OMX_ERRORTYPE internalGetParameter(
        OMX_INDEXTYPE index, OMX_PTR params);

    virtual OMX_ERRORTYPE internalSetParameter(
        OMX_INDEXTYPE index, const OMX_PTR params);

    virtual void onQueueFilled(OMX_U32 portIndex);
    virtual void onPortFlushCompleted(OMX_U32 portIndex);
    virtual void onPortEnableCompleted(OMX_U32 portIndex, bool enabled);

private:

    enum {
        kInputBuffersNum = 4,
        kOutputBuffersNum = 4,
    };

    struct AUDIO_INFO * mAInfo;
    const char * mMimeType;
    char mComponentRole[128];
    bool setflag;
    sp<AmFFmpegCodec> mCodec;

    enum {
        NONE,
        AWAITING_DISABLED,
        AWAITING_ENABLED
    } mOutputPortSettingsChange;

    void initPorts();
    status_t initDecoder();

    DISALLOW_EVIL_CONSTRUCTORS(SoftAmadec);
};

}

#endif
