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

#define LOG_NDEBUG 0
#define LOG_TAG "NU-AmNuPlayer"
#include <utils/Log.h>

#include <stdio.h>
#include <assert.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <inttypes.h>
#include "AmNuPlayer.h"

#include "AmHTTPLiveSource.h"
#include "AmNuPlayerCCDecoder.h"
#include "AmNuPlayerDecoder.h"
#include "AmNuPlayerDecoderBase.h"
#include "AmNuPlayerDecoderPassThrough.h"
#include "AmNuPlayerDriver.h"
#include "AmNuPlayerRenderer.h"
#include "AmNuPlayerSource.h"
#include "AmRTSPSource.h"
#include "AmStreamingSource.h"
#include "AmGenericSource.h"
#include "TextDescriptions.h"

#include "AmATSParser.h"

#include <cutils/properties.h>

#include <media/AudioResamplerPublic.h>
#include <media/AVSyncSettings.h>

#include <media/stagefright/foundation/hexdump.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/MediaBuffer.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MetaData.h>
#ifdef WITH_AMLOGIC_MEDIA_EX_SUPPORT
#include <media/stagefright/AmMediaDefsExt.h>
#endif


#include <gui/IGraphicBufferProducer.h>
#include <gui/Surface.h>

#include "avc_utils.h"

#include "ESDS.h"
#include <media/stagefright/Utils.h>

#include <Amavutils.h>

namespace android {

struct AmNuPlayer::Action : public RefBase {
    Action() {}

    virtual void execute(AmNuPlayer *player) = 0;

private:
    DISALLOW_EVIL_CONSTRUCTORS(Action);
};

struct AmNuPlayer::SeekAction : public Action {
    SeekAction(int64_t seekTimeUs)
        : mSeekTimeUs(seekTimeUs) {
    }

    virtual void execute(AmNuPlayer *player) {
        player->performSeek(mSeekTimeUs);
    }

private:
    int64_t mSeekTimeUs;

    DISALLOW_EVIL_CONSTRUCTORS(SeekAction);
};

struct AmNuPlayer::ResumeDecoderAction : public Action {
    ResumeDecoderAction(bool needNotify)
        : mNeedNotify(needNotify) {
    }

    virtual void execute(AmNuPlayer *player) {
        player->performResumeDecoders(mNeedNotify);
    }

private:
    bool mNeedNotify;

    DISALLOW_EVIL_CONSTRUCTORS(ResumeDecoderAction);
};

struct AmNuPlayer::SetSurfaceAction : public Action {
    SetSurfaceAction(const sp<Surface> &surface)
        : mSurface(surface) {
    }

    virtual void execute(AmNuPlayer *player) {
        player->performSetSurface(mSurface);
    }

private:
    sp<Surface> mSurface;

    DISALLOW_EVIL_CONSTRUCTORS(SetSurfaceAction);
};

struct AmNuPlayer::FlushDecoderAction : public Action {
    FlushDecoderAction(FlushCommand audio, FlushCommand video)
        : mAudio(audio),
          mVideo(video) {
    }

    virtual void execute(AmNuPlayer *player) {
        player->performDecoderFlush(mAudio, mVideo);
    }

private:
    FlushCommand mAudio;
    FlushCommand mVideo;

    DISALLOW_EVIL_CONSTRUCTORS(FlushDecoderAction);
};

struct AmNuPlayer::PostMessageAction : public Action {
    PostMessageAction(const sp<AMessage> &msg)
        : mMessage(msg) {
    }

    virtual void execute(AmNuPlayer *) {
        mMessage->post();
    }

private:
    sp<AMessage> mMessage;

    DISALLOW_EVIL_CONSTRUCTORS(PostMessageAction);
};

// Use this if there's no state necessary to save in order to execute
// the action.
struct AmNuPlayer::SimpleAction : public Action {
    typedef void (AmNuPlayer::*ActionFunc)();

    SimpleAction(ActionFunc func)
        : mFunc(func) {
    }

    virtual void execute(AmNuPlayer *player) {
        (player->*mFunc)();
    }

private:
    ActionFunc mFunc;

