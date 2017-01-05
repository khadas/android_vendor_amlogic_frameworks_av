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
 *
 */

#ifndef OMX_Audio_EX_AML_h
#define OMX_Audio_EX_AML_h

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef enum OMX_AUDIO_CODINGTYPE_AML {
    OMX_AUDIO_CodingDDP               = 0x7F001000, /**< Any variant of DDP encoded data */
    OMX_AUDIO_CodingTRUEHD,                         /**< Any variant of TRUEHD encoded data */
    OMX_AUDIO_CodingFFMPEG,                         /*for all ffmpeg decoder*/
} OMX_AUDIO_CODINGTYPE_AML;

/** asfparams */
typedef struct OMX_AUDIO_PARAM_ASFTYPE {
    OMX_U32 nSize;            /**< Size of this structure, in Bytes */
    OMX_VERSIONTYPE nVersion; /**< OMX specification version information */
    OMX_U32 nPortIndex;       /**< Port that this structure applies to */

    OMX_U16 wFormatTag;
    OMX_U16 nChannels;
    OMX_U32 nSamplesPerSec;
    OMX_U32 nAvgBitratePerSec;
    OMX_U16 nBlockAlign;
    OMX_U16 wBitsPerSample;
    OMX_U16 extradata_size;
    OMX_U8  extradata[128];
} OMX_AUDIO_PARAM_ASFTYPE;

typedef struct OMX_AUDIO_PARAM_APETYPE {
    OMX_U32 nSize;            /**< Size of this structure, in Bytes */
    OMX_VERSIONTYPE nVersion; /**< OMX specification version information */
    OMX_U32 nPortIndex;       /**< Port that this structure applies to */

    OMX_U16 nChannels;
    OMX_U32 nSamplesPerSec;
    OMX_U16 wBitsPerSample;
    OMX_U16 extradata_size;
    OMX_U8  *extradata;
} OMX_AUDIO_PARAM_APETYPE;

typedef struct OMX_AUDIO_PARAM_ALACTYPE {
    OMX_U32 nSize;            /**< Size of this structure, in Bytes */
    OMX_VERSIONTYPE nVersion; /**< OMX specification version information */
    OMX_U32 nPortIndex;       /**< Port that this structure applies to */

    OMX_U16 nChannels;
    OMX_U32 nSamplesPerSec;
    OMX_U16 extradata_size;
    OMX_U8 *extradata;
} OMX_AUDIO_PARAM_ALACTYPE;

typedef enum OMX_INDEXTYPE_AML {
    // specific added by amlogic to enhance omx to support more audio codec format
    OMX_IndexParamAudioAsf = 0x04400000,    /**< reference: OMX_AUDIO_PARAM_WMASTDTYPE */
    OMX_IndexParamAudioAlac,                /**< reference: OMX_AUDIO_PARAM_ALACTYPE */
    OMX_IndexParamAudioDtshd,               /**< reference: OMX_AUDIO_PARAM_DTSHDTYPE */
    OMX_IndexParamAudioApe,                 /**< reference: OMX_AUDIO_PARAM_APETYPE */
    OMX_IndexParamAudioDolbyAudio,          /**< reference: OMX_AUDIO_PARAM_DOLBYAUDIO */
    OMX_IndexParamAudioFFmpeg,
} OMX_INDEXTYPE_AML;
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
