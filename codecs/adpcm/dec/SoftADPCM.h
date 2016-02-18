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
 *Note: The SoftADPCM code is added to stagefright by zedong.xiong, Amlogic Inc. 
 *          2012.9.25
 */

#ifndef SOFT_ADPCM_H_

#define SOFT_ADPCM_H_

#include "SimpleSoftOMXComponent.h"

namespace android {
	
//ima
const int adpcm_step[89] =
{
  7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
  19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
  50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
  130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
  337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
  876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
  2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
  5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
  15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

const int adpcm_index[16] =
{
  -1, -1, -1, -1, 2, 4, 6, 8,
  -1, -1, -1, -1, 2, 4, 6, 8
};
#define CLAMP_0_TO_88(x)  if (x < 0) x = 0; else if (x > 88) x = 88;
// clamp a number within a signed 16-bit range
#define CLAMP_S16(x)  if (x < -32768) x = -32768; \
  else if (x > 32767) x = 32767;
// clamp a number above 16
#define CLAMP_ABOVE_16(x)  if (x < 16) x = 16;
// sign extend a 16-bit value
#define SE_16BIT(x)  if (x & 0x8000) x -= 0x10000;
// sign extend a 4-bit value
#define SE_4BIT(x)  if (x & 0x8) x -= 0x10;

#define le2me_16(x) (x)

#define MS_IMA_ADPCM_PREAMBLE_SIZE 4

#define LE_16(x) (le2me_16(*(unsigned short *)(x)))

//ms
#define MSADPCM_ADAPT_COEFF_COUNT   7
const int AdaptationTable [] =
{	230, 230, 230, 230, 307, 409, 512, 614,
768, 614, 512, 409, 307, 230, 230, 230
} ;

/* TODO : The first 7 coef's are are always hardcode and must
appear in the actual WAVE file.  They should be read in
in case a sound program added extras to the list. */

const int AdaptCoeff1 [MSADPCM_ADAPT_COEFF_COUNT] =
{	256, 512, 0, 192, 240, 460, 392
} ;

const int AdaptCoeff2 [MSADPCM_ADAPT_COEFF_COUNT] =
{	0, -256, 0, 64, 0, -208, -232
} ;




struct SoftADPCM : public SimpleSoftOMXComponent {
    SoftADPCM(const char *name,
            const OMX_CALLBACKTYPE *callbacks,
            OMX_PTR appData,
            OMX_COMPONENTTYPE **component);

protected:
    virtual ~SoftADPCM();

    virtual OMX_ERRORTYPE internalGetParameter(
            OMX_INDEXTYPE index, OMX_PTR params);

    virtual OMX_ERRORTYPE internalSetParameter(
            OMX_INDEXTYPE index, const OMX_PTR params);

    virtual void onQueueFilled(OMX_U32 portIndex);
    virtual void onPortEnableCompleted(OMX_U32 portIndex, bool enabled);

private:
	
    enum {
		kNumBuffers = 4,
		kOutputBufferSize = 16384,
    };
    enum {
        NONE,
        AWAITING_DISABLED,
        AWAITING_ENABLED
    } mOutputPortSettingsChange;	
	bool mIsIma;
	OMX_U32 mNumChannels;
	OMX_U32 mSampleRate;
	OMX_U32 mBlockAlign;
	bool mSignalledError;
	
    void initPorts();
	bool ima_decoder(OMX_U8 *out, OMX_U32 * outlen, const uint8_t *in, size_t inSize);
	bool ms_decoder(OMX_U8 *out, OMX_U32 * outlen, const uint8_t *in, size_t inSize);
	int ima_decode_block(unsigned short *output, const unsigned char *input, int channels, int block_size);
	int ms_decode_block(short *pcm_buf, const unsigned char *buf, int channel, int block);
	void decode_nibbles(unsigned short *output,
  								int output_size, int channels,
  								int predictor_l, int index_l,
  								int predictor_r, int index_r);

	
    DISALLOW_EVIL_CONSTRUCTORS(SoftADPCM);
};

}  // namespace android

#endif  // SOFT_G711_H_