    DISALLOW_EVIL_CONSTRUCTORS(SimpleAction);
};

////////////////////////////////////////////////////////////////////////////////
// static
Mutex AmNuPlayer::mThreadLock;
Vector<android_thread_id_t> AmNuPlayer::mThreadId;

AmNuPlayer::AmNuPlayer(pid_t pid)
    : mUIDValid(false),
      mPID(pid),
      mSourceFlags(0),
      mOffloadAudio(false),
      mAudioDecoderGeneration(0),
      mVideoDecoderGeneration(0),
      mRendererGeneration(0),
      mPreviousSeekTimeUs(0),
      mAudioEOS(false),
      mVideoEOS(false),
      mScanSourcesPending(false),
      mScanSourcesGeneration(0),
      mPollDurationGeneration(0),
      mTimedTextGeneration(0),
      mFlushingAudio(NONE),
      mFlushingVideo(NONE),
      mResumePending(false),
      mVideoScalingMode(NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW),
      mEnableFrameRate(false),
      mFrameRate(-1.0),
      mDecodecParam(-1),
      mWaitSeconds(10),
      mPlaybackSettings(AUDIO_PLAYBACK_RATE_DEFAULT),
      mVideoFpsHint(-1.f),
      mStarted(false),
      mPrepared(false),
      mResetting(false),
      mSourceStarted(false),
      mPaused(false),
      mPausedByClient(true),
      mPausedForBuffering(false) {
    clearFlushComplete();
    DtshdApreTotal=0;
    DtsHdStreamType=0;
    DtsHdMulAssetHint=0;
    DtsHdHpsHint=0;
    mStrCurrentAudioCodec = NULL;
    char value[PROPERTY_VALUE_MAX];
    if (property_get("media.hls.wait-seconds", value, NULL)) {
        mWaitSeconds = atoi(value);
    }
    memset(&mStreamInfo, 0, sizeof(mStreamInfo));
}

AmNuPlayer::~AmNuPlayer() {
    if (mEnableFrameRate) {
        amsysfs_set_sysfs_int("/sys/class/video/video_seek_flag", 0);
        amsysfs_set_sysfs_int("/sys/class/ionvideo/ionvideo_seek_flag", 0);
    }
    for (int i = 0; i < mStreamInfo.stream_info.total_video_num; i++) {
        if (mStreamInfo.video_info[i] != NULL) {
            free(mStreamInfo.video_info[i]);
            mStreamInfo.video_info[i] = NULL;
        }
    }

    for (int i = 0; i < mStreamInfo.stream_info.total_audio_num; i++) {
        if (mStreamInfo.audio_info[i] != NULL) {
            free(mStreamInfo.audio_info[i]);
            mStreamInfo.audio_info[i] = NULL;
        }
    }
}

// static
void AmNuPlayer::thread_interrupt() {
    android_thread_id_t thread_id = androidGetThreadId();

    Mutex::Autolock autoLock(mThreadLock);

    if (mThreadId.isEmpty()) {
        mThreadId.push_back(thread_id);
    } else {
        size_t size = mThreadId.size();
        size_t i;
        for (i = 0; i < size; i++) {
            if (mThreadId.itemAt(i) == thread_id) {
                break;
            }
        }
        if (i == size) {
            mThreadId.push_back(thread_id);
        }
    }
}

// static
void AmNuPlayer::thread_uninterrupt() {
    android_thread_id_t thread_id = androidGetThreadId();

    Mutex::Autolock autoLock(mThreadLock);

    size_t size = mThreadId.size();
    for (size_t i = 0; i < size; i++) {
        if (mThreadId.itemAt(i) == thread_id) {
            mThreadId.removeAt(i);
            break;
        }
    }
}

// static
int32_t AmNuPlayer::interrupt_callback(android_thread_id_t thread_id) {
    Mutex::Autolock autoLock(mThreadLock);
    if (mThreadId.isEmpty()) {
        return 0;
    } else {
        size_t size = mThreadId.size();
        for (size_t i = 0; i < size; i++) {
            if (mThreadId.itemAt(i) == thread_id) {
                return 1;
            }
        }
    }
    return 0;
}
int AmNuPlayer::getintfromString8(String8 &s, const char*pre){
    int off;
    int val = 0;
    if ((off = s.find(pre, 0)) >= 0) {
        sscanf(s.string() + off + strlen(pre), "%d", &val);
    }
    return val;
}


#define DTSM6_EXCHANGE_INFO_NODE "/sys/class/amaudio/debug"
static void dtsm6_get_exchange_info(int *streamtype,int *APreCnt,int *APreSel,int *ApreAssetSel,int32_t *ApresAssetsArray,int *MulAssetHint,int *HPs_hint){
    int fd=open(DTSM6_EXCHANGE_INFO_NODE,  O_RDWR | O_TRUNC, 0644);
    int bytes=0,i=0;

    if (fd >= 0) {
        uint8_t ubuf8[256]={0};
        bytes=read(fd,ubuf8,256);

        if (streamtype != NULL) {
            uint8_t *pStreamType=(uint8_t *)strstr((const char*)ubuf8,"StreamType");
            if (pStreamType != NULL) {
               pStreamType+=10;
               *streamtype=atoi((const char*)pStreamType);
            }
        }

        if (APreCnt != NULL) {
            uint8_t *pApreCnt=(uint8_t *)strstr((const char*)ubuf8,"ApreCnt");
            if (pApreCnt != NULL) {
               pApreCnt+=7;
               *APreCnt=atoi((const char*)pApreCnt);
            }
        }

        if (APreSel != NULL) {
            uint8_t *pApreSel=(uint8_t *)strstr((const char*)ubuf8,"ApreSel");
            if (pApreSel != NULL) {
               pApreSel+=7;
               *APreSel=atoi((const char*)pApreSel);
            }
        }

        if (ApreAssetSel != NULL) {
            uint8_t *pApreAssetSel=(uint8_t *)strstr((const char*)ubuf8,"ApreAssetSel");
            if (pApreAssetSel != NULL) {
                pApreAssetSel+=12;
                *ApreAssetSel=atoi((const char*)pApreAssetSel);
            }
        }

        if (ApresAssetsArray != NULL && APreCnt != NULL) {
            uint8_t *pApresAssetsArray=(uint8_t *)strstr((const char*)ubuf8,"ApresAssetsArray");
            if (pApresAssetsArray != NULL) {
               pApresAssetsArray+=16;
               for (i=0;i<*APreCnt;i++) {
                 ApresAssetsArray[i]=pApresAssetsArray[i];
                 ALOGI("[%s %d]ApresAssetsArray[%d]/%d",__FUNCTION__,__LINE__,i,ApresAssetsArray[i]);
               }
            }
        }
        if (MulAssetHint != NULL) {
            uint8_t *pMulAssetHint=(uint8_t *)strstr((const char*)ubuf8,"MulAssetHint");
            if (pMulAssetHint != NULL) {
               pMulAssetHint+=12;
               *MulAssetHint=atoi((const char*)pMulAssetHint);
            }
        }
        if (HPs_hint != NULL) {
            uint8_t *phps_hint=(uint8_t *)strstr((const char*)ubuf8,"HPSHint");
            if (phps_hint != NULL) {
               phps_hint +=7;
               *HPs_hint=atoi((const char*)phps_hint);
            }
        }
        close(fd);
    } else {
        ALOGI("[%s %d]open %s failed!\n",__FUNCTION__,__LINE__,DTSM6_EXCHANGE_INFO_NODE);
       if (streamtype != NULL)  *streamtype=0;
       if (APreCnt != NULL)     *APreCnt=0;
       if (APreSel != NULL)     *APreSel=0;
       if (ApreAssetSel != NULL)*ApreAssetSel=0;
       if (HPs_hint != NULL)    *HPs_hint=0;
       if (ApresAssetsArray != NULL&& APreCnt != NULL) memset(ApresAssetsArray,0,*APreCnt);
    }
}

static void dtsm6_set_exchange_info(int *APreSel,int *ApreAssetSel)
{
    int fd=open(DTSM6_EXCHANGE_INFO_NODE,  O_RDWR | O_TRUNC, 0644);
    int bytes,pos=0;
    if (fd >= 0) {
       char ubuf8[128]={0};
       if (APreSel != NULL) {
           bytes=sprintf(ubuf8,"dtsm6_apre_sel_set%d",*APreSel);
           write(fd, ubuf8, bytes);
       }
       if (ApreAssetSel != NULL) {
           bytes=sprintf(ubuf8,"dtsm6_apre_assets_sel_set%d",*ApreAssetSel);
           write(fd, ubuf8, bytes);
       }
       close(fd);
    }else{
       ALOGI("[%s %d]open %s failed!\n",__FUNCTION__,__LINE__,DTSM6_EXCHANGE_INFO_NODE);
    }
}
static aformat_t audioTypeConvert(enum CodecID id)
{
    aformat_t format = (aformat_t)-1;
    switch (id) {
    case CODEC_ID_PCM_MULAW:
        //format = AFORMAT_MULAW;
        format = AFORMAT_ADPCM;
        break;

    case CODEC_ID_PCM_ALAW:
        //format = AFORMAT_ALAW;
        format = AFORMAT_ADPCM;
        break;


    case CODEC_ID_MP1:
    case CODEC_ID_MP2:
    case CODEC_ID_MP3:
        format = AFORMAT_MPEG;
        break;

    case CODEC_ID_AAC_LATM:
        format = AFORMAT_AAC_LATM;
        break;


    case CODEC_ID_AAC:
        format = AFORMAT_AAC;
        break;

    case CODEC_ID_AC3:
        format = AFORMAT_AC3;
        break;
    case CODEC_ID_EAC3:
        format = AFORMAT_EAC3;
        break;
    case CODEC_ID_DTS:
        format = AFORMAT_DTS;
        break;

    case CODEC_ID_PCM_S16BE:
        format = AFORMAT_PCM_S16BE;
        break;

    case CODEC_ID_PCM_S16LE:
        format = AFORMAT_PCM_S16LE;
        break;

    case CODEC_ID_PCM_U8:
        format = AFORMAT_PCM_U8;
        break;

    case CODEC_ID_COOK:
        format = AFORMAT_COOK;
        break;

    case CODEC_ID_ADPCM_IMA_WAV:
    case CODEC_ID_ADPCM_MS:
        format = AFORMAT_ADPCM;
        break;
    case CODEC_ID_AMR_NB:
    case CODEC_ID_AMR_WB:
        format =  AFORMAT_AMR;
        break;
    case CODEC_ID_WMAV1:
    case CODEC_ID_WMAV2:
        format =  AFORMAT_WMA;
        break;
    case CODEC_ID_FLAC:
        format = AFORMAT_FLAC;
        break;

    case CODEC_ID_WMAPRO:
        format = AFORMAT_WMAPRO;
        break;

    case CODEC_ID_PCM_BLURAY:
        format = AFORMAT_PCM_BLURAY;
        break;
    case CODEC_ID_ALAC:
        format = AFORMAT_ALAC;
        break;
    case CODEC_ID_VORBIS:
        format =    AFORMAT_VORBIS;
        break;
    case CODEC_ID_APE:
        format =    AFORMAT_APE;
        break;
    case CODEC_ID_PCM_WIFIDISPLAY:
        format = AFORMAT_PCM_WIFIDISPLAY;
        break;
    default:
        format = AFORMAT_UNSUPPORT;
        ALOGV("audio codec_id=0x%x\n", id);
    }
    ALOGV("[audioTypeConvert]audio codec_id=0x%x format=%d\n", id, format);

    return format;
}

#define INVALID_TS_PROG_NUM  -1
status_t AmNuPlayer::updateMediaInfo(void) {
    ALOGI("updateMediaInfo");
    maudio_info_t *ainfo;
    mvideo_info_t *vinfo;
    int cur_video_index = 0;
    int cur_audio_index = 0;
    mStreamInfo.stream_info.total_video_num = 0;
    mStreamInfo.stream_info.total_audio_num = 0;
    mStreamInfo.stream_info.cur_sub_index   = -1;
    mStreamInfo.is_multi_prog = 0;
    mStreamInfo.ts_programe_info.programe_num = 0;
    if (mSource->isStreaming()) {
        sp<AMessage> aformat= mSource->getFormat(true);  //audio
        sp<AMessage> vformat= mSource->getFormat(false);   // video
        if (vformat != NULL) {
            vinfo = (mvideo_info_t *)malloc(sizeof(mvideo_info_t));
            memset(vinfo, 0, sizeof(mvideo_info_t));
            int32_t codecid=-1,width=-1,height=-1,bitrate=-1;
            int64_t duration = -1;
            vinfo->index = 0;
            if (vformat->findInt32("codec-id", &codecid)) {
                vinfo->id = codecid;
            }
            if (vformat->findInt32("width", &width)) {
                vinfo->width = width;
            }
            if (vformat->findInt32("height", &height)) {
                vinfo->height = height;
            }
            if (vformat->findInt64("durationUs", &duration)) {
                 vinfo->duartion = duration;
            }
            if (vformat->findInt32("bit-rate", &bitrate)) {
                vinfo->bit_rate = bitrate;
            }
            vinfo->format  = (vformat_t)0;
            vinfo->aspect_ratio_num = 0;
            vinfo->aspect_ratio_den = 0;
            vinfo->frame_rate_num   = 0;
            vinfo->frame_rate_den   = 0;
            vinfo->video_rotation_degree = 0;
            mStreamInfo.video_info[mStreamInfo.stream_info.total_video_num] = vinfo;
            mStreamInfo.stream_info.total_video_num++;
        }
        if (aformat != NULL) {
            ainfo = (maudio_info_t *)malloc(sizeof(maudio_info_t));
            memset(ainfo, 0, sizeof(maudio_info_t));
            int32_t codecid=-1, bitrate=-1, samplerate=-1, channelcount=-1;
            int64_t duration=-1;
            ainfo->index = 0;
            AString mime;
            if (aformat->findInt32("codec-id", &codecid)) {
                ainfo->id = codecid;
            }
            if (aformat->findString("mime", &mime)) {
                if (mime == MEDIA_MIMETYPE_AUDIO_DTSHD) {
                    ALOGI("mime:%s",MEDIA_MIMETYPE_AUDIO_DTSHD);
                    mStrCurrentAudioCodec = "DTSHD";
                    ainfo->id = CODEC_ID_DTS;
                }
            }
            if (aformat->findInt32("bit-rate", &bitrate)) {
                ainfo->bit_rate = bitrate;
            }
            if (aformat->findInt32("channel-count", &channelcount)) {
                ainfo->channel = channelcount;
            }
            if (aformat->findInt32("sample-rate", &samplerate)) {
                ainfo->sample_rate = samplerate;
            }
            if (vformat->findInt64("durationUs", &duration)) {
                ainfo->duration = duration;
            }
            if (ainfo->id  > 0)
                ainfo->aformat      = audioTypeConvert((enum CodecID)ainfo->id);
            ALOGV("aformat %d",ainfo->aformat);
            mStreamInfo.audio_info[mStreamInfo.stream_info.total_audio_num] = ainfo;
            mStreamInfo.stream_info.total_audio_num++;

        }
        mStreamInfo.stream_info.cur_video_index = cur_video_index;
        mStreamInfo.stream_info.cur_audio_index = cur_audio_index;
        mStreamInfo.stream_info.cur_sub_index   = -1;
    }else {
        int64_t duration = 0;
        if (OK == mSource->getDuration(&duration)) {
            mStreamInfo.stream_info.duration = duration / 1000000; //us to sec
        }
        int track_num = mSource->getTrackCount();
        for (size_t i = 0; i < track_num; i++) {
            sp<AMessage> format = mSource->getTrackInfo(i);
            ALOGV("i: %d",i);
            AString mime;
            format->findString("mime", &mime);

            int32_t prog_num = INVALID_TS_PROG_NUM;
            if (format->findInt32("program-num", &prog_num)) {
                ALOGV("[%s:%d]get prognum=%d\n", __FUNCTION__, __LINE__, prog_num);
            }

            if (mime.startsWith("video/")) {
                vinfo = (mvideo_info_t *)malloc(sizeof(mvideo_info_t));
                memset(vinfo, 0, sizeof(mvideo_info_t));

                int32_t codecid,width,height,bitrate;
                int64_t duration;
                vinfo->index       = i;
                vinfo->id = i;
                if (format->findInt32("width", &width)) {
                    vinfo->width = width;
                }
                if (format->findInt32("height", &height)) {
                    vinfo->height = height;
                }
                if (format->findInt64("durationUs", &duration)) {
                     vinfo->duartion = duration;
                }
                if (format->findInt32("bit-rate", &bitrate)) {
                    vinfo->bit_rate = bitrate;
                }
                vinfo->format      = (vformat_t)0;
                vinfo->aspect_ratio_num = 0;
                vinfo->aspect_ratio_den = 0;
                vinfo->frame_rate_num   = 0;
                vinfo->frame_rate_den   = 0;
                vinfo->video_rotation_degree = 0;
                mStreamInfo.video_info[mStreamInfo.stream_info.total_video_num] = vinfo;
                if (INVALID_TS_PROG_NUM != prog_num) {
                    mStreamInfo.video_info[mStreamInfo.stream_info.total_video_num]->prog_num = prog_num;
                }
                mStreamInfo.stream_info.total_video_num++;
                if (i == mSource->getSelectedTrack(MEDIA_TRACK_TYPE_VIDEO)) {
                    cur_video_index = i;
                    mStreamInfo.ts_programe_info.cur_prognum = prog_num;
                }
                AString name;
                if (format->findString("program-name", &name)) {
                    ALOGV("program-name %s",name.c_str());
                    int num = mStreamInfo.ts_programe_info.programe_num;
                    mStreamInfo.ts_programe_info.ts_programe_detail[num].video_pid = i;
                    strcpy(mStreamInfo.ts_programe_info.ts_programe_detail[num].programe_name, name.c_str());
                    mStreamInfo.ts_programe_info.programe_num++;
                }
            } else if (mime.startsWith("audio/")) {
                ainfo = (maudio_info_t *)malloc(sizeof(maudio_info_t));
                memset(ainfo, 0, sizeof(maudio_info_t));
                int32_t codecid, bitrate, samplerate, channelcount;
                int64_t duration;
                ainfo->index     = i;
                if (format->findInt32("codec-id", &codecid)) {
                    ainfo->id = codecid;
                }
                if (format->findInt32("bit-rate", &bitrate)) {
                    ainfo->bit_rate = bitrate;
                }
                if (format->findInt32("channel-count", &channelcount)) {
                    ainfo->channel = channelcount;
                }
                if (format->findInt32("sample-rate", &samplerate)) {
                    ainfo->sample_rate = samplerate;
                }
                if (format->findInt64("durationUs", &duration)) {
                    ainfo->duration = duration;
                }
                ainfo->aformat      = audioTypeConvert((enum CodecID)ainfo->id);
                mStreamInfo.audio_info[mStreamInfo.stream_info.total_audio_num] = ainfo;
                if (INVALID_TS_PROG_NUM != prog_num) {
                    mStreamInfo.audio_info[mStreamInfo.stream_info.total_audio_num]->prog_num = prog_num;
                }
                mStreamInfo.stream_info.total_audio_num++;
                if (i == mSource->getSelectedTrack(MEDIA_TRACK_TYPE_AUDIO)) {
                    cur_audio_index = i;
                    if (mime == MEDIA_MIMETYPE_AUDIO_DTSHD) {
                        ALOGI("mime:%s",MEDIA_MIMETYPE_AUDIO_DTSHD);
                        mStrCurrentAudioCodec = "DTSHD";
                    } else {
                        mStrCurrentAudioCodec = NULL;
                    }
                }
            }
        }

        int prog_vnum_tmp = 0;
        if (mStreamInfo.ts_programe_info.programe_num > 1) {
            ALOGI("Multiple Programme\n");
            mStreamInfo.is_multi_prog  = 1;
        }

        for (int prog_cnt_tmp  = 0; prog_cnt_tmp < mStreamInfo.ts_programe_info.programe_num; prog_cnt_tmp++) {
            int prog_anum_tmp = 0;
            prog_vnum_tmp = mStreamInfo.video_info[prog_cnt_tmp]->prog_num;
            ALOGI("[%s:%d] video prog_num=%d,cnt=%d\n",__FUNCTION__, __LINE__, prog_vnum_tmp, prog_cnt_tmp);
            mStreamInfo.video_info[prog_cnt_tmp]->audio_info.prog_audio_num = 0;
            for (int audio_cnt_tmp = 0; audio_cnt_tmp < mStreamInfo.stream_info.total_audio_num; audio_cnt_tmp++) {
                prog_anum_tmp = mStreamInfo.audio_info[audio_cnt_tmp]->prog_num;
                if (prog_vnum_tmp == prog_anum_tmp) {
                    ALOGI("[%s:%d] audio prog_num=%d\n",__FUNCTION__, __LINE__, prog_anum_tmp);
                    ts_prog_audio_info *pinfo = &mStreamInfo.video_info[prog_cnt_tmp]->audio_info;
                    pinfo->prog_audio_index_list[pinfo->prog_audio_num] = audio_cnt_tmp;
                    pinfo->prog_audio_num ++;
                    mStreamInfo.audio_info[audio_cnt_tmp]->prog_video_index = prog_cnt_tmp;
                }
            }
        }

        mStreamInfo.stream_info.cur_video_index = cur_video_index;
        mStreamInfo.stream_info.cur_audio_index = cur_audio_index;
        mStreamInfo.stream_info.cur_sub_index   = -1;
    }

    return OK;

}

status_t AmNuPlayer::getMediaInfo(Parcel* reply){
    sp<AMessage> msg = new AMessage(kWhatGetMediaInfo, this);
    msg->setPointer("reply", reply);

    sp<AMessage> response;
    status_t err = msg->postAndAwaitResponse(&response);
    return err;
}


status_t AmNuPlayer::doGetMediaInfo(Parcel* reply){
    //Mutex::Autolock autoLock(mLock);
    ALOGI("AmNuPlayer::getMediaInfo");
    int datapos=reply->dataPosition();
    updateMediaInfo();
    //filename
    reply->writeString16(String16("-1"));
    //duration
    if (mStreamInfo.stream_info.duration > 0)
        reply->writeInt32(mStreamInfo.stream_info.duration);
    else
        reply->writeInt32(-1);

    reply->writeString16(String16("null"));

    //bitrate
      if (mStreamInfo.stream_info.bitrate > 0)
        reply->writeInt32(mStreamInfo.stream_info.bitrate);
    else
        reply->writeInt32(-1);

    //filetype
    reply->writeInt32(mStreamInfo.stream_info.type);

    /*select info*/
    reply->writeInt32(mStreamInfo.stream_info.cur_video_index);
    reply->writeInt32(mStreamInfo.stream_info.cur_audio_index);
    reply->writeInt32(mStreamInfo.stream_info.cur_sub_index);
    ALOGI("--cur video:%d cur audio:%d cur sub:%d \n",mStreamInfo.stream_info.cur_video_index,mStreamInfo.stream_info.cur_audio_index,mStreamInfo.stream_info.cur_sub_index);
    /*build video info*/
    reply->writeInt32(mStreamInfo.stream_info.total_video_num);
    for (int i = 0;i < mStreamInfo.stream_info.total_video_num; i ++) {
        reply->writeInt32(mStreamInfo.video_info[i]->index);
        reply->writeInt32(mStreamInfo.video_info[i]->id);
        reply->writeString16(String16("unknown"));
        reply->writeInt32(mStreamInfo.video_info[i]->width);
        reply->writeInt32(mStreamInfo.video_info[i]->height);
        ALOGI("--video index:%d id:%d totlanum:%d width:%d height:%d \n",mStreamInfo.video_info[i]->index,mStreamInfo.video_info[i]->id,mStreamInfo.stream_info.total_video_num,mStreamInfo.video_info[i]->width,mStreamInfo.video_info[i]->height);
    }

    /*build audio info*/
    if (mStreamInfo.is_multi_prog) {
        int ts_total_audio_num = 0;
        for (int i = 0; i < mStreamInfo.ts_programe_info.programe_num; i ++) {
            if (mStreamInfo.video_info[i]->prog_num == mStreamInfo.ts_programe_info.cur_prognum) {
                ts_total_audio_num = mStreamInfo.video_info[i]->audio_info.prog_audio_num;
                break;
            }
        }
        reply->writeInt32(ts_total_audio_num);
    }
    else {
        reply->writeInt32(mStreamInfo.stream_info.total_audio_num);
    }

    for (int i = 0; i < mStreamInfo.stream_info.total_audio_num; i ++) {
        if (mStreamInfo.is_multi_prog && mStreamInfo.ts_programe_info.cur_prognum != \
            mStreamInfo.audio_info[i]->prog_num) {
            continue;
        }

        reply->writeInt32(mStreamInfo.audio_info[i]->index);
        reply->writeInt32(mStreamInfo.audio_info[i]->id);
        reply->writeInt32(mStreamInfo.audio_info[i]->aformat);
        reply->writeInt32(mStreamInfo.audio_info[i]->channel);
        reply->writeInt32(mStreamInfo.audio_info[i]->sample_rate);
        ALOGI("--audio index:%d id:%d totlanum:%d channel:%d samplerate:%d aformat=%d\n",mStreamInfo.audio_info[i]->index,mStreamInfo.audio_info[i]->id,mStreamInfo.stream_info.total_audio_num,mStreamInfo.audio_info[i]->channel,mStreamInfo.audio_info[i]->sample_rate,mStreamInfo.audio_info[i]->aformat);
    }

    /*build subtitle info*/
    reply->writeInt32(0);
    reply->writeInt32(mStreamInfo.ts_programe_info.programe_num);
    for (int i = 0; i < mStreamInfo.ts_programe_info.programe_num; i++) {
        reply->writeInt32(mStreamInfo.ts_programe_info.ts_programe_detail[i].video_pid);
        reply->writeString16(String16(mStreamInfo.ts_programe_info.ts_programe_detail[i].programe_name));
        ALOGI("--programe i:%d, id:%d programe_name:%s\n", i,
                                mStreamInfo.ts_programe_info.ts_programe_detail[i].video_pid,
                                mStreamInfo.ts_programe_info.ts_programe_detail[i].programe_name);
    }
    reply->setDataPosition(datapos);
    return OK;
}



status_t AmNuPlayer::setParameter(int key , const Parcel &  request ) {
    if (KEY_PARAMETER_AML_PLAYER_SET_DTS_ASSET == key) {
        int ApreID =0,ApreAssetSel;
        const String16 uri16 = request.readString16();
        String8 keyStr = String8(uri16);
        ALOGI("setParameter %d=[%s]\n", key, keyStr.string());
        ApreID = getintfromString8(keyStr, "dtsApre:");
        ApreAssetSel=getintfromString8(keyStr, "dtsAsset:");
        if (ApreID >= 0 && ApreAssetSel >= 0) {
            dtsm6_set_exchange_info(&ApreID,&ApreAssetSel);
        }
    }else{
        ALOGI("unsupport setParameter value!=%d\n", key);
        return INVALID_OPERATION;
    }
    return OK;
}

status_t AmNuPlayer::getParameter(int key, Parcel *reply){
    if (key == KEY_PARAMETER_AML_PLAYER_GET_DTS_ASSET_TOTAL) {
        if (mSource == NULL)
            return INVALID_OPERATION;
        int32_t codecid;
        if (mStrCurrentAudioCodec != NULL && !strncmp(mStrCurrentAudioCodec,"DTS",3)) {
            int32_t ApresAssetsArray[32]={0};
            dtsm6_get_exchange_info(NULL,&DtshdApreTotal,NULL,NULL,ApresAssetsArray,NULL,NULL);
            reply->writeInt32(DtshdApreTotal);
            reply->writeInt32Array(32,ApresAssetsArray);
        }else{
            int32_t ApresAssetsArray[32]={0};
            reply->writeInt32(0);
            reply->writeInt32Array(32,ApresAssetsArray);
        }
    }else if (KEY_PARAMETER_AML_PLAYER_GET_MEDIA_INFO == key) {
        getMediaInfo(reply);
    }else if (key == KEY_PARAMETER_AML_PLAYER_TYPE_STR) {
        reply->writeString16(String16("AMNU_PLAYER"));
    }else {
        ALOGI("unsupport getParameter value!=%d\n", key);
    }
    return  OK;
}


void AmNuPlayer::setUID(uid_t uid) {
    mUIDValid = true;
    mUID = uid;
}

void AmNuPlayer::setDriver(const wp<AmNuPlayerDriver> &driver) {
    mDriver = driver;
}

void AmNuPlayer::setDataSourceAsync(const sp<IStreamSource> &source) {
    sp<AMessage> msg = new AMessage(kWhatSetDataSource, this);

    sp<AMessage> notify = new AMessage(kWhatSourceNotify, this);

    msg->setObject("source", new StreamingSource(notify, source));
    msg->post();
}

#define IS_LOCAL_HTTP(uri) (uri && (strcasestr(uri,"://127.0.0.1") || strcasestr(uri,"://localhost")))
static bool IsHTTPLiveURL(const char *url) {
    if (!strncasecmp("http://", url, 7)
            || !strncasecmp("https://", url, 8)
            || !strncasecmp("file://", url, 7)) {
            if (IS_LOCAL_HTTP(url) && strstr(url,"html") && !strstr(url,"m3u8")) {// not hls localhost
                return false;
            }
            return true;
#if 0
        size_t len = strlen(url);
        if (len >= 5 && !strcasecmp(".m3u8", &url[len - 5])) {
            return true;
        }

        if (strstr(url,"m3u8")) {
            return true;
        }
#endif
    }
    return false;
}

void AmNuPlayer::setDataSourceAsync(
        const sp<IMediaHTTPService> &httpService,
        const char *url,
        const KeyedVector<String8, String8> *headers) {

    sp<AMessage> msg = new AMessage(kWhatSetDataSource, this);
    size_t len = strlen(url);

    sp<AMessage> notify = new AMessage(kWhatSourceNotify, this);

    sp<Source> source;
    mEnableFrameRate = false;
    if (IsHTTPLiveURL(url)) {
        source = new HTTPLiveSource(notify, httpService, url, headers,interrupt_callback);

        // only enable auto frame-rate for HLS
        char value[PROPERTY_VALUE_MAX];
        if (property_get("media.hls.frame-rate", value, NULL)) {
            mEnableFrameRate = atoi(value);
        }

    } else if (!strncasecmp(url, "rtsp://", 7)) {
        source = new RTSPSource(
                notify, httpService, url, headers, mUIDValid, mUID);
    } else if ((!strncasecmp(url, "http://", 7)
                || !strncasecmp(url, "https://", 8))
                    && ((len >= 4 && !strcasecmp(".sdp", &url[len - 4]))
                    || strstr(url, ".sdp?"))) {
        source = new RTSPSource(
                notify, httpService, url, headers, mUIDValid, mUID, true);
    } else {
        sp<GenericSource> genericSource =
                new GenericSource(notify, mUIDValid, mUID);
        // Don't set FLAG_SECURE on mSourceFlags here for widevine.
        // The correct flags will be updated in Source::kWhatFlagsChanged
        // handler when  GenericSource is prepared.
        ALOGI("this is a http create a GenericSource! %s",url);
        status_t err = genericSource->setDataSource(httpService, url, headers);

        if (err == OK) {
            source = genericSource;
        } else {
            ALOGE("Failed to set data source!");
        }
    }

    msg->setObject("source", source);
    msg->post();
}

void AmNuPlayer::setDataSourceAsync(int fd, int64_t offset, int64_t length) {
    sp<AMessage> msg = new AMessage(kWhatSetDataSource, this);

    sp<AMessage> notify = new AMessage(kWhatSourceNotify, this);

    sp<GenericSource> source =
            new GenericSource(notify, mUIDValid, mUID);

    status_t err = source->setDataSource(fd, offset, length);

    if (err != OK) {
        ALOGE("Failed to set data source!");
        source = NULL;
    }

    msg->setObject("source", source);
    msg->post();
}

void AmNuPlayer::setDataSourceAsync(const sp<DataSource> &dataSource) {
    sp<AMessage> msg = new AMessage(kWhatSetDataSource, this);
    sp<AMessage> notify = new AMessage(kWhatSourceNotify, this);

    sp<GenericSource> source = new GenericSource(notify, mUIDValid, mUID);
    status_t err = source->setDataSource(dataSource);

    if (err != OK) {
        ALOGE("Failed to set data source!");
        source = NULL;
    }

    msg->setObject("source", source);
    msg->post();
}

void AmNuPlayer::prepareAsync() {
    (new AMessage(kWhatPrepare, this))->post();
}

void AmNuPlayer::setVideoSurfaceTextureAsync(
        const sp<IGraphicBufferProducer> &bufferProducer) {
    sp<AMessage> msg = new AMessage(kWhatSetVideoSurface, this);

    if (bufferProducer == NULL) {
        msg->setObject("surface", NULL);
    } else {
        msg->setObject("surface", new Surface(bufferProducer, true /* controlledByApp */));
    }

    msg->post();
}

void AmNuPlayer::setAudioSink(const sp<MediaPlayerBase::AudioSink> &sink) {
    sp<AMessage> msg = new AMessage(kWhatSetAudioSink, this);
    msg->setObject("sink", sink);
    msg->post();
}

void AmNuPlayer::start() {
    (new AMessage(kWhatStart, this))->post();
}

status_t AmNuPlayer::setPlaybackSettings(const AudioPlaybackRate &rate) {
    // do some cursory validation of the settings here. audio modes are
    // only validated when set on the audiosink.
     if ((rate.mSpeed != 0.f && rate.mSpeed < AUDIO_TIMESTRETCH_SPEED_MIN)
            || rate.mSpeed > AUDIO_TIMESTRETCH_SPEED_MAX
            || rate.mPitch < AUDIO_TIMESTRETCH_SPEED_MIN
            || rate.mPitch > AUDIO_TIMESTRETCH_SPEED_MAX) {
        return BAD_VALUE;
    }
    sp<AMessage> msg = new AMessage(kWhatConfigPlayback, this);
    writeToAMessage(msg, rate);
    sp<AMessage> response;
    status_t err = msg->postAndAwaitResponse(&response);
    if (err == OK && response != NULL) {
        CHECK(response->findInt32("err", &err));
    }
    return err;
}

status_t AmNuPlayer::getPlaybackSettings(AudioPlaybackRate *rate /* nonnull */) {
    sp<AMessage> msg = new AMessage(kWhatGetPlaybackSettings, this);
    sp<AMessage> response;
    status_t err = msg->postAndAwaitResponse(&response);
    if (err == OK && response != NULL) {
        CHECK(response->findInt32("err", &err));
        if (err == OK) {
            readFromAMessage(response, rate);
        }
    }
    return err;
}

status_t AmNuPlayer::setSyncSettings(const AVSyncSettings &sync, float videoFpsHint) {
    sp<AMessage> msg = new AMessage(kWhatConfigSync, this);
    writeToAMessage(msg, sync, videoFpsHint);
    sp<AMessage> response;
    status_t err = msg->postAndAwaitResponse(&response);
    if (err == OK && response != NULL) {
        CHECK(response->findInt32("err", &err));
    }
    return err;
}

status_t AmNuPlayer::getSyncSettings(
        AVSyncSettings *sync /* nonnull */, float *videoFps /* nonnull */) {
    sp<AMessage> msg = new AMessage(kWhatGetSyncSettings, this);
    sp<AMessage> response;
    status_t err = msg->postAndAwaitResponse(&response);
    if (err == OK && response != NULL) {
        CHECK(response->findInt32("err", &err));
        if (err == OK) {
            readFromAMessage(response, sync, videoFps);
        }
    }
    return err;
}

void AmNuPlayer::pause() {
    (new AMessage(kWhatPause, this))->post();
}

void AmNuPlayer::resetAsync() {
    sp<Source> source;
    {
        Mutex::Autolock autoLock(mSourceLock);
        source = mSource;
    }

    if (source != NULL) {
        // During a reset, the data source might be unresponsive already, we need to
        // disconnect explicitly so that reads exit promptly.
        // We can't queue the disconnect request to the looper, as it might be
        // queued behind a stuck read and never gets processed.
        // Doing a disconnect outside the looper to allows the pending reads to exit
        // (either successfully or with error).
        source->disconnect();
    }

    (new AMessage(kWhatReset, this))->post();
}

void AmNuPlayer::seekToAsync(int64_t seekTimeUs, bool needNotify) {
    sp<AMessage> msg = new AMessage(kWhatSeek, this);
    msg->setInt64("seekTimeUs", seekTimeUs);
    msg->setInt32("needNotify", needNotify);
    msg->post();
}


void AmNuPlayer::writeTrackInfo(
        Parcel* reply, const sp<AMessage> format) const {
    if (format == NULL) {
        ALOGE("NULL format");
        return;
    }
    int32_t trackType;
    if (!format->findInt32("type", &trackType)) {
        ALOGE("no track type");
        return;
    }

    AString mime;
    if (!format->findString("mime", &mime)) {
        // Java MediaPlayer only uses mimetype for subtitle and timedtext tracks.
        // If we can't find the mimetype here it means that we wouldn't be needing
        // the mimetype on the Java end. We still write a placeholder mime to keep the
        // (de)serialization logic simple.
        if (trackType == MEDIA_TRACK_TYPE_AUDIO) {
            mime = "audio/";
        } else if (trackType == MEDIA_TRACK_TYPE_VIDEO) {
            mime = "video/";
        } else {
            ALOGE("unknown track type: %d", trackType);
            return;
        }
    }

    AString lang;
    if (!format->findString("language", &lang)) {
        ALOGE("no language");
        return;
    }

    reply->writeInt32(2); // write something non-zero
    reply->writeInt32(trackType);
    reply->writeString16(String16(mime.c_str()));
    reply->writeString16(String16(lang.c_str()));

    if (trackType == MEDIA_TRACK_TYPE_SUBTITLE) {
        int32_t isAuto, isDefault, isForced;
        CHECK(format->findInt32("auto", &isAuto));
        CHECK(format->findInt32("default", &isDefault));
        CHECK(format->findInt32("forced", &isForced));

        reply->writeInt32(isAuto);
        reply->writeInt32(isDefault);
        reply->writeInt32(isForced);
    }
}

void AmNuPlayer::onMessageReceived(const sp<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatSetDataSource:
        {
            ALOGV("kWhatSetDataSource");

            CHECK(mSource == NULL);

            status_t err = OK;
            sp<RefBase> obj;
            CHECK(msg->findObject("source", &obj));
            if (obj != NULL) {
                Mutex::Autolock autoLock(mSourceLock);
                mSource = static_cast<Source *>(obj.get());
                mSelfThreadId = androidGetThreadId();
                mSource->setParentThreadId(mSelfThreadId);

            } else {
                err = UNKNOWN_ERROR;
            }

            CHECK(mDriver != NULL);
            sp<AmNuPlayerDriver> driver = mDriver.promote();
            if (driver != NULL) {
                driver->notifySetDataSourceCompleted(err);
            }
            break;
        }

        case kWhatPrepare:
        {
            mSource->prepareAsync();
            break;
        }

        case kWhatGetTrackInfo:
        {
            sp<AReplyToken> replyID;
            CHECK(msg->senderAwaitsResponse(&replyID));

            Parcel* reply;
            CHECK(msg->findPointer("reply", (void**)&reply));

            size_t inbandTracks = 0;
            if (mSource != NULL) {
                inbandTracks = mSource->getTrackCount();
            }

            size_t ccTracks = 0;
            if (mCCDecoder != NULL) {
                ccTracks = mCCDecoder->getTrackCount();
            }

            // total track count
            reply->writeInt32(inbandTracks + ccTracks);

            // write inband tracks
            for (size_t i = 0; i < inbandTracks; ++i) {
                writeTrackInfo(reply, mSource->getTrackInfo(i));
            }

            // write CC track
            for (size_t i = 0; i < ccTracks; ++i) {
                writeTrackInfo(reply, mCCDecoder->getTrackInfo(i));
            }

            sp<AMessage> response = new AMessage;
            response->postReply(replyID);
            break;
        }

        case kWhatGetSelectedTrack:
        {
            status_t err = INVALID_OPERATION;
            if (mSource != NULL) {
                err = OK;

                int32_t type32;
                CHECK(msg->findInt32("type", (int32_t*)&type32));
                media_track_type type = (media_track_type)type32;
                ssize_t selectedTrack = mSource->getSelectedTrack(type);

                Parcel* reply;
                CHECK(msg->findPointer("reply", (void**)&reply));
                reply->writeInt32(selectedTrack);
            }

            sp<AMessage> response = new AMessage;
            response->setInt32("err", err);

            sp<AReplyToken> replyID;
            CHECK(msg->senderAwaitsResponse(&replyID));
            response->postReply(replyID);
            break;
        }

        case kWhatSelectTrack:
        {
            sp<AReplyToken> replyID;
            CHECK(msg->senderAwaitsResponse(&replyID));

            size_t trackIndex;
            int32_t select;
            int64_t timeUs;
            CHECK(msg->findSize("trackIndex", &trackIndex));
            CHECK(msg->findInt32("select", &select));
            CHECK(msg->findInt64("timeUs", &timeUs));

            status_t err = INVALID_OPERATION;

            size_t inbandTracks = 0;
            if (mSource != NULL) {
                inbandTracks = mSource->getTrackCount();
            }
            size_t ccTracks = 0;
            if (mCCDecoder != NULL) {
                ccTracks = mCCDecoder->getTrackCount();
            }

            if (trackIndex < inbandTracks) {
                err = mSource->selectTrack(trackIndex, select, timeUs);

                if (!select && err == OK) {
                    int32_t type;
                    sp<AMessage> info = mSource->getTrackInfo(trackIndex);
                    if (info != NULL
                            && info->findInt32("type", &type)
                            && type == MEDIA_TRACK_TYPE_TIMEDTEXT) {
                        ++mTimedTextGeneration;
                    }
                }

                /* select the first relevant audio */
                sp<AMessage> info = mSource->getTrackInfo(trackIndex);
                if (info != NULL) {
                    AString mime;
                    if (info->findString("mime", &mime)) {
                        if (mime.startsWith("video/")) {
                           if (mStreamInfo.ts_programe_info.programe_num > 1)  {
                                size_t atrackIndex = -1;
                                for (int i = 0; i < mStreamInfo.ts_programe_info.programe_num; i++) {
                                    if (mStreamInfo.video_info[i]->index == trackIndex) {
                                        int index = mStreamInfo.video_info[i]->audio_info.prog_audio_index_list[0];
                                        atrackIndex = mStreamInfo.audio_info[index]->index;
                                        break;
                                    }
                                }
                                err = mSource->selectTrack(atrackIndex, select, timeUs);
                                if (!select && err == OK) {
                                    int32_t type;
                                    sp<AMessage> info = mSource->getTrackInfo(atrackIndex);
                                    if (info != NULL
                                            && info->findInt32("type", &type)
                                            && type == MEDIA_TRACK_TYPE_TIMEDTEXT) {
                                        ++mTimedTextGeneration;
                                    }
                                }
                            }
                        }
                    }
                }
            } else {
                trackIndex -= inbandTracks;

                if (trackIndex < ccTracks) {
                    err = mCCDecoder->selectTrack(trackIndex, select);
                }
            }

            sp<AMessage> response = new AMessage;
            response->setInt32("err", err);

            response->postReply(replyID);
            break;
        }

        case kWhatPollDuration:
        {
            int32_t generation;
            CHECK(msg->findInt32("generation", &generation));

            if (generation != mPollDurationGeneration) {
                // stale
                break;
            }

            int64_t durationUs;
            if (mDriver != NULL && mSource->getDuration(&durationUs) == OK) {
                sp<AmNuPlayerDriver> driver = mDriver.promote();
                if (driver != NULL) {
                    driver->notifyDuration(durationUs);
                }
            }

            msg->post(1000000ll);  // poll again in a second.
            break;
        }

        case kWhatSetVideoSurface:
        {

            sp<RefBase> obj;
            CHECK(msg->findObject("surface", &obj));
            sp<Surface> surface = static_cast<Surface *>(obj.get());

            ALOGD("onSetVideoSurface(%p, %s video decoder)",
                    surface.get(),
                    (mSource != NULL && mStarted && mSource->getFormat(false /* audio */) != NULL
                            && mVideoDecoder != NULL) ? "have" : "no");

            // Need to check mStarted before calling mSource->getFormat because AmNuPlayer might
            // be in preparing state and it could take long time.
            // When mStarted is true, mSource must have been set.
            if (mSource == NULL || !mStarted || mSource->getFormat(false /* audio */) == NULL
                    // NOTE: mVideoDecoder's mSurface is always non-null
                    || (mVideoDecoder != NULL && mVideoDecoder->setVideoSurface(surface) == OK)) {
                performSetSurface(surface);
                break;
            }

            mDeferredActions.push_back(
                    new FlushDecoderAction(FLUSH_CMD_FLUSH /* audio */,
                                           FLUSH_CMD_SHUTDOWN /* video */));

            mDeferredActions.push_back(new SetSurfaceAction(surface));

            if (obj == NULL) {
                // if surface is NULL, set hasVideoMedia false
                if (mRenderer != NULL) {
                    mRenderer->setHasNoMedia(false);
                }
            }

            if (obj != NULL || mAudioDecoder != NULL) {
                if (mStarted) {
                    // Issue a seek to refresh the video screen only if started otherwise
                    // the extractor may not yet be started and will assert.
                    // If the video decoder is not set (perhaps audio only in this case)
                    // do not perform a seek as it is not needed.
                    int64_t currentPositionUs = 0;
                    if (getCurrentPosition(&currentPositionUs) == OK) {
                        mDeferredActions.push_back(
                                new SeekAction(currentPositionUs));
                    }
                }

                // If there is a new surface texture, instantiate decoders
                // again if possible.
                mDeferredActions.push_back(
                        new SimpleAction(&AmNuPlayer::performScanSources));
            }

            // After a flush without shutdown, decoder is paused.
            // Don't resume it until source seek is done, otherwise it could
            // start pulling stale data too soon.
            mDeferredActions.push_back(
                    new ResumeDecoderAction(false /* needNotify */));

            processDeferredActions();
            break;
        }

        case kWhatSetAudioSink:
        {
            ALOGV("kWhatSetAudioSink");

            sp<RefBase> obj;
            CHECK(msg->findObject("sink", &obj));

            mAudioSink = static_cast<MediaPlayerBase::AudioSink *>(obj.get());
            break;
        }

        case kWhatStart:
        {
            ALOGV("kWhatStart");
            if (mStarted) {
                // do not resume yet if the source is still buffering
                if (!mPausedForBuffering) {
                    onResume();
                }
            } else {
                onStart();
            }
            mPausedByClient = false;
            break;
        }

        case kWhatConfigPlayback:
        {
            sp<AReplyToken> replyID;
            CHECK(msg->senderAwaitsResponse(&replyID));
            AudioPlaybackRate rate /* sanitized */;
            readFromAMessage(msg, &rate);
            status_t err = OK;
            if (mRenderer != NULL) {
                // AudioSink allows only 1.f and 0.f for offload mode.
                // For other speed, switch to non-offload mode.
                if (mOffloadAudio && ((rate.mSpeed != 0.f && rate.mSpeed != 1.f)
                        || rate.mPitch != 1.f)) {
                    int64_t currentPositionUs;
                    if (getCurrentPosition(&currentPositionUs) != OK) {
                        currentPositionUs = mPreviousSeekTimeUs;
                    }

                    // Set mPlaybackSettings so that the new audio decoder can
                    // be created correctly.
                    mPlaybackSettings = rate;
                    if (!mPaused) {
                        mRenderer->pause();
                    }
                    restartAudio(
                            currentPositionUs, true /* forceNonOffload */,
                            true /* needsToCreateAudioDecoder */);
                    if (!mPaused) {
                        mRenderer->resume();
                    }
                }

                err = mRenderer->setPlaybackSettings(rate);
            }
            if (err == OK) {
                if (rate.mSpeed == 0.f) {
                    onPause();
                    mPausedByClient = true;
                    // save all other settings (using non-paused speed)
                    // so we can restore them on start
                    AudioPlaybackRate newRate = rate;
                    newRate.mSpeed = mPlaybackSettings.mSpeed;
                    mPlaybackSettings = newRate;
                } else { /* rate.mSpeed != 0.f */
                    mPlaybackSettings = rate;
                    if (mStarted) {
                        // do not resume yet if the source is still buffering
                        if (!mPausedForBuffering) {
                            onResume();
                        }
                    } else if (mPrepared) {
                        onStart();
                    }

                    mPausedByClient = false;
                }
            }

            if (mVideoDecoder != NULL) {
                float rate = getFrameRate();
                if (rate > 0) {
                    sp<AMessage> params = new AMessage();
                    params->setFloat("operating-rate", rate * mPlaybackSettings.mSpeed);
                    mVideoDecoder->setParameters(params);
                }
            }

            sp<AMessage> response = new AMessage;
            response->setInt32("err", err);
            response->postReply(replyID);
            break;
        }

        case kWhatGetPlaybackSettings:
        {
            sp<AReplyToken> replyID;
            CHECK(msg->senderAwaitsResponse(&replyID));
            AudioPlaybackRate rate = mPlaybackSettings;
            status_t err = OK;
            if (mRenderer != NULL) {
                err = mRenderer->getPlaybackSettings(&rate);
            }
            if (err == OK) {
                // get playback settings used by renderer, as it may be
                // slightly off due to audiosink not taking small changes.
                mPlaybackSettings = rate;
                if (mPaused) {
                    rate.mSpeed = 0.f;
                }
            }
            sp<AMessage> response = new AMessage;
            if (err == OK) {
                writeToAMessage(response, rate);
            }
            response->setInt32("err", err);
            response->postReply(replyID);
            break;
        }

        case kWhatConfigSync:
        {
            sp<AReplyToken> replyID;
            CHECK(msg->senderAwaitsResponse(&replyID));

            ALOGV("kWhatConfigSync");
            AVSyncSettings sync;
            float videoFpsHint;
            readFromAMessage(msg, &sync, &videoFpsHint);
            status_t err = OK;
            if (mRenderer != NULL) {
                err = mRenderer->setSyncSettings(sync, videoFpsHint);
            }
            if (err == OK) {
                mSyncSettings = sync;
                mVideoFpsHint = videoFpsHint;
            }
            sp<AMessage> response = new AMessage;
            response->setInt32("err", err);
            response->postReply(replyID);
            break;
        }

        case kWhatGetSyncSettings:
        {
            sp<AReplyToken> replyID;
            CHECK(msg->senderAwaitsResponse(&replyID));
            AVSyncSettings sync = mSyncSettings;
            float videoFps = mVideoFpsHint;
            status_t err = OK;
            if (mRenderer != NULL) {
                err = mRenderer->getSyncSettings(&sync, &videoFps);
                if (err == OK) {
                    mSyncSettings = sync;
                    mVideoFpsHint = videoFps;
                }
            }
            sp<AMessage> response = new AMessage;
            if (err == OK) {
                writeToAMessage(response, sync, videoFps);
            }
            response->setInt32("err", err);
            response->postReply(replyID);
            break;
        }

        case kWhatScanSources:
        {
            int32_t generation;
            CHECK(msg->findInt32("generation", &generation));
            if (generation != mScanSourcesGeneration) {
                // Drop obsolete msg.
                break;
            }
            if (mEnableFrameRate && mFrameRate < 0.0) {
                int32_t num = 0;
                msg->findInt32("scan-num", &num);
                if (num < mWaitSeconds * 100) {     // wait up to 10 seconds
                    ALOGI("scan sources wait %d", num);
                    ++num;
                    msg->setInt32("scan-num", num);
                    msg->post(10 * 1000);
                    break;
                }
            }

            mScanSourcesPending = false;

            ALOGV("scanning sources haveAudio=%d, haveVideo=%d",
                 mAudioDecoder != NULL, mVideoDecoder != NULL);

            bool mHadAnySourcesBefore =
                (mAudioDecoder != NULL) || (mVideoDecoder != NULL);
            bool rescan = false;

            // initialize video before audio because successful initialization of
            // video may change deep buffer mode of audio.
            if (mSurface != NULL) {
                if (instantiateDecoder(false, &mVideoDecoder) == -EWOULDBLOCK) {
                    rescan = true;
                }
            }

            // Don't try to re-open audio sink if there's an existing decoder.
            if (mAudioSink != NULL && mAudioDecoder == NULL) {
                if (instantiateDecoder(true, &mAudioDecoder) == -EWOULDBLOCK) {
                    rescan = true;
                }
            }

            if (!mHadAnySourcesBefore
                    && (mAudioDecoder != NULL || mVideoDecoder != NULL)) {
                // This is the first time we've found anything playable.

                if (mSourceFlags & Source::FLAG_DYNAMIC_DURATION) {
                    schedulePollDuration();
                }
            }

            status_t err;
            if ((err = mSource->feedMoreTSData()) != OK) {
                if (mAudioDecoder == NULL && mVideoDecoder == NULL) {
                    // We're not currently decoding anything (no audio or
                    // video tracks found) and we just ran out of input data.

                    if (err == ERROR_END_OF_STREAM) {
                        notifyListener(MEDIA_PLAYBACK_COMPLETE, 0, 0);
                    } else {
                        notifyListener(MEDIA_ERROR, MEDIA_ERROR_UNKNOWN, err);
                    }
                }
                break;
            }

            if (rescan) {
                msg->post(100000ll);
                mScanSourcesPending = true;
            }
            break;
        }

        case kWhatVideoNotify:
        case kWhatAudioNotify:
        {
            bool audio = msg->what() == kWhatAudioNotify;

            int32_t currentDecoderGeneration =
                (audio? mAudioDecoderGeneration : mVideoDecoderGeneration);
            int32_t requesterGeneration = currentDecoderGeneration - 1;
            CHECK(msg->findInt32("generation", &requesterGeneration));

            if (requesterGeneration != currentDecoderGeneration) {
                ALOGV("got message from old %s decoder, generation(%d:%d)",
                        audio ? "audio" : "video", requesterGeneration,
                        currentDecoderGeneration);
                sp<AMessage> reply;
                if (!(msg->findMessage("reply", &reply))) {
                    return;
                }

                reply->setInt32("err", INFO_DISCONTINUITY);
                reply->post();
                return;
            }

            // restore the state of auto frame-rate after seek
            if (mEnableFrameRate && !audio) {
                amsysfs_set_sysfs_int("/sys/class/video/video_seek_flag", 0);
                amsysfs_set_sysfs_int("/sys/class/ionvideo/ionvideo_seek_flag", 0);
            }

            int32_t what;
            CHECK(msg->findInt32("what", &what));

            if (what == DecoderBase::kWhatInputDiscontinuity) {
                int32_t formatChange;
                CHECK(msg->findInt32("formatChange", &formatChange));

                ALOGI("%s discontinuity: formatChange %d",
                        audio ? "audio" : "video", formatChange);

                if (formatChange) {
                    mDeferredActions.push_back(
                            new FlushDecoderAction(
                                audio ? FLUSH_CMD_SHUTDOWN : FLUSH_CMD_NONE,
                                audio ? FLUSH_CMD_NONE : FLUSH_CMD_SHUTDOWN));
                }

                mDeferredActions.push_back(
                        new SimpleAction(
                                &AmNuPlayer::performScanSources));

                processDeferredActions();
            } else if (what == DecoderBase::kWhatEOS) {
                int32_t err;
                CHECK(msg->findInt32("err", &err));

                if (err == ERROR_END_OF_STREAM) {
                    ALOGI("got %s decoder EOS", audio ? "audio" : "video");
                } else {
                    ALOGI("got %s decoder EOS w/ error %d",
                         audio ? "audio" : "video",
                         err);
                }

                mRenderer->queueEOS(audio, err);
            } else if (what == DecoderBase::kWhatFlushCompleted) {
                ALOGI("decoder %s flush completed", audio ? "audio" : "video");

                handleFlushComplete(audio, true /* isDecoder */);
                finishFlushIfPossible();
            } else if (what == DecoderBase::kWhatVideoSizeChanged) {
                sp<AMessage> format;
                CHECK(msg->findMessage("format", &format));

                sp<AMessage> inputFormat =
                        mSource->getFormat(false /* audio */);

                updateVideoSize(inputFormat, format);
            } else if (what == DecoderBase::kWhatShutdownCompleted) {
                ALOGI("%s shutdown completed", audio ? "audio" : "video");
                if (audio) {
                    mAudioDecoder.clear();
                    ++mAudioDecoderGeneration;

                    CHECK_EQ((int)mFlushingAudio, (int)SHUTTING_DOWN_DECODER);
                    mFlushingAudio = SHUT_DOWN;
                } else {
                    mVideoDecoder.clear();
                    ++mVideoDecoderGeneration;

                    CHECK_EQ((int)mFlushingVideo, (int)SHUTTING_DOWN_DECODER);
                    mFlushingVideo = SHUT_DOWN;
                }

                finishFlushIfPossible();
            } else if (what == DecoderBase::kWhatResumeCompleted) {
                finishResume();
            } else if (what == DecoderBase::kWhatError) {
                status_t err;
                if (!msg->findInt32("err", &err) || err == OK) {
                    err = UNKNOWN_ERROR;
                }

                // Decoder errors can be due to Source (e.g. from streaming),
                // or from decoding corrupted bitstreams, or from other decoder
                // MediaCodec operations (e.g. from an ongoing reset or seek).
                // They may also be due to openAudioSink failure at
                // decoder start or after a format change.
                //
                // We try to gracefully shut down the affected decoder if possible,
                // rather than trying to force the shutdown with something
                // similar to performReset(). This method can lead to a hang
                // if MediaCodec functions block after an error, but they should
                // typically return INVALID_OPERATION instead of blocking.

                FlushStatus *flushing = audio ? &mFlushingAudio : &mFlushingVideo;
                ALOGE("received error(%#x) from %s decoder, flushing(%d), now shutting down",
                        err, audio ? "audio" : "video", *flushing);

                switch (*flushing) {
                    case NONE:
                        mDeferredActions.push_back(
                                new FlushDecoderAction(
                                    audio ? FLUSH_CMD_SHUTDOWN : FLUSH_CMD_NONE,
                                    audio ? FLUSH_CMD_NONE : FLUSH_CMD_SHUTDOWN));
                        processDeferredActions();
                        break;
                    case FLUSHING_DECODER:
                        *flushing = FLUSHING_DECODER_SHUTDOWN; // initiate shutdown after flush.
                        break; // Wait for flush to complete.
                    case FLUSHING_DECODER_SHUTDOWN:
                        break; // Wait for flush to complete.
                    case SHUTTING_DOWN_DECODER:
                        break; // Wait for shutdown to complete.
                    case FLUSHED:
                        // Widevine source reads must stop before releasing the video decoder.
                        if (!audio && mSource != NULL && mSourceFlags & Source::FLAG_SECURE) {
                            mSource->stop();
                            mSourceStarted = false;
                        }
                        getDecoder(audio)->initiateShutdown(); // In the middle of a seek.
                        *flushing = SHUTTING_DOWN_DECODER;     // Shut down.
                        break;
                    case SHUT_DOWN:
                        finishFlushIfPossible();  // Should not occur.
                        break;                    // Finish anyways.
                }
                notifyListener(MEDIA_ERROR, MEDIA_ERROR_UNKNOWN, err);
            } else {
                ALOGE("Unhandled decoder notification %d '%c%c%c%c'.",
                      what,
                      what >> 24,
                      (what >> 16) & 0xff,
                      (what >> 8) & 0xff,
                      what & 0xff);
            }

            break;
        }

        case kWhatRendererNotify:
        {
            int32_t requesterGeneration = mRendererGeneration - 1;
            CHECK(msg->findInt32("generation", &requesterGeneration));
            if (requesterGeneration != mRendererGeneration) {
                ALOGV("got message from old renderer, generation(%d:%d)",
                        requesterGeneration, mRendererGeneration);
                return;
            }

            int32_t what;
            CHECK(msg->findInt32("what", &what));

            if (what == Renderer::kWhatEOS) {
                int32_t audio;
                CHECK(msg->findInt32("audio", &audio));

                int32_t finalResult;
                CHECK(msg->findInt32("finalResult", &finalResult));

                if (audio) {
                    mAudioEOS = true;
                } else {
                    mVideoEOS = true;
                }

                if (finalResult == ERROR_END_OF_STREAM) {
                    ALOGV("reached %s EOS", audio ? "audio" : "video");
                } else {
                    ALOGE("%s track encountered an error (%d)",
                         audio ? "audio" : "video", finalResult);

                    notifyListener(
                            MEDIA_ERROR, MEDIA_ERROR_UNKNOWN, finalResult);
                }

                if ((mAudioEOS || mAudioDecoder == NULL)
                        && (mVideoEOS || mVideoDecoder == NULL)) {
                    notifyListener(MEDIA_PLAYBACK_COMPLETE, 0, 0);
                }
            } else if (what == Renderer::kWhatFlushComplete) {
                int32_t audio;
                CHECK(msg->findInt32("audio", &audio));

                if (audio) {
                    mAudioEOS = false;
                } else {
                    mVideoEOS = false;
                }

                ALOGI("renderer %s flush completed.", audio ? "audio" : "video");
                if (audio && (mFlushingAudio == NONE || mFlushingAudio == FLUSHED
                        || mFlushingAudio == SHUT_DOWN)) {
                    // Flush has been handled by tear down.
                    break;
                }
                handleFlushComplete(audio, false /* isDecoder */);
                finishFlushIfPossible();
            } else if (what == Renderer::kWhatVideoRenderingStart) {
                notifyListener(MEDIA_INFO, MEDIA_INFO_RENDERING_START, 0);
            } else if (what == Renderer::kWhatMediaRenderingStart) {
                ALOGV("media rendering started");
                notifyListener(MEDIA_STARTED, 0, 0);
            } else if (what == Renderer::kWhatAudioTearDown) {
                int32_t reason;
                CHECK(msg->findInt32("reason", &reason));
                ALOGV("Tear down audio with reason %d.", reason);
                if (reason == Renderer::kDueToTimeout && !(mPaused && mOffloadAudio)) {
                    // TimeoutWhenPaused is only for offload mode.
                    ALOGW("Receive a stale message for teardown.");
                    break;
                }
                int64_t positionUs;
                if (!msg->findInt64("positionUs", &positionUs)) {
                    positionUs = mPreviousSeekTimeUs;
                }

                restartAudio(
                        positionUs, reason == Renderer::kForceNonOffload /* forceNonOffload */,
                        reason != Renderer::kDueToTimeout /* needsToCreateAudioDecoder */);
            }
            break;
        }

        case kWhatMoreDataQueued:
        {
            break;
        }

        case kWhatReset:
        {
            ALOGI("kWhatReset");

            mResetting = true;

            mDeferredActions.push_back(
                    new FlushDecoderAction(
                        FLUSH_CMD_SHUTDOWN /* audio */,
                        FLUSH_CMD_SHUTDOWN /* video */));

            mDeferredActions.push_back(
                    new SimpleAction(&AmNuPlayer::performReset));

            processDeferredActions();
            break;
        }

        case kWhatSeek:
        {
            int64_t seekTimeUs;
            int32_t needNotify;
            CHECK(msg->findInt64("seekTimeUs", &seekTimeUs));
            CHECK(msg->findInt32("needNotify", &needNotify));

            ALOGI("kWhatSeek seekTimeUs=%lld us, needNotify=%d",
                    (long long)seekTimeUs, needNotify);

            // temporarily close auto frame-rate to avoid black srceen when seek
            if (mEnableFrameRate && mFrameRate > 0.0) {
                int64_t nowUs = ALooper::GetNowUs();
                int64_t timeSinceStart = nowUs - mStartTimeUs;
                if (timeSinceStart > 100000) {
                    amsysfs_set_sysfs_int("/sys/class/video/video_seek_flag", 1);
                    amsysfs_set_sysfs_int("/sys/class/ionvideo/ionvideo_seek_flag", 1);
                }
            }

            if (!mStarted) {
                // Seek before the player is started. In order to preview video,
                // need to start the player and pause it. This branch is called
                // only once if needed. After the player is started, any seek
                // operation will go through normal path.
                // Audio-only cases are handled separately.
                onStart(seekTimeUs);
                if (mStarted) {
                    onPause();
                    mPausedByClient = true;
                }
                if (needNotify) {
                    notifyDriverSeekComplete();
                }
                break;
            }

            mDeferredActions.push_back(
                    new FlushDecoderAction(FLUSH_CMD_FLUSH /* audio */,
                                           FLUSH_CMD_FLUSH /* video */));

            mDeferredActions.push_back(
                    new SeekAction(seekTimeUs));

            // After a flush without shutdown, decoder is paused.
            // Don't resume it until source seek is done, otherwise it could
            // start pulling stale data too soon.
            mDeferredActions.push_back(
                    new ResumeDecoderAction(needNotify));

            processDeferredActions();
            break;
        }

        case kWhatPause:
        {
            onPause();
            mPausedByClient = true;
            break;
        }

        case kWhatSourceNotify:
        {
            onSourceNotify(msg);
            break;
        }

        case kWhatClosedCaptionNotify:
        {
            onClosedCaptionNotify(msg);
            break;
        }

        case kWhatGetMediaInfo:
        {
            sp<AReplyToken> replyID;
            CHECK(msg->senderAwaitsResponse(&replyID));

            Parcel* reply;
            CHECK(msg->findPointer("reply", (void**)&reply));

            doGetMediaInfo(reply);

            sp<AMessage> response = new AMessage;
            response->postReply(replyID);
            break;
        }

        default:
            TRESPASS();
            break;
    }
}

