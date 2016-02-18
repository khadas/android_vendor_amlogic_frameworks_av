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

#ifndef SOFT_MP2_H_

#define SOFT_MP2_H_

#include "SimpleSoftOMXComponent.h"

#include "mpg123.h"

namespace android {

struct SoftMP2 : public SimpleSoftOMXComponent {
    SoftMP2(const char *name,
            const OMX_CALLBACKTYPE *callbacks,
            OMX_PTR appData,
            OMX_COMPONENTTYPE **component);

protected:
    virtual ~SoftMP2();

    virtual OMX_ERRORTYPE internalGetParameter(
            OMX_INDEXTYPE index, OMX_PTR params);

    virtual OMX_ERRORTYPE internalSetParameter(
            OMX_INDEXTYPE index, const OMX_PTR params);

    virtual void onQueueFilled(OMX_U32 portIndex);
    virtual void onPortEnableCompleted(OMX_U32 portIndex, bool enabled);


private:
    enum {
        kNumBuffers = 4,
        kOutputBufferSize = 4608 * 2
    };

    int64_t mAnchorTimeUs;
    int64_t mNumFramesOutput;
	unsigned char* m_buffer;

    int32_t mNumChannels;
    int32_t mSamplingRate;

    bool mConfigured;

    bool mSignalledError;

    enum {
        NONE,
        AWAITING_DISABLED,
        AWAITING_ENABLED
    } mOutputPortSettingsChange;

    void initPorts();
    void initDecoder();
	
	bool init_mpg123decoder();

	int read(mpg123_handle* m_handle, unsigned char* m_buffer, size_t m_buffer_size);
	
	int readBuffer(mpg123_handle* m_handle, unsigned char* m_buffer, size_t m_buffer_size);
	void cleanup(mpg123_handle* handle);
	mpg123_handle * m_handle;
	
    DISALLOW_EVIL_CONSTRUCTORS(SoftMP2);
};

}  // namespace android

#endif  // SOFT_MP2_H_


