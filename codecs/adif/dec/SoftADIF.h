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

#ifndef SOFT_ADIF_H_

#define SOFT_ADIF_H_

#include "SimpleSoftOMXComponent.h"
#include "neaacdec.h"

namespace android {

struct SoftADIF : public SimpleSoftOMXComponent {
    SoftADIF(const char *name,
            const OMX_CALLBACKTYPE *callbacks,
            OMX_PTR appData,
            OMX_COMPONENTTYPE **component);

protected:
    virtual ~SoftADIF();

    virtual OMX_ERRORTYPE internalGetParameter(
            OMX_INDEXTYPE index, OMX_PTR params);

    virtual OMX_ERRORTYPE internalSetParameter(
            OMX_INDEXTYPE index, const OMX_PTR params);

    virtual void onQueueFilled(OMX_U32 portIndex);
    virtual void onPortEnableCompleted(OMX_U32 portIndex, bool enabled);
    virtual void onPortFlushCompleted(OMX_U32 portIndex);
private:
    enum {
        kNumBuffers = 4,
        kMaxNumSamplesPerFrame = 16384,
        AAC_MIN_STREAMSIZE = 768,
        AAC_CHANNELS = 8,
    };
    enum {
        NONE,
        AWAITING_DISABLED,
        AWAITING_ENABLED
    } mOutputPortSettingsChange;
	aac_buffer * b;

	NeAACDecHandle hDecoder;
	NeAACDecFrameInfo * frameInfo;
	NeAACDecConfigurationPtr config;

	OMX_U32 mNumSampleRate;
	OMX_U32 mNumFrameLength;

	size_t mInputBufferCount;
	OMX_U32 mNumChannels;
	bool mSignalledError;
	int64_t mAnchorTimeUs;
	unsigned char *pAdifHeader;
	int AdifHeaderSize;
	void initPorts();
	status_t initDecoder();
	bool isConfigured() const;

	
    DISALLOW_EVIL_CONSTRUCTORS(SoftADIF);
};

}  // namespace android

#endif  // SOFT_ADIF_H_