void AmNuPlayer::onResume() {
    if (!mPaused || mResetting) {
        ALOGD_IF(mResetting, "resetting, onResume discarded");
        return;
    }
    mPaused = false;
    if (mSource != NULL) {
        mSource->resume();
    } else {
        ALOGW("resume called when source is gone or not set");
    }
    // |mAudioDecoder| may have been released due to the pause timeout, so re-create it if
    // needed.
    if (audioDecoderStillNeeded() && mAudioDecoder == NULL) {
        instantiateDecoder(true /* audio */, &mAudioDecoder);
    }
    if (mRenderer != NULL) {
        mRenderer->resume();
    } else {
        ALOGW("resume called when renderer is gone or not set");
    }
}

status_t AmNuPlayer::onInstantiateSecureDecoders() {
    status_t err;
    if (!(mSourceFlags & Source::FLAG_SECURE)) {
        return BAD_TYPE;
    }

    if (mRenderer != NULL) {
        ALOGE("renderer should not be set when instantiating secure decoders");
        return UNKNOWN_ERROR;
    }

    // TRICKY: We rely on mRenderer being null, so that decoder does not start requesting
    // data on instantiation.
    if (mSurface != NULL) {
        err = instantiateDecoder(false, &mVideoDecoder);
        if (err != OK) {
            return err;
        }
    }

    if (mAudioSink != NULL) {
        err = instantiateDecoder(true, &mAudioDecoder);
        if (err != OK) {
            return err;
        }
    }
    return OK;
}

void AmNuPlayer::onStart(int64_t startPositionUs) {
    if (!mSourceStarted) {
        mSourceStarted = true;
        mSource->start();
    }
    if (startPositionUs > 0) {
        performSeek(startPositionUs);
        if (mSource->getFormat(false /* audio */) == NULL) {
            return;
        }
    }

    mOffloadAudio = false;
    mAudioEOS = false;
    mVideoEOS = false;
    mStarted = true;
    mPaused = false;

    uint32_t flags = 0;

    if (mSource->isRealTime()) {
        flags |= Renderer::FLAG_REAL_TIME;
    }

    sp<MetaData> audioMeta = mSource->getFormatMeta(true /* audio */);
    ALOGV_IF(audioMeta == NULL, "no metadata for audio source");  // video only stream
    audio_stream_type_t streamType = AUDIO_STREAM_MUSIC;
    if (mAudioSink != NULL) {
        streamType = mAudioSink->getAudioStreamType();
    }

    sp<AMessage> videoFormat = mSource->getFormat(false /* audio */);

    mOffloadAudio =
        canOffloadStream(audioMeta, (videoFormat != NULL), mSource->isStreaming(), streamType)
                && (mPlaybackSettings.mSpeed == 1.f && mPlaybackSettings.mPitch == 1.f);
    if (mOffloadAudio) {
        flags |= Renderer::FLAG_OFFLOAD_AUDIO;
    }

    sp<AMessage> notify = new AMessage(kWhatRendererNotify, this);
    ++mRendererGeneration;
    notify->setInt32("generation", mRendererGeneration);
    mRenderer = new Renderer(mAudioSink, notify, flags);
    mRendererLooper = new ALooper;
    mRendererLooper->setName("NuPlayerRenderer");
    mRendererLooper->start(false, false, ANDROID_PRIORITY_AUDIO);
    mRendererLooper->registerHandler(mRenderer);

    status_t err = mRenderer->setPlaybackSettings(mPlaybackSettings);
    if (err != OK) {
        mSource->stop();
        mSourceStarted = false;
        notifyListener(MEDIA_ERROR, MEDIA_ERROR_UNKNOWN, err);
        return;
    }

    float rate = getFrameRate();
    if (rate > 0) {
        mRenderer->setVideoFrameRate(rate);
    }

    if (mVideoDecoder != NULL) {
        mRenderer->setHasMedia(false);
        mVideoDecoder->setRenderer(mRenderer);
    }
    if (mAudioDecoder != NULL) {
        mRenderer->setHasMedia(true);
        mAudioDecoder->setRenderer(mRenderer);
    }

    mStartTimeUs = ALooper::GetNowUs();

    postScanSources();
}

void AmNuPlayer::onPause() {
    if (mPaused) {
        return;
    }
    mPaused = true;
    if (mSource != NULL) {
        mSource->pause();
    } else {
        ALOGW("pause called when source is gone or not set");
    }
    if (mRenderer != NULL) {
        mRenderer->pause();
    } else {
        ALOGW("pause called when renderer is gone or not set");
    }
}

bool AmNuPlayer::audioDecoderStillNeeded() {
    // Audio decoder is no longer needed if it's in shut/shutting down status.
    return ((mFlushingAudio != SHUT_DOWN) && (mFlushingAudio != SHUTTING_DOWN_DECODER));
}

void AmNuPlayer::handleFlushComplete(bool audio, bool isDecoder) {
    // We wait for both the decoder flush and the renderer flush to complete
    // before entering either the FLUSHED or the SHUTTING_DOWN_DECODER state.

    mFlushComplete[audio][isDecoder] = true;
    if (!mFlushComplete[audio][!isDecoder]) {
        return;
    }

    FlushStatus *state = audio ? &mFlushingAudio : &mFlushingVideo;
    switch (*state) {
        case FLUSHING_DECODER:
        {
            *state = FLUSHED;
            break;
        }

        case FLUSHING_DECODER_SHUTDOWN:
        {
            *state = SHUTTING_DOWN_DECODER;

            ALOGV("initiating %s decoder shutdown", audio ? "audio" : "video");
            if (!audio) {
                // Widevine source reads must stop before releasing the video decoder.
                if (mSource != NULL && mSourceFlags & Source::FLAG_SECURE) {
                    mSource->stop();
                    mSourceStarted = false;
                }
            }
            getDecoder(audio)->initiateShutdown();
            break;
        }

        default:
            // decoder flush completes only occur in a flushing state.
            LOG_ALWAYS_FATAL_IF(isDecoder, "decoder flush in invalid state %d", *state);
            break;
    }
}

void AmNuPlayer::finishFlushIfPossible() {
    if (mFlushingAudio != NONE && mFlushingAudio != FLUSHED
            && mFlushingAudio != SHUT_DOWN) {
        return;
    }

    if (mFlushingVideo != NONE && mFlushingVideo != FLUSHED
            && mFlushingVideo != SHUT_DOWN) {
        return;
    }

    ALOGV("both audio and video are flushed now.");

    mFlushingAudio = NONE;
    mFlushingVideo = NONE;

    clearFlushComplete();

    processDeferredActions();
}

void AmNuPlayer::postScanSources() {
    if (mScanSourcesPending) {
        return;
    }

    sp<AMessage> msg = new AMessage(kWhatScanSources, this);
    msg->setInt32("generation", mScanSourcesGeneration);
    msg->post();

    mScanSourcesPending = true;
}

void AmNuPlayer::tryOpenAudioSinkForOffload(
        const sp<AMessage> &format, const sp<MetaData> &audioMeta, bool hasVideo) {
    // Note: This is called early in AmNuPlayer to determine whether offloading
    // is possible; otherwise the decoders call the renderer openAudioSink directly.

    status_t err = mRenderer->openAudioSink(
            format, true /* offloadOnly */, hasVideo, AUDIO_OUTPUT_FLAG_NONE, &mOffloadAudio);
    if (err != OK) {
        // Any failure we turn off mOffloadAudio.
        mOffloadAudio = false;
    } else if (mOffloadAudio) {
        sendMetaDataToHal(mAudioSink, audioMeta);
    }
}

void AmNuPlayer::closeAudioSink() {
    mRenderer->closeAudioSink();
}

void AmNuPlayer::restartAudio(
        int64_t currentPositionUs, bool forceNonOffload, bool needsToCreateAudioDecoder) {
    if (mAudioDecoder != NULL) {
        mAudioDecoder->pause();
        mAudioDecoder.clear();
        ++mAudioDecoderGeneration;
    }
    if (mFlushingAudio == FLUSHING_DECODER) {
        mFlushComplete[1 /* audio */][1 /* isDecoder */] = true;
        mFlushingAudio = FLUSHED;
        finishFlushIfPossible();
    } else if (mFlushingAudio == FLUSHING_DECODER_SHUTDOWN
            || mFlushingAudio == SHUTTING_DOWN_DECODER) {
        mFlushComplete[1 /* audio */][1 /* isDecoder */] = true;
        mFlushingAudio = SHUT_DOWN;
        finishFlushIfPossible();
        needsToCreateAudioDecoder = false;
    }
    if (mRenderer == NULL) {
        return;
    }
    closeAudioSink();
    mRenderer->flush(true /* audio */, false /* notifyComplete */);
    if (mVideoDecoder != NULL) {
        mRenderer->flush(false /* audio */, false /* notifyComplete */);
    }

    performSeek(currentPositionUs);

    if (forceNonOffload) {
        mRenderer->signalDisableOffloadAudio();
        mOffloadAudio = false;
    }
    if (needsToCreateAudioDecoder) {
        instantiateDecoder(true /* audio */, &mAudioDecoder, !forceNonOffload);
    }
}

void AmNuPlayer::determineAudioModeChange(const sp<AMessage> &audioFormat) {
    if (mSource == NULL || mAudioSink == NULL) {
        return;
    }

    if (mRenderer == NULL) {
        ALOGW("No renderer can be used to determine audio mode. Use non-offload for safety.");
        mOffloadAudio = false;
        return;
    }

    sp<MetaData> audioMeta = mSource->getFormatMeta(true /* audio */);
    sp<AMessage> videoFormat = mSource->getFormat(false /* audio */);
    audio_stream_type_t streamType = mAudioSink->getAudioStreamType();
    const bool hasVideo = (videoFormat != NULL);
    const bool canOffload = canOffloadStream(
            audioMeta, hasVideo, mSource->isStreaming(), streamType)
                    && (mPlaybackSettings.mSpeed == 1.f && mPlaybackSettings.mPitch == 1.f);
    if (canOffload) {
        if (!mOffloadAudio) {
            mRenderer->signalEnableOffloadAudio();
        }
        // open audio sink early under offload mode.
        tryOpenAudioSinkForOffload(audioFormat, audioMeta, hasVideo);
    } else {
        if (mOffloadAudio) {
            mRenderer->signalDisableOffloadAudio();
            mOffloadAudio = false;
        }
    }
}

status_t AmNuPlayer::instantiateDecoder(
        bool audio, sp<DecoderBase> *decoder, bool checkAudioModeChange) {
    // The audio decoder could be cleared by tear down. If still in shut down
    // process, no need to create a new audio decoder.
    if (*decoder != NULL || (audio && mFlushingAudio == SHUT_DOWN)) {
        return OK;
    }

    sp<AMessage> format = mSource->getFormat(audio);

    if (format == NULL) {
        return UNKNOWN_ERROR;
    } else {
        status_t err;
        if (format->findInt32("err", &err) && err) {
            return err;
        }
    }

    format->setInt32("priority", 0 /* realtime */);

    if (!audio) {
        AString mime;
        CHECK(format->findString("mime", &mime));

        sp<AMessage> ccNotify = new AMessage(kWhatClosedCaptionNotify, this);
        if (mCCDecoder == NULL) {
            mCCDecoder = new CCDecoder(ccNotify);
        }

        if (mSourceFlags & Source::FLAG_SECURE) {
            format->setInt32("secure", true);
        }

        if (mSourceFlags & Source::FLAG_PROTECTED) {
            format->setInt32("protected", true);
        }

        float rate = getFrameRate();
        if (rate > 0) {
            format->setFloat("operating-rate", rate * mPlaybackSettings.mSpeed);
        }
    }

    if (audio) {
        sp<AMessage> notify = new AMessage(kWhatAudioNotify, this);
        ++mAudioDecoderGeneration;
        notify->setInt32("generation", mAudioDecoderGeneration);

        if (checkAudioModeChange) {
            determineAudioModeChange(format);
        }
        if (mOffloadAudio) {
            mSource->setOffloadAudio(true /* offload */);

            const bool hasVideo = (mSource->getFormat(false /*audio */) != NULL);
            format->setInt32("has-video", hasVideo);
            *decoder = new DecoderPassThrough(notify, mSource, mRenderer);
        } else {
            mSource->setOffloadAudio(false /* offload */);

            *decoder = new Decoder(notify, mSource, mPID, mRenderer);
        }
        mRenderer->setHasMedia(audio);
    } else {
        sp<AMessage> notify = new AMessage(kWhatVideoNotify, this);
        ++mVideoDecoderGeneration;
        notify->setInt32("generation", mVideoDecoderGeneration);

        format->setFloat("frame-rate", mFrameRate);
        ALOGI("set frame-rate %.2f",mFrameRate);

        if (mDecodecParam > 0)
            format->setInt32("decodec-param",mDecodecParam); // 0x40

        *decoder = new Decoder(
                notify, mSource, mPID, mRenderer, mSurface, mCCDecoder);
        mRenderer->setHasMedia(audio);
        // enable FRC if high-quality AV sync is requested, even if not
        // directly queuing to display, as this will even improve textureview
        // playback.
        {
            char value[PROPERTY_VALUE_MAX];
            if (property_get("persist.sys.media.avsync", value, NULL) &&
                    (!strcmp("1", value) || !strcasecmp("true", value))) {
                format->setInt32("auto-frc", 1);
            }
        }
    }
    (*decoder)->init();
    (*decoder)->configure(format);

    // allocate buffers to decrypt widevine source buffers
    if (!audio && (mSourceFlags & Source::FLAG_SECURE)) {
        Vector<sp<ABuffer> > inputBufs;
        CHECK_EQ((*decoder)->getInputBuffers(&inputBufs), (status_t)OK);

        Vector<MediaBuffer *> mediaBufs;
        for (size_t i = 0; i < inputBufs.size(); i++) {
            const sp<ABuffer> &buffer = inputBufs[i];
            MediaBuffer *mbuf = new MediaBuffer(buffer->data(), buffer->size());
            mediaBufs.push(mbuf);
        }

        status_t err = mSource->setBuffers(audio, mediaBufs);
        if (err != OK) {
            for (size_t i = 0; i < mediaBufs.size(); ++i) {
                mediaBufs[i]->release();
            }
            mediaBufs.clear();
            ALOGE("Secure source didn't support secure mediaBufs.");
            return err;
        }
    }
    return OK;
}

void AmNuPlayer::updateVideoSize(
        const sp<AMessage> &inputFormat,
        const sp<AMessage> &outputFormat) {
    if (inputFormat == NULL) {
        ALOGW("Unknown video size, reporting 0x0!");
        notifyListener(MEDIA_SET_VIDEO_SIZE, 0, 0);
        return;
    }

    int32_t displayWidth, displayHeight;
    if (outputFormat != NULL) {
        int32_t width, height;
        CHECK(outputFormat->findInt32("width", &width));
        CHECK(outputFormat->findInt32("height", &height));

        int32_t cropLeft, cropTop, cropRight, cropBottom;
        CHECK(outputFormat->findRect(
                    "crop",
                    &cropLeft, &cropTop, &cropRight, &cropBottom));

        displayWidth = cropRight - cropLeft + 1;
        displayHeight = cropBottom - cropTop + 1;

        ALOGV("Video output format changed to %d x %d "
             "(crop: %d x %d @ (%d, %d))",
             width, height,
             displayWidth,
             displayHeight,
             cropLeft, cropTop);
    } else {
        CHECK(inputFormat->findInt32("width", &displayWidth));
        CHECK(inputFormat->findInt32("height", &displayHeight));

        ALOGV("Video input format %d x %d", displayWidth, displayHeight);
    }

    // Take into account sample aspect ratio if necessary:
    int32_t sarWidth, sarHeight;
    if (inputFormat->findInt32("sar-width", &sarWidth)
            && inputFormat->findInt32("sar-height", &sarHeight)) {
        ALOGV("Sample aspect ratio %d : %d", sarWidth, sarHeight);

        displayWidth = (displayWidth * sarWidth) / sarHeight;

        ALOGV("display dimensions %d x %d", displayWidth, displayHeight);
    }

    int32_t rotationDegrees;
    if (!inputFormat->findInt32("rotation-degrees", &rotationDegrees)) {
        rotationDegrees = 0;
    }

    if (rotationDegrees == 90 || rotationDegrees == 270) {
        int32_t tmp = displayWidth;
        displayWidth = displayHeight;
        displayHeight = tmp;
    }

    notifyListener(
            MEDIA_SET_VIDEO_SIZE,
            displayWidth,
            displayHeight);
}

void AmNuPlayer::notifyListener(int msg, int ext1, int ext2, const Parcel *in) {
    if (mDriver == NULL) {
        return;
    }

    sp<AmNuPlayerDriver> driver = mDriver.promote();

    if (driver == NULL) {
        return;
    }

    driver->notifyListener(msg, ext1, ext2, in);
}

void AmNuPlayer::flushDecoder(bool audio, bool needShutdown) {
    ALOGV("[%s] flushDecoder needShutdown=%d",
          audio ? "audio" : "video", needShutdown);

    const sp<DecoderBase> &decoder = getDecoder(audio);
    if (decoder == NULL) {
        ALOGI("flushDecoder %s without decoder present",
             audio ? "audio" : "video");
        return;
    }

    // Make sure we don't continue to scan sources until we finish flushing.
    ++mScanSourcesGeneration;
    if (mScanSourcesPending) {
        mDeferredActions.push_back(
                new SimpleAction(&AmNuPlayer::performScanSources));
        mScanSourcesPending = false;
    }

    decoder->signalFlush();

    FlushStatus newStatus =
        needShutdown ? FLUSHING_DECODER_SHUTDOWN : FLUSHING_DECODER;

    mFlushComplete[audio][false /* isDecoder */] = (mRenderer == NULL);
    mFlushComplete[audio][true /* isDecoder */] = false;
    if (audio) {
        ALOGE_IF(mFlushingAudio != NONE,
                "audio flushDecoder() is called in state %d", mFlushingAudio);
        mFlushingAudio = newStatus;
    } else {
        ALOGE_IF(mFlushingVideo != NONE,
                "video flushDecoder() is called in state %d", mFlushingVideo);
        mFlushingVideo = newStatus;
    }
}

void AmNuPlayer::queueDecoderShutdown(
        bool audio, bool video, const sp<AMessage> &reply) {
    ALOGI("queueDecoderShutdown audio=%d, video=%d", audio, video);

    mDeferredActions.push_back(
            new FlushDecoderAction(
                audio ? FLUSH_CMD_SHUTDOWN : FLUSH_CMD_NONE,
                video ? FLUSH_CMD_SHUTDOWN : FLUSH_CMD_NONE));

    mDeferredActions.push_back(
            new SimpleAction(&AmNuPlayer::performScanSources));

    mDeferredActions.push_back(new PostMessageAction(reply));

    processDeferredActions();
}

status_t AmNuPlayer::setVideoScalingMode(int32_t mode) {
    mVideoScalingMode = mode;
    if (mSurface != NULL) {
        status_t ret = native_window_set_scaling_mode(mSurface.get(), mVideoScalingMode);
        if (ret != OK) {
            ALOGE("Failed to set scaling mode (%d): %s",
                -ret, strerror(-ret));
            return ret;
        }

        int videoscreenmode = 1; //0.normal mode,1.full,2.4:3,3,16:9
        switch (mode) {
            case NATIVE_WINDOW_SCALING_MODE_FREEZE:
                videoscreenmode = 0;
                break;
            case NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW:
                videoscreenmode = 1; ///full strecch
                break;
            case NATIVE_WINDOW_SCALING_MODE_SCALE_CROP:
            case NATIVE_WINDOW_SCALING_MODE_NO_SCALE_CROP:
            default:
                videoscreenmode = 0; ///normal mode
        }
#ifndef UNUSE_SCREEN_MODE
        ALOGI("set video screenmode to %d\n", videoscreenmode);
        amsysfs_set_sysfs_int("/sys/class/video/screen_mode", videoscreenmode);
#endif
    }
    return OK;
}

status_t AmNuPlayer::getTrackInfo(Parcel* reply) const {
    sp<AMessage> msg = new AMessage(kWhatGetTrackInfo, this);
    msg->setPointer("reply", reply);

    sp<AMessage> response;
    status_t err = msg->postAndAwaitResponse(&response);
    return err;
}

status_t AmNuPlayer::getSelectedTrack(int32_t type, Parcel* reply) const {
    sp<AMessage> msg = new AMessage(kWhatGetSelectedTrack, this);
    msg->setPointer("reply", reply);
    msg->setInt32("type", type);

    sp<AMessage> response;
    status_t err = msg->postAndAwaitResponse(&response);
    if (err == OK && response != NULL) {
        CHECK(response->findInt32("err", &err));
    }
    return err;
}

status_t AmNuPlayer::selectTrack(size_t trackIndex, bool select, int64_t timeUs) {
    sp<AMessage> msg = new AMessage(kWhatSelectTrack, this);
    msg->setSize("trackIndex", trackIndex);
    msg->setInt32("select", select);
    msg->setInt64("timeUs", timeUs);

    sp<AMessage> response;
    status_t err = msg->postAndAwaitResponse(&response);

    if (err != OK) {
        return err;
    }

    if (!response->findInt32("err", &err)) {
        err = OK;
    }

    return err;
}

status_t AmNuPlayer::getCurrentPosition(int64_t *mediaUs) {
    sp<Renderer> renderer = mRenderer;
    if (renderer == NULL) {
        return NO_INIT;
    }
    //ALOGI("getcurrent:%s",(mStrCurrentAudioCodec != NULL) ? "dts" : "null" );
    if ( mStrCurrentAudioCodec != NULL &&!strncmp(mStrCurrentAudioCodec,"DTS",3)) {
        int stream_type=0;
        int TotalApre=0;
        int MulAssetHint=0;
        int HPS_hint=0;
        dtsm6_get_exchange_info(&stream_type,&TotalApre,NULL,NULL,NULL,&MulAssetHint,&HPS_hint);
        ALOGI("%d:%d:%d:%d",stream_type,TotalApre,MulAssetHint,HPS_hint);
        if (TotalApre != DtshdApreTotal && TotalApre>0 ) {
            ALOGI("[%s %d]TotalApre changed:%d-->%d\n",__FUNCTION__,__LINE__,DtshdApreTotal,TotalApre);
            DtshdApreTotal=TotalApre;
            notifyListener(MEDIA_INFO, MEDIA_INFO_AMLOGIC_SHOW_DTS_ASSET,0,NULL);
        }
        if (stream_type != DtsHdStreamType) {
            ALOGI("[%s %d]DtsHdStreamType changed:%d-->%d\n",__FUNCTION__,__LINE__,DtsHdStreamType,stream_type);
             DtsHdStreamType=stream_type;
             if (DtsHdStreamType == 0x0)
                notifyListener(MEDIA_INFO, MEDIA_INFO_AMLOGIC_SHOW_DTS_ASSET,0,NULL);
             if (DtsHdStreamType == 0x1)
                notifyListener(MEDIA_INFO, MEDIA_INFO_AMLOGIC_SHOW_DTS_EXPRESS,0,NULL);
             else if (DtsHdStreamType==0x2)
                notifyListener(MEDIA_INFO, MEDIA_INFO_AMLOGIC_SHOW_DTS_HD_MASTER_AUDIO,0,NULL);
        }
        if (DtsHdMulAssetHint != MulAssetHint && MulAssetHint) {//TOTO:xiangliang.wang
            ALOGI("[%s %d]MulAssetHint event send\n",__FUNCTION__,__LINE__);
            notifyListener(MEDIA_INFO, MEDIA_INFO_AMLOGIC_SHOW_DTS_MULASSETHINT,0,NULL);
            DtsHdMulAssetHint=MulAssetHint;
        }

        if (HPS_hint && DtsHdHpsHint == 0) {
            notifyListener(MEDIA_INFO,MEDIA_INFO_AMLOGIC_SHOW_DTS_HPS_NOTSUPPORT,0,NULL);
            DtsHdHpsHint=1;
        }
    }
    return renderer->getCurrentPosition(mediaUs);
}

void AmNuPlayer::getStats(Vector<sp<AMessage> > *mTrackStats) {
    CHECK(mTrackStats != NULL);

    mTrackStats->clear();
    if (mVideoDecoder != NULL) {
        mTrackStats->push_back(mVideoDecoder->getStats());
    }
    if (mAudioDecoder != NULL) {
        mTrackStats->push_back(mAudioDecoder->getStats());
    }
}

sp<MetaData> AmNuPlayer::getFileMeta() {
    return mSource->getFileFormatMeta();
}

float AmNuPlayer::getFrameRate() {
    sp<MetaData> meta = mSource->getFormatMeta(false /* audio */);
    if (meta == NULL) {
        return 0;
    }
    int32_t rate;
    if (!meta->findInt32(kKeyFrameRate, &rate)) {
        // fall back to try file meta
        sp<MetaData> fileMeta = getFileMeta();
        if (fileMeta == NULL) {
            ALOGW("source has video meta but not file meta");
            return -1;
        }
        int32_t fileMetaRate;
        if (!fileMeta->findInt32(kKeyFrameRate, &fileMetaRate)) {
            return -1;
        }
        return fileMetaRate;
    }
    return rate;
}

void AmNuPlayer::schedulePollDuration() {
    sp<AMessage> msg = new AMessage(kWhatPollDuration, this);
    msg->setInt32("generation", mPollDurationGeneration);
    msg->post();
}

void AmNuPlayer::cancelPollDuration() {
    ++mPollDurationGeneration;
}

void AmNuPlayer::processDeferredActions() {
    while (!mDeferredActions.empty()) {
        // We won't execute any deferred actions until we're no longer in
        // an intermediate state, i.e. one more more decoders are currently
        // flushing or shutting down.

        if (mFlushingAudio != NONE || mFlushingVideo != NONE) {
            // We're currently flushing, postpone the reset until that's
            // completed.

            ALOGV("postponing action mFlushingAudio=%d, mFlushingVideo=%d",
                  mFlushingAudio, mFlushingVideo);

            break;
        }

        sp<Action> action = *mDeferredActions.begin();
        mDeferredActions.erase(mDeferredActions.begin());

        action->execute(this);
    }
}

void AmNuPlayer::performSeek(int64_t seekTimeUs) {
    ALOGV("performSeek seekTimeUs=%lld us (%.2f secs)",
          (long long)seekTimeUs,
          seekTimeUs / 1E6);

    if (mSource == NULL) {
        // This happens when reset occurs right before the loop mode
        // asynchronously seeks to the start of the stream.
        LOG_ALWAYS_FATAL_IF(mAudioDecoder != NULL || mVideoDecoder != NULL,
                "mSource is NULL and decoders not NULL audio(%p) video(%p)",
                mAudioDecoder.get(), mVideoDecoder.get());
        return;
    }
    mPreviousSeekTimeUs = seekTimeUs;

    thread_interrupt();
    mSource->seekTo(seekTimeUs);
    thread_uninterrupt();

    ++mTimedTextGeneration;

    // everything's flushed, continue playback.
}

void AmNuPlayer::performDecoderFlush(FlushCommand audio, FlushCommand video) {
    ALOGI("performDecoderFlush audio=%d, video=%d", audio, video);

    if ((audio == FLUSH_CMD_NONE || mAudioDecoder == NULL)
            && (video == FLUSH_CMD_NONE || mVideoDecoder == NULL)) {
        return;
    }

    if (audio != FLUSH_CMD_NONE && mAudioDecoder != NULL) {
        flushDecoder(true /* audio */, (audio == FLUSH_CMD_SHUTDOWN));
    }

    if (video != FLUSH_CMD_NONE && mVideoDecoder != NULL) {
        flushDecoder(false /* audio */, (video == FLUSH_CMD_SHUTDOWN));
    }
}

void AmNuPlayer::performReset() {
    ALOGI("performReset");

    CHECK(mAudioDecoder == NULL);
    CHECK(mVideoDecoder == NULL);

    cancelPollDuration();

    ++mScanSourcesGeneration;
    mScanSourcesPending = false;

    if (mRendererLooper != NULL) {
        if (mRenderer != NULL) {
            mRendererLooper->unregisterHandler(mRenderer->id());
        }
        mRendererLooper->stop();
        mRendererLooper.clear();
    }
    mRenderer.clear();
    ++mRendererGeneration;

    if (mSource != NULL) {
        thread_interrupt();//add
        mSource->stop();
        Mutex::Autolock autoLock(mSourceLock);
        mSource.clear();

        thread_uninterrupt();
    }

    if (mDriver != NULL) {
        sp<AmNuPlayerDriver> driver = mDriver.promote();
        if (driver != NULL) {
            driver->notifyResetComplete();
        }
    }

    mStarted = false;
    mPrepared = false;
    mResetting = false;
    mSourceStarted = false;
}

void AmNuPlayer::performScanSources() {
    ALOGV("performScanSources");

    if (!mStarted) {
        return;
    }

    if (mAudioDecoder == NULL || mVideoDecoder == NULL) {
        postScanSources();
    }
}

void AmNuPlayer::performSetSurface(const sp<Surface> &surface) {
    ALOGV("performSetSurface");

    mSurface = surface;

    // XXX - ignore error from setVideoScalingMode for now
    setVideoScalingMode(mVideoScalingMode);

    if (mDriver != NULL) {
        sp<AmNuPlayerDriver> driver = mDriver.promote();
        if (driver != NULL) {
            driver->notifySetSurfaceComplete();
        }
    }
}

void AmNuPlayer::performResumeDecoders(bool needNotify) {
    if (needNotify) {
        mResumePending = true;
        if (mVideoDecoder == NULL) {
            // if audio-only, we can notify seek complete now,
            // as the resume operation will be relatively fast.
            finishResume();
        }
    }

    if (mVideoDecoder != NULL) {
        // When there is continuous seek, MediaPlayer will cache the seek
        // position, and send down new seek request when previous seek is
        // complete. Let's wait for at least one video output frame before
        // notifying seek complete, so that the video thumbnail gets updated
        // when seekbar is dragged.
        mVideoDecoder->signalResume(needNotify);
    }

    if (mAudioDecoder != NULL) {
        mAudioDecoder->signalResume(false /* needNotify */);
    }
}

void AmNuPlayer::finishResume() {
    if (mResumePending) {
        mResumePending = false;
        notifyDriverSeekComplete();
    }
}

void AmNuPlayer::notifyDriverSeekComplete() {
    if (mDriver != NULL) {
        sp<AmNuPlayerDriver> driver = mDriver.promote();
        if (driver != NULL) {
            driver->notifySeekComplete();
        }
    }
}

void AmNuPlayer::onSourceNotify(const sp<AMessage> &msg) {
    int32_t what;
    CHECK(msg->findInt32("what", &what));

    switch (what) {
        case Source::kWhatInstantiateSecureDecoders:
        {
            if (mSource == NULL) {
                // This is a stale notification from a source that was
                // asynchronously preparing when the client called reset().
                // We handled the reset, the source is gone.
                break;
            }

            sp<AMessage> reply;
            CHECK(msg->findMessage("reply", &reply));
            status_t err = onInstantiateSecureDecoders();
            reply->setInt32("err", err);
            reply->post();
            break;
        }

        case Source::kWhatPrepared:
        {
            if (mSource == NULL) {
                // This is a stale notification from a source that was
                // asynchronously preparing when the client called reset().
                // We handled the reset, the source is gone.
                break;
            }

            int32_t err;
            CHECK(msg->findInt32("err", &err));

            if (err != OK) {
                // shut down potential secure codecs in case client never calls reset
                mDeferredActions.push_back(
                        new FlushDecoderAction(FLUSH_CMD_SHUTDOWN /* audio */,
                                               FLUSH_CMD_SHUTDOWN /* video */));
                processDeferredActions();
            } else {
                mPrepared = true;
            }

            sp<AmNuPlayerDriver> driver = mDriver.promote();
            if (driver != NULL) {
                // notify duration first, so that it's definitely set when
                // the app received the "prepare complete" callback.
                int64_t durationUs;
                if (mSource->getDuration(&durationUs) == OK) {
                    driver->notifyDuration(durationUs);
                }
                ALOGI("notifyPrepareCompleted");
                driver->notifyPrepareCompleted(err);
            }

            break;
        }

        case Source::kWhatFlagsChanged:
        {
            uint32_t flags;
            CHECK(msg->findInt32("flags", (int32_t *)&flags));

            sp<AmNuPlayerDriver> driver = mDriver.promote();
            if (driver != NULL) {
                if ((flags & AmNuPlayer::Source::FLAG_CAN_SEEK) == 0) {
                    driver->notifyListener(
                            MEDIA_INFO, MEDIA_INFO_NOT_SEEKABLE, 0);
                }
                driver->notifyFlagsChanged(flags);
            }

            if ((mSourceFlags & Source::FLAG_DYNAMIC_DURATION)
                    && (!(flags & Source::FLAG_DYNAMIC_DURATION))) {
                cancelPollDuration();
            } else if (!(mSourceFlags & Source::FLAG_DYNAMIC_DURATION)
                    && (flags & Source::FLAG_DYNAMIC_DURATION)
                    && (mAudioDecoder != NULL || mVideoDecoder != NULL)) {
                schedulePollDuration();
            }

            mSourceFlags = flags;
            break;
        }

        case Source::kWhatVideoSizeChanged:
        {
            sp<AMessage> format;
            CHECK(msg->findMessage("format", &format));

            updateVideoSize(format);
            break;
        }

        case Source::kWhatBufferingUpdate:
        {
            int32_t percentage;
            CHECK(msg->findInt32("percentage", &percentage));

            notifyListener(MEDIA_BUFFERING_UPDATE, percentage, 0);
            break;
        }

        case Source::kWhatPauseOnBufferingStart:
        {
            // ignore if not playing
            if (mStarted) {
                ALOGI("buffer low, pausing...");

                mPausedForBuffering = true;
                onPause();
            }
            notifyListener(MEDIA_INFO, MEDIA_INFO_BUFFERING_START, 0);
            break;
        }

        case Source::kWhatResumeOnBufferingEnd:
        {
            // ignore if not playing
            if (mStarted) {
                ALOGI("buffer ready, resuming...");

                mPausedForBuffering = false;

                // do not resume yet if client didn't unpause
                if (!mPausedByClient) {
                    onResume();
                }
            }
            notifyListener(MEDIA_INFO, MEDIA_INFO_BUFFERING_END, 0);
            break;
        }

        case Source::kWhatCacheStats:
        {
            int32_t kbps;
            CHECK(msg->findInt32("bandwidth", &kbps));

            notifyListener(MEDIA_INFO, MEDIA_INFO_NETWORK_BANDWIDTH, kbps);
            break;
        }

        case Source::kWhatSubtitleData:
        {
            sp<ABuffer> buffer;
            CHECK(msg->findBuffer("buffer", &buffer));

            sendSubtitleData(buffer, 0 /* baseIndex */);
            break;
        }

        case Source::kWhatTimedMetaData:
        {
            sp<ABuffer> buffer;
            if (!msg->findBuffer("buffer", &buffer)) {
                notifyListener(MEDIA_INFO, MEDIA_INFO_METADATA_UPDATE, 0);
            } else {
                sendTimedMetaData(buffer);
            }
            break;
        }

        case Source::kWhatTimedTextData:
        {
            int32_t generation;
            if (msg->findInt32("generation", &generation)
                    && generation != mTimedTextGeneration) {
                break;
            }

            sp<ABuffer> buffer;
            CHECK(msg->findBuffer("buffer", &buffer));

            sp<AmNuPlayerDriver> driver = mDriver.promote();
            if (driver == NULL) {
                break;
            }

            int posMs;
            int64_t timeUs, posUs;
            driver->getCurrentPosition(&posMs);
            posUs = (int64_t) posMs * 1000ll;
            CHECK(buffer->meta()->findInt64("timeUs", &timeUs));

            if (posUs < timeUs) {
                if (!msg->findInt32("generation", &generation)) {
                    msg->setInt32("generation", mTimedTextGeneration);
                }
                msg->post(timeUs - posUs);
            } else {
                sendTimedTextData(buffer);
            }
            break;
        }

        case Source::kWhatQueueDecoderShutdown:
        {
            int32_t audio, video;
            CHECK(msg->findInt32("audio", &audio));
            CHECK(msg->findInt32("video", &video));

            sp<AMessage> reply;
            CHECK(msg->findMessage("reply", &reply));

            queueDecoderShutdown(audio, video, reply);
            break;
        }

        case Source::kWhatDrmNoLicense:
        {
            notifyListener(MEDIA_ERROR, MEDIA_ERROR_UNKNOWN, ERROR_DRM_NO_LICENSE);
            break;
        }

        case Source::kWhatSourceReady:
        {
            int32_t err;
            CHECK(msg->findInt32("err", &err));
            notifyListener(0xffff, err, 0);
            break;
        }

        case Source::kWhatFrameRate:
        {
            CHECK(msg->findFloat("frame-rate", &mFrameRate));
            //ALOGI("frame-rate %.2f",mFrameRate);
            break;
        }
        case Source::kWhatDecodeParam:
        {
            CHECK(msg->findInt32("decodc-param", &mDecodecParam));
            break;
        }

        default:
            TRESPASS();
    }
}

void AmNuPlayer::onClosedCaptionNotify(const sp<AMessage> &msg) {
    int32_t what;
    CHECK(msg->findInt32("what", &what));

    switch (what) {
        case AmNuPlayer::CCDecoder::kWhatClosedCaptionData:
        {
            sp<ABuffer> buffer;
            CHECK(msg->findBuffer("buffer", &buffer));

            size_t inbandTracks = 0;
            if (mSource != NULL) {
                inbandTracks = mSource->getTrackCount();
            }

            sendSubtitleData(buffer, inbandTracks);
            break;
        }

        case AmNuPlayer::CCDecoder::kWhatTrackAdded:
        {
            notifyListener(MEDIA_INFO, MEDIA_INFO_METADATA_UPDATE, 0);

            break;
        }

        default:
            TRESPASS();
    }


}

void AmNuPlayer::sendSubtitleData(const sp<ABuffer> &buffer, int32_t baseIndex) {
    int32_t trackIndex;
    int64_t timeUs, durationUs;
    CHECK(buffer->meta()->findInt32("trackIndex", &trackIndex));
    CHECK(buffer->meta()->findInt64("timeUs", &timeUs));
    CHECK(buffer->meta()->findInt64("durationUs", &durationUs));

    Parcel in;
    in.writeInt32(trackIndex + baseIndex);
    in.writeInt64(timeUs);
    in.writeInt64(durationUs);
    in.writeInt32(buffer->size());
    in.writeInt32(buffer->size());
    in.write(buffer->data(), buffer->size());

    notifyListener(MEDIA_SUBTITLE_DATA, 0, 0, &in);
}

void AmNuPlayer::sendTimedMetaData(const sp<ABuffer> &buffer) {
    int64_t timeUs;
    CHECK(buffer->meta()->findInt64("timeUs", &timeUs));

    Parcel in;
    in.writeInt64(timeUs);
    in.writeInt32(buffer->size());
    in.writeInt32(buffer->size());
    in.write(buffer->data(), buffer->size());

    notifyListener(MEDIA_META_DATA, 0, 0, &in);
}

void AmNuPlayer::sendTimedTextData(const sp<ABuffer> &buffer) {
    const void *data;
    size_t size = 0;
    int64_t timeUs;
    int32_t flag = TextDescriptions::IN_BAND_TEXT_3GPP;

    AString mime;
    CHECK(buffer->meta()->findString("mime", &mime));
    CHECK(strcasecmp(mime.c_str(), MEDIA_MIMETYPE_TEXT_3GPP) == 0);

    data = buffer->data();
    size = buffer->size();

    Parcel parcel;
    if (size > 0) {
        CHECK(buffer->meta()->findInt64("timeUs", &timeUs));
        int32_t global = 0;
        if (buffer->meta()->findInt32("global", &global) && global) {
            flag |= TextDescriptions::GLOBAL_DESCRIPTIONS;
        } else {
            flag |= TextDescriptions::LOCAL_DESCRIPTIONS;
        }
        TextDescriptions::getParcelOfDescriptions(
                (const uint8_t *)data, size, flag, timeUs / 1000, &parcel);
    }

    if ((parcel.dataSize() > 0)) {
        notifyListener(MEDIA_TIMED_TEXT, 0, 0, &parcel);
    } else {  // send an empty timed text
        notifyListener(MEDIA_TIMED_TEXT, 0, 0);
    }
}
////////////////////////////////////////////////////////////////////////////////

sp<AMessage> AmNuPlayer::Source::getFormat(bool audio) {
    sp<MetaData> meta = getFormatMeta(audio);

    if (meta == NULL) {
        return NULL;
    }

    sp<AMessage> msg = new AMessage;

    if (convertMetaDataToMessage(meta, &msg) == OK) {
        return msg;
    }
    return NULL;
}

void AmNuPlayer::Source::notifyFlagsChanged(uint32_t flags) {
    sp<AMessage> notify = dupNotify();
    notify->setInt32("what", kWhatFlagsChanged);
    notify->setInt32("flags", flags);
    notify->post();
}

void AmNuPlayer::Source::notifyVideoSizeChanged(const sp<AMessage> &format) {
    sp<AMessage> notify = dupNotify();
    notify->setInt32("what", kWhatVideoSizeChanged);
    notify->setMessage("format", format);
    notify->post();
}

void AmNuPlayer::Source::notifyPrepared(status_t err) {
    sp<AMessage> notify = dupNotify();
    notify->setInt32("what", kWhatPrepared);
    notify->setInt32("err", err);
    notify->post();
}

void AmNuPlayer::Source::notifyInstantiateSecureDecoders(const sp<AMessage> &reply) {
    sp<AMessage> notify = dupNotify();
    notify->setInt32("what", kWhatInstantiateSecureDecoders);
    notify->setMessage("reply", reply);
    notify->post();
}

void AmNuPlayer::Source::onMessageReceived(const sp<AMessage> & /* msg */) {
    TRESPASS();
}

}  // namespace android
