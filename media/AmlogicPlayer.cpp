/*
** Copyright 2007, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#define LOG_NDEBUG 0
#define LOG_TAG "AmlogicPlayer"
#include "utils/Log.h"
#include <stdio.h>
#include <assert.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <utils/String8.h>

#include <gui/Surface.h>


#include <gui/ISurfaceComposer.h>
#include <gui/SurfaceComposerClient.h>

#include <android/native_window.h>
#include "AmlogicPlayerRender.h"
#include <ui/Rect.h>
#include "AmlogicPlayerExtractorDemux.h"
#include <binder/IPCThreadState.h>
#include <SubSource.h>
//#include <media/stagefright/timedtext/TimedTextDriver.h>
//#include <ui/Overlay.h>
#define  TRACE()    LOGV("[%s::%d]\n",__FUNCTION__,__LINE__)
//#define  TRACE()
#include <amdrmutils.h>
#include <dlfcn.h>

#define SCREEN_MODE_SET_PATH "/sys/class/video/screen_mode"

//screen mode 0 normal; 1 full screen;  2 4:3; 3 16:9;

#include <cutils/properties.h>
#include <media/IMediaHTTPConnection.h>
#include <media/IMediaHTTPService.h>

extern int android_datasource_init(void);


static inline bool casestr_is_endof(const char *str, const char *tag)
{
    int slen = strlen(str);
    int taglen = strlen(tag);
    return (slen >= taglen && !strcasecmp(tag, &str[slen - taglen]));
}

#define HAS_CASE_STR(str,tag) (strcasestr(str,tag))
#define IS_END_OF(url,tag) casestr_is_endof(url,tag)
#define IS_M3U8_URL(url) (HAS_CASE_STR(url,".m3u8") || IS_END_OF(url,"m3u8"))


#include "AmlogicPlayer.h"
#include "Amvideoutils.h"
#include "ammodule.h"

#ifndef FBIOPUT_OSD_SRCCOLORKEY
#define  FBIOPUT_OSD_SRCCOLORKEY    0x46fb
#endif

#ifndef FBIOPUT_OSD_SRCKEY_ENABLE
#define  FBIOPUT_OSD_SRCKEY_ENABLE  0x46fa
#endif

#ifndef FBIOPUT_OSD_SET_GBL_ALPHA
#define  FBIOPUT_OSD_SET_GBL_ALPHA  0x4500
#endif


#ifdef HAVE_GETTID
static pid_t myTid()
{
    return gettid();
}
#else
static pid_t myTid()
{
    return getpid();
}
#endif

// ----------------------------------------------------------------------------
namespace android
{
#ifndef MIN
#define MIN(x,y) ((x)<(y)?(x):(y))
#endif
// ----------------------------------------------------------------------------

// TODO: Determine appropriate return codes
static status_t ERROR_NOT_OPEN = -1;
static status_t ERROR_OPEN_FAILED = -2;
static status_t ERROR_ALLOCATE_FAILED = -4;
static status_t ERROR_NOT_SUPPORTED = -8;
static status_t ERROR_NOT_READY = -16;
static status_t STATE_INIT = 0;
static status_t STATE_ERROR = 1;
static status_t STATE_OPEN = 2;



static URLProtocol android_protocol;

#define MID_800_400_FREESC  (0x10001)

AmlogicPlayer::AmlogicPlayer() :
    mPlayTime(0),  mStreamTime(0), mDuration(0),
    mState(STATE_ERROR),
    mStreamType(-1), mLoop(false), mFirstPlaying(true),
    mExit(false), mPaused(false), mRunning(false),
    mPlayer_id(-1),
    mWidth(0), mHeight(0),
    mhasVideo(1),  mhasAudio(1),
    mIgnoreMsg(false),
    mTypeReady(false),
    mAudioTrackNum(0),
    mVideoTrackNum(0),
    mInnerSubNum(0),
    mHttpWV(false),
    mDecryptHandle(NULL),
    mDrmManagerClient(NULL),
    isHDCPFailed(false),
    isWidevineStreaming(false),
    mAuto3DDetected(false),
    mSeekPos(0),
    isSmoothStreaming(false)
{
    Mutex::Autolock l(mMutex);
    streaminfo_valied = false;
    mStrCurrentVideoCodec = NULL;
    mStrCurrentAudioCodec = NULL;
    mAudioExtInfo = NULL;
    mSubExtInfo = NULL;
    mVideoExtInfo = NULL;
    mChangedCpuFreq = false;
    mInbuffering = false;
    PlayerStartTimeUS = ALooper::GetNowUs();
    mLastPlayTimeUpdateUS = ALooper::GetNowUs();
    LOGV("AmlogicPlayer constructor\n");
    memset(&mAmlogicFile, 0, sizeof mAmlogicFile);
    memset(&mPlay_ctl, 0, sizeof mPlay_ctl);
    memset(mTypeStr, 0, sizeof(mTypeStr));
    memset(&mStreamInfo, 0, sizeof(mStreamInfo));
    curLayout = Rect(0, 0, 0, 0);
    video_rotation_degree = 0;
    fastNotifyMode = 0;
    mEnded = false;
    mShouldSendPlayComplete = false;
    mOffsetBacFlag = false;
    mOffsetBac = 0;
    mLowLevelBufMode = false;
    LatestPlayerState = PLAYER_INITING;
    mDelayUpdateTime = 0;
    isTryDRM = false;
    mNeedResetOnResume = 0;
    if (PropGetFloat("media.amplayer.stopbuftime")) {
        mStopFeedingBuf_ms = PropGetFloat("media.amplayer.stopbuftime") * 1000;
    } else {
        mStopFeedingBuf_ms = 1200;
    }
    LOGV("mStopFeedingBuf_ms = %d\n", mStopFeedingBuf_ms);
    if (mStopFeedingBuf_ms < 0) {
        mStopFeedingBuf_ms = 1200; //set default
    }
    mHWaudiobufsize = 384 * 1024;
    mHWvideobufsize = 7 * 1024 * 1024;
    mHWaudiobuflevel = 0;
    mHWvideobuflevel = 0;
    isHTTPSource = false;
    isDvbTvinSource = false;
    mStreamTimeExtAddS = PropGetFloat("media.amplayer.streamtimeadd");
    if (mStreamTimeExtAddS <= 0) {
        mStreamTimeExtAddS = 10000;
    }
    mLastStreamTimeUpdateUS = ALooper::GetNowUs();
    mVideoScalingMode = NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW;
    CallingAPkName[0] = '\0';
    //mTextDriver = NULL;
    mListener = this;
    mSubSource = NULL;
    enableOSDVideo = false;
    drop_tiny_seek_ms = PropGetFloat("media.amplayer.droptinyseek.ms", -1);
    if (drop_tiny_seek_ms < 0) {
        drop_tiny_seek_ms = 100;
    }
    mFFStatus = false;
    mSupportSeek = -1;
    mSeekdone = false;
    mLastPlaytime = 0;
    mLastPosition = 0;
    mPlayTimeBac = 0;
    realpositionBac = 0;
    mLeftVolume = 1;
    mRightVolume = 1;
    mSetVolumeFlag = 0;
    mLatestPauseState = false;

    bufferTime = 0;
    mDelaySendBufferingInfo_s = (int)PropGetFloat("media.amplayer.delaybuffering.s", 0.0);
    DtshdApreTotal = 0;
    DtsHdStreamType = 0;
    DtsHdMulAssetHint = 0;
    DtsHdHpsHint = 0;
    mParcel = NULL;
    AudioDualMonoNeed = 0;
    AudioDualMonoSetOK = 0;
    mPlaybackSettings = AUDIO_PLAYBACK_RATE_DEFAULT;
    memset(mTrackMap, -1, sizeof(mTrackMap));
}

int HistoryMgt(const char * path, int r0w1, int mTime)
{
    ///this a simple history mgt;only save the latest file,and playingtime,and must be http;
    static char static_path[1024] = "";
    static Mutex HistoryMutex;
    static int lastplayingtime = -1;
    Mutex::Autolock l(HistoryMutex);
    if (!r0w1) { //read
        if (strcmp(path, static_path) == 0) {
            return lastplayingtime;
        }

    } else { //save
        if (strlen(path) > 1024 - 1 || strlen(path) < 10) {
            return 0;
        }
        if (memcmp(path, "http://", 7) == 0 || memcmp(path, "shttp://", 8) == 0) { //not http,we don't save it now;
            strcpy(static_path, path);
            lastplayingtime = mTime;
            return 0;
        }
    }
    return 0;
}

#define MAX_PATH_LEN  100
#define MAX_VFM_MAP_LEN 300

struct map_t {
    int pos;
    String8 name;
};

static int Vfm_Compare(const map_t *a, const map_t *b)
{
    if (a->pos > b->pos) {
        return 1;
    }
    return 0;
}

static void Get_Vfm_Path(char * path, Vector<map_t> &list)
{
    const char* vfm_map_list[] = {
        "ppmgr", "deinterlace", "decoder", "amvideo", "amlvideo2.0", "amlvideo",
        "ionvideo", "amvideo4osd", "osd", "vdin0", "vdin1", "vm", NULL
    };
    char *node_p = NULL;
    int i = 0;
    i = 0;
    while (vfm_map_list[i]) {
        if (node_p = strstr(path, vfm_map_list[i])) {
            map_t node;
            node.pos = (int)node_p;
            node.name.append(vfm_map_list[i]);
            list.push_back(node);
            LOGI("list path [%s] pos:%d\n", vfm_map_list[i], node_p);
        }
        i++;
    }

    if (list.size() > 1) {
        list.sort(Vfm_Compare);
    }
}

int IsTheSameVfmPathDefault(char * path)
{
    int fd;
    char valstr[MAX_VFM_MAP_LEN] = {0};
    char default_map[MAX_VFM_MAP_LEN] = {0};
    char *start_p;
    char *end_p;
    int len;

    if (path == NULL) {
        LOGE("[%s:%d] path is null\n", __FUNCTION__, __LINE__);
        return 1;
    }
    LOGI("get path [%s]\n", path);

    Vector<map_t> prop_list;
    Vector<map_t> node_list;

    Get_Vfm_Path(path, prop_list);

    fd = open("sys/class/vfm/map", O_RDONLY);
    if (fd >= 0) {
        memset(valstr, 0, MAX_VFM_MAP_LEN);
        read(fd, valstr, MAX_VFM_MAP_LEN);
        valstr[MAX_VFM_MAP_LEN - 1] = '\0';
        close(fd);
    } else {
        LOGE("unable to open file %s,err: %s", path, strerror(errno));
        sprintf(valstr, "%s", "fail");
        return -1;
    }

    start_p = strstr(valstr, "default {");
    end_p = strstr(start_p, "}");

    len = end_p - start_p + 1;

    strncpy(default_map, start_p, len);
    LOGI("default_map path [%s]\n", default_map);

    Get_Vfm_Path(default_map, node_list);

    bool isPropEmpty, isNodeEmpty;
    do {
        isPropEmpty = prop_list.isEmpty();
        isNodeEmpty = node_list.isEmpty();
        if (isPropEmpty) {
            if (!isNodeEmpty) {
                LOGE("[%s:%d] prop_list is null but node_list is not null\n", __FUNCTION__, __LINE__);
                return -1;
            }
            LOGI("[%s:%d]both prop_list and node_list is null\n", __FUNCTION__, __LINE__);
        } else {
            if (isNodeEmpty) {
                LOGE("[%s:%d] node_list is null but prop_list is not null\n", __FUNCTION__, __LINE__);
                return -1;
            }
            map_t prop = prop_list.itemAt(0);
            map_t node = node_list.itemAt(0);
            prop_list.removeAt(0);
            node_list.removeAt(0);
            LOGI("prop_list [%s]  node_list[%s]\n", prop.name.string(), node.name.string());
            if (prop.name != node.name) {
                LOGE("prop_list [%s] is different with node_list[%s]\n", prop.name.string(), node.name.string());
                return -1;
            } else {
                LOGI("prop_list [%s] is same with node_list[%s]\n", prop.name.string(), node.name.string());
            }
        }
    } while (!isPropEmpty && !isNodeEmpty);
    LOGI("prop_list path is same with node_list\n");
    return 1;
}

#define DISABLE_VIDEO "/sys/class/video/disable_video"

typedef void *(*DTVPRelease)();
int set_sys_int(const char *path, int val);

void CheckTVPEnable()
{
    int num;
    struct tvp_region r;
    void* SecLibHandle;
    DTVPRelease TVPRelease;

    num = tvp_mm_get_mem_region(&r, sizeof(struct tvp_region));
    if (num > 0) {
        LOGE("CheckTVPEnable unprotect TVP memory\n", num);
        SecLibHandle = dlopen("libsecmem.so", RTLD_NOW);
        if (SecLibHandle) {
            TVPRelease = (DTVPRelease)dlsym(SecLibHandle, "Secure_ReleaseResource");
            if (TVPRelease) {
                TVPRelease();
            }
        }
        set_sys_int(DISABLE_VIDEO, 1);
        free_cma_buffer();
        tvp_mm_disable(0);
    }
}

status_t AmlogicPlayer::BasicInit()
{
    static int have_inited = 0;
    if (!have_inited) {
        char dir[PROPERTY_VALUE_MAX];
        int cachesize, blocksize;
        player_init();
        URLProtocol *prot = &android_protocol;
        prot->name = "android";
        prot->url_open = (int (*)(URLContext *, const char *, int))vp_open;
        prot->url_read = (int (*)(URLContext *, unsigned char *, int))vp_read;
        prot->url_write = (int (*)(URLContext *, unsigned char *, int))vp_write;
        prot->url_seek = (int64_t (*)(URLContext *, int64_t , int))vp_seek;
        prot->url_close = (int (*)(URLContext *))vp_close;
        prot->url_get_file_handle = (int (*)(URLContext *))vp_get_file_handle;
        av_register_protocol(prot);
        AmlogicPlayerStreamSource::init();
        have_inited++;
        if (PropIsEnable("media.amplayer.cacheenable")) {
            if (property_get("media.amplayer.cachedir", dir, NULL) > 0)
                ;
            else {
                dir[0] = '\0';    /*clear the dir path*/
            }
            cachesize = (int)PropGetFloat("media.amplayer.cachesize");
            blocksize = (int)PropGetFloat("media.amplayer.cacheblocksize");
            player_cache_system_init(1, dir, cachesize, blocksize);
        }
        AmlogicPlayerDataSouceProtocol::BasicInit();
        AmlogicPlayerExtractorDemux::BasicInit();
        //reset the default map for mediaserver may crashed,map with unwanted value;
        char newsetting[128];
        char value[PROPERTY_VALUE_MAX];
        int ret = 1;
        if (property_get("media.decoder.vfm.defmap", value, NULL) > 0) {
            ret = IsTheSameVfmPathDefault(value);
            if (ret < 0) {
                LOGI("get def maping [%s]\n", value);
                strcpy(newsetting, "add default ");
                strcat(newsetting, value);
                amsysfs_set_sysfs_str("/sys/class/vfm/map", "rm default");
                amsysfs_set_sysfs_str("/sys/class/vfm/map", newsetting);
            }
        }
        CheckTVPEnable();
    }
    return 0;
}



bool AmlogicPlayer::PropIsEnable(const char* str, bool def)
{
    char value[PROPERTY_VALUE_MAX];
    if (property_get(str, value, NULL) > 0) {
        if ((!strcmp(value, "1") || !strcmp(value, "true") || !strcmp(value, "ok"))) {
            LOGI("%s is enabled\n", str);
            return true;
        } else {
            LOGI("%s is disabled\n", str);
            return false;
        }
    }
    LOGI("%s is not setting,use default %s\n", str, def ? "true" : "false");
    return def;
}


float AmlogicPlayer::PropGetFloat(const char* str, float def)
{
    char value[PROPERTY_VALUE_MAX];
    float ret = def;
    if (property_get(str, value, NULL) > 0) {
        if ((sscanf(value, "%f", &ret)) > 0) {
            LOGI("%s is set to %f\n", str, ret);
            return ret;
        }
    }
    LOGI("%s is not set used def=%f\n", str, ret);
    return ret;
}

status_t AmlogicPlayer::exitAllThreads()
{
    AmlogicPlayer::BasicInit();
    pid_info_t playerinfo;

    player_list_allpid(&playerinfo);
    LOGI("found %d not exit player threads,try exit it now\n", playerinfo.num);
    if (playerinfo.num > 0) {
        int i;
        for (i = 0; i < playerinfo.num; i++) {
            //player_exit(playerinfo.pid[i]);
        }
    }
    return NO_ERROR;
}

void AmlogicPlayer::onFirstRef()
{
    Mutex::Autolock l(mMutex);
    LOGV("onFirstRef");
    AmlogicPlayer::BasicInit();
    AmlogicPlayer::exitAllThreads();
    av_log_set_level(50);
    // create playback thread
    mState = STATE_INIT;
}

status_t AmlogicPlayer::initCheck()
{
    Mutex::Autolock l(mMutex);
    LOGV("initCheck");
    if (mState != STATE_ERROR) {
        return NO_ERROR;
    }
    return ERROR_NOT_READY;
}

int get_sysfs_int(const char *path)
{
    int fd;
    int val = 0;
    char  bcmd[16];
    fd = open(path, O_RDONLY);
    if (fd >= 0) {
        read(fd, bcmd, sizeof(bcmd));
        val = strtol(bcmd, NULL, 10);
        close(fd);
    }
    return val;
}

int set_sys_int(const char *path, int val)
{
    int fd;
    char  bcmd[16];
    fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd >= 0) {
        sprintf(bcmd, "%d", val);
        write(fd, bcmd, strlen(bcmd));
        close(fd);
        return 0;
    }
    LOGV("set fs%s=%d failed\n", path, val);
    return -1;
}
bool IsManifestUrl(const char* url)
{
    return IS_END_OF(url, "/manifest");
}
bool IsVrVmUrl(const char* url)
{
    if (!strncasecmp(url, "vrwc", 4) || !strncasecmp(url, "vstb", 4)) {    ////Verimatrix link, vrwc:viewright web client ; vstb:viewright stb for iptv
        return true;
    } else {
        return false;
    }
}


void
AmlogicPlayer::VideoViewOn(void)
{
    int ret = 0;
    //disable_freescale(MID_800_400_FREESC);
    //GL_2X_scale(1);
    //disable_freescale_MBX();
    ret = player_video_overlay_en(1);
    LOGV("VideoViewOn=%d\n", ret);
    //OsdBlank("/sys/class/graphics/fb0/blank",1);
    if (!PropIsEnable("media.amplayer.displast_frame")) {
        set_sys_int(DISABLE_VIDEO, 2);
    }

}
void
AmlogicPlayer::VideoViewClose(void)
{
    int ret = 0;
    ret = player_video_overlay_en(0);
    if (!PropIsEnable("media.amplayer.displast_frame")) {
        set_sys_int(DISABLE_VIDEO, 2);
    }
    //enable_freescale(MID_800_400_FREESC);
    //GL_2X_scale(0);
    //enable_freescale_MBX();
    LOGV("VideoViewClose=%d\n", ret);
    //OsdBlank("/sys/class/graphics/fb0/blank",0);

}

void
AmlogicPlayer::SetCpuScalingOnAudio(float mul_audio)
{
    const char InputFile[] = "/sys/class/audiodsp/codec_mips";
    const char OutputFile[] = "/sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq";
    int val;
    val = get_sysfs_int(InputFile);
    if (val > 0 && mul_audio > 0) {
        val = mul_audio * val;
        set_sys_int(OutputFile, val);
        LOGV("set_cpu_freq_scaling_based_auido %d\n", val);
    } else {
        LOGV("set_cpu_freq_scaling_based_auido failed\n");
    }
}

//static
int AmlogicPlayer::GetCallingAPKName(char *name, int size)
{
    /*char path[64];*/
    int ret = -1;
    strcpy(name, "NA");
    /*
    snprintf(path, 64, "/proc/%d/cmdline", IPCThreadState::self()->getCallingPid());
    ret = amsysfs_get_sysfs_str(path, name, 64);
    LOGI("GetCallingAPKName %s,name=[%s]", path, name);
    */
    return ret;
}
AmlogicPlayer::~AmlogicPlayer()
{
    LOGV("AmlogicPlayer destructor\n");
    Mutex::Autolock l(mMutex);
    if (mParcel != NULL) {
        delete mParcel;
        mParcel = NULL;
    }
    release();
    if (mStrCurrentAudioCodec) {
        free(mStrCurrentAudioCodec);
        mStrCurrentAudioCodec = NULL;
    }
    if (mStrCurrentVideoCodec) {
        free(mStrCurrentVideoCodec);
        mStrCurrentVideoCodec = NULL;
    }
    if (mAudioExtInfo) {
        free(mAudioExtInfo);
        mAudioExtInfo = NULL;
    }
    if (mSubExtInfo) {
        free(mSubExtInfo);
        mSubExtInfo = NULL;
    }
    if (mVideoExtInfo) {
        free(mVideoExtInfo);
        mVideoExtInfo = NULL;
    }


}

status_t AmlogicPlayer::setDataSource(const sp<IMediaHTTPService> &httpService,
                                      const char *uri, const KeyedVector<String8, String8> *headers)
{
    LOGV("setDataSource");

    if (!strncmp(uri, "tvin:", strlen("tvin:"))) {
        isDvbTvinSource = true;
        setdatasource(uri, -1, 0, 0x7ffffffffffffffLL, headers);
        sendEvent(MEDIA_PREPARED);
        return NO_ERROR;
    }

    if (IsManifestUrl(uri)) {
        // SmoothStreaming source detected.
        setdatasource(uri, -1, 0, 0x7ffffffffffffffLL, headers);
        return NO_ERROR;
    }
    if (strncmp(uri, "http", strlen("http")) == 0 ||
        strncmp(uri, "shttp", strlen("shttp")) == 0 ||
        strncmp(uri, "https", strlen("https")) == 0 ||
        IsVrVmUrl(uri)) {
        isHTTPSource = true;
    }

    bool isNormalHLS = false;
    if (isHTTPSource && IS_M3U8_URL(uri)) {
        isNormalHLS = true;
    }
    mHTTPService = httpService;

    if (PropIsEnable("media.amplayer.useandroidhttp") &&  !strncmp(uri, "http://", strlen("http://"))) {
        mSouceProtocol = AmlogicPlayerDataSouceProtocol::CreateFromUrl(mHTTPService, uri, headers);
        return setdatasource(mSouceProtocol->GetPathString(), -1, 0, 0x7ffffffffffffffLL, NULL);
    } else if (!strncmp(uri, "widevine://", strlen("widevine://")) && !isNormalHLS) {
        mSouceProtocol = AmlogicPlayerDataSouceProtocol::CreateFromUrl(mHTTPService, uri, headers);
        if (mSouceProtocol.get() != NULL) {
            mPlay_ctl.auto_buffing_enable = 1;
            isTryDRM = true;
            return setdatasource(mSouceProtocol->GetPathString(), -1, 0, 0x7ffffffffffffffLL, NULL);
        }
    } else if (PropIsEnable("media.amplayer.dsource4local") &&
               (!strncmp(uri, "file://", strlen("file://")) || (strstr(uri, "//") == NULL)))/*local file used android datasource
                                                                       no "//",I think it is local source.*/
    {
        mSouceProtocol = AmlogicPlayerDataSouceProtocol::CreateFromUrl(mHTTPService, uri, headers);
        if (mSouceProtocol.get() != NULL) {
            return setdatasource(mSouceProtocol->GetPathString(), -1, 0, 0x7ffffffffffffffLL, NULL);
        }
    }
    return setdatasource(uri, -1, 0, 0x7ffffffffffffffLL, headers); // intentionally less than LONG_MAX
}


status_t AmlogicPlayer::setDataSource(int fd, int64_t offset, int64_t length)
{
    LOGV("setDataSource,fd=%d,offset=%" PRId64 ",len=%" PRId64 ",not finished\n", fd, offset, length);
    if (PropIsEnable("media.amplayer.dsource4local")) {
        mSouceProtocol = AmlogicPlayerDataSouceProtocol::CreateFromFD(fd, offset, length);
        return setdatasource(mSouceProtocol->GetPathString(), -1, 0, 0x7ffffffffffffffLL, NULL);
    } else {
        return setdatasource(NULL, fd, offset, length, NULL);
    }
}
int AmlogicPlayer:: setDataSource(const sp<IStreamSource> &source)
{
    mSource = source;
    mStreamSource = new AmlogicPlayerStreamSource(source);
    fastNotifyMode = 1;
    mLowLevelBufMode = true;
    mPlay_ctl.auto_buffing_enable = 1; /*istream mode.maybe network,used auto buffering.*/
    return setdatasource(mStreamSource->GetPathString(), -1, 0, 0x7ffffffffffffffLL, NULL);
}
int AmlogicPlayer::vp_open(URLContext *h, const char *filename, int flags)
{
    /*
    sprintf(file,"android:AmlogicPlayer=[%x:%x],AmlogicPlayer_fd=[%x:%x]",
    */
    if (PropIsEnable("media.amplayer.disp_url", true)) {
        LOGV("vp_open=%s\n", filename);
    }
    if (strncmp(filename, "android", strlen("android")) == 0) {
        unsigned int fd = 0, fd1 = 0;
        char *str = strstr(filename, "AmlogicPlayer_fd");
        if (str == NULL) {
            return -1;
        }
        sscanf(str, "AmlogicPlayer_fd=[%x:%x]\n", (unsigned int*)&fd, (unsigned int*)&fd1);
        if (fd != 0 && ((unsigned int)fd1 == ~(unsigned int)fd)) {
            AmlogicPlayer_File* af = (AmlogicPlayer_File*)fd;
            h->priv_data = (void*) fd;
            h->priv_flags |= FLAGS_LOCALMEDIA;
            if (af != NULL && af->fd_valid) {

                lseek64(af->fd, af->mOffset, SEEK_SET);
                af->mCurPos = af->mOffset;
                if (PropIsEnable("media.amplayer.disp_url", true)) {
                    LOGV("android_open %s OK,h->priv_data=%p\n", filename, h->priv_data);
                }
                return 0;
            } else {
                if (PropIsEnable("media.amplayer.disp_url", true)) {
                    LOGV("android_open %s Faild\n", filename);
                }
                return -1;
            }
        }
    }
    return -1;
}

int AmlogicPlayer::vp_read(URLContext *h, unsigned char *buf, int size)
{
    AmlogicPlayer_File* af = (AmlogicPlayer_File*)h->priv_data;
    int ret;
    int len = MIN(size, (af->mOffset + af->mLength - af->mCurPos));
    if (len <= 0) {
        return 0;    /*read end*/
    }
    //LOGV("start%s,pos=%" PRId64 ",size=%d,ret=%d\n",__FUNCTION__,(int64_t)lseek(af->fd, 0, SEEK_CUR),size,ret);
    ret = read(af->fd, buf, len);
    //LOGV("end %s,size=%d,ret=%d\n",__FUNCTION__,size,ret);
    if (ret > 0) {
        af->mCurPos += ret;
    }
    return ret;
}

int AmlogicPlayer::vp_write(URLContext *h, unsigned char *buf, int size)
{
    AmlogicPlayer_File* af = (AmlogicPlayer_File*)h->priv_data;
    LOGV("%s\n", __FUNCTION__);
    return -1;
}
int64_t AmlogicPlayer::vp_seek(URLContext *h, int64_t pos, int whence)
{
    AmlogicPlayer_File* af = (AmlogicPlayer_File*)h->priv_data;
    int64_t ret;
    int64_t newsetpos;
    //LOGV("%sret=%" PRId64 ",pos=%" PRId64 ",whence=%d,tell=%" PRId64 "\n",__FUNCTION__,(int64_t)0,pos,whence,(int64_t)lseek(af->fd,0,SEEK_CUR));
    if (whence == AVSEEK_SIZE) {
        return af->mLength;
    }
    switch (whence) {
    case SEEK_CUR:
        newsetpos = af->mCurPos + pos;
        break;
    case SEEK_END:
        newsetpos = af->mOffset + af->mLength + pos;
        break;
    case SEEK_SET:
        newsetpos = af->mOffset + pos;
        break;
    default:
        return -1;/*unsupport other case;*/
    }
    if (newsetpos > (af->mOffset + af->mLength) || newsetpos < af->mOffset) {
        return -1;/*out stream range*/
    }
    ret = lseek64(af->fd, newsetpos, SEEK_SET);
    if (ret >= 0) {
        af->mCurPos = ret;
        return ret - af->mOffset;
    } else {
        return ret;
    }
    return -1;
}


int AmlogicPlayer::vp_close(URLContext *h)
{
    FILE* fp = (FILE*)h->priv_data;
    LOGV("%s\n", __FUNCTION__);
    return 0; /*don't close file here*/
    //return fclose(fp);
}

int AmlogicPlayer::vp_get_file_handle(URLContext *h)
{
    LOGV("%s\n", __FUNCTION__);
    return (intptr_t) h->priv_data;
}

status_t AmlogicPlayer::UpdateBufLevel(hwbufstats_t *pbufinfo)
{
    if (!pbufinfo || !mLowLevelBufMode || (LatestPlayerState < PLAYER_INITOK) || (LatestPlayerState > PLAYER_ERROR)) {
        return 0;
    }
    mHWaudiobufsize = pbufinfo->abufsize;
    mHWvideobufsize = pbufinfo->vbufsize;
    return 0;
}
int AmlogicPlayer::notifyhandle(int pid, int msg, unsigned long ext1, unsigned long ext2)
{
    AmlogicPlayer *player = (AmlogicPlayer *)player_get_extern_priv(pid);
    if (player != NULL) {
        return player->NotifyHandle(pid, msg, ext1, ext2);
    } else {
        return -1;
    }
}
int AmlogicPlayer::NotifyHandle(int pid, int msg, unsigned long ext1, unsigned long ext2)
{
    player_file_type_t *type;
    int ret;
    switch (msg) {
    case PLAYER_EVENTS_PLAYER_INFO:
        return UpdateProcess(pid, (player_info_t *)ext1);
        break;
    case PLAYER_EVENTS_STATE_CHANGED:
    case PLAYER_EVENTS_ERROR:
    case PLAYER_EVENTS_BUFFERING:
        break;
    case PLAYER_EVENTS_NOT_SUPPORT_SEEKABLE:
        mSupportSeek = 0;
        break;
    case PLAYER_EVENTS_FILE_TYPE: {
        type = (player_file_type_t *)ext1;
        mhasAudio = type->audio_tracks;
        mhasVideo = type->video_tracks;
        strncpy(mTypeStr, type->fmt_string, 64);
        mTypeStr[63] = '\0';
        LOGV("Type=%s,videos=%d,audios=%d\n", type->fmt_string, mhasVideo, mhasAudio);
        if (!strcmp(type->fmt_string, "DRMdemux")) {
            isWidevineStreaming = true;
            LOGV("It is WidevineStreaming!\n");
        }
        if (!strcmp(type->fmt_string, "Demux_no_prot")) {
            isSmoothStreaming = true;
            LOGV("It is SmoothStreaming!\n");
        }
        if (!PropIsEnable("media.amplayer.hdmicloseauthen") && isWidevineStreaming) {
            ret = amvideo_utils_get_hdmi_authenticate();
            LOGV("hdcp authenticate : %d\n", ret);
            if (ret == HDMI_HDCP_FAILED) {
                isHDCPFailed = true;
                LOGV("hdcp authenticate failed, it will close video!\n");
            }
        }
        if (isWidevineStreaming) {
            //reset map
            char newsetting[128];
            char value[PROPERTY_VALUE_MAX];
            int ret = 1;
            if (property_get("media.decoder.vfm.drmmap", value, NULL) > 0) {
                ret = IsTheSameVfmPathDefault(value);
                if (ret < 0) {
                    LOGE("reset DRM maping [%s]\n", value);
                    strcpy(newsetting, "add default ");
                    strcat(newsetting, value);
                    amsysfs_set_sysfs_str("/sys/class/vfm/map", "rm default");
                    amsysfs_set_sysfs_str("/sys/class/vfm/map", newsetting);
                }
            }
        }
        mTypeReady = true;
        sendEvent(0x11000);
        if (strstr(mTypeStr, "mpeg") != NULL) { /*mpeg,ts,ps\,may can't detect types here.*/
            mhasVideo = 1;
            mhasAudio = 1;
        } else if ((mDecryptHandle == NULL) &&
                   (strstr(mTypeStr, "DRMdemux") != NULL)) {
            if (mSouceProtocol.get() != NULL) {
                mSouceProtocol->getDrmInfo(mDecryptHandle, &mDrmManagerClient);

                if (mDecryptHandle == NULL) {
                    LOGE("after getDrmInfo, mDecryptHandle = NULL");
                }
                if (mDrmManagerClient == NULL) {
                    LOGE("after getDrmInfo, mDrmManagerClient = NULL");
                }

                if (mDecryptHandle != NULL && mDrmManagerClient != NULL) {
                    LOGV("L%d:getmDecryptHandle", __LINE__);
                    if (RightsStatus::RIGHTS_VALID != mDecryptHandle->status) {
                        //notifyListener_l(MEDIA_ERROR, MEDIA_ERROR_UNKNOWN, ERROR_DRM_NO_LICENSE);
                        LOGE("L%d:getDrmInfo error", __LINE__);
                    } else {
                        mDrmManagerClient->setPlaybackStatus(mDecryptHandle,
                                                             Playback::START, (int64_t)(mPlay_ctl.t_pos * 1000));
                    }
                }
            }
        }
        break;
    }
    case PLAYER_EVENTS_HTTP_WV: {
        mHttpWV = true;
        sendEvent(0x12000);
        LOGV("Get http wvm, goto WVM Extractor");
    }
    break;
    case PLAYER_EVENTS_HWBUF_DATA_SIZE_CHANGED: {
        hwbufstats_t *pbufinfo = (hwbufstats_t *)ext1;
        UpdateBufLevel(pbufinfo);
    }
    break;
    case PLAYER_EVENTS_VIDEO_SIZE_CHANGED: {
        if (PropIsEnable("media.amplayer.vs.change.msg.on", 1) && !isSmoothStreaming) {
            mWidth = ext1;
            mHeight = ext2;
            if (mAspect_ratio_num > mAspect_ratio_den) {
                mWidth = mWidth * mAspect_ratio_num / mAspect_ratio_den;
            } else {
                mHeight = mHeight * mAspect_ratio_den / mAspect_ratio_num;
            }
            if (video_rotation_degree == 1 || video_rotation_degree == 3) {
                sendEvent(MEDIA_SET_VIDEO_SIZE, mHeight, mWidth);    // 90du,or 270du
            } else {
                sendEvent(MEDIA_SET_VIDEO_SIZE, mWidth, mHeight);
            }
        }
    }
    break;
    case PLAYER_EVENTS_SUBTITLE_DATA: {
        AVSubtitleData * sub = (AVSubtitleData *)ext1;
        Parcel in;
        in.writeInt32(sub->sub_trackIndex);
        in.writeInt64(sub->sub_timeUs);
        in.writeInt64(sub->sub_durationUs);
        in.writeInt32(sub->sub_size);
        in.writeInt32(sub->sub_size);
        in.write(sub->sub_buffer, sub->sub_size);
        sendEvent(MEDIA_SUBTITLE_DATA, 0, 0, &in);
        free(sub->sub_buffer);
        free(sub);
    }
    break;
    case PLAYER_EVENTS_BLURAY_INFO: {
        LOGD("PLAYER_EVENTS_BLURAY_INFO");
        bluray_info_t *info = (bluray_info_t *)ext1;
        switch (info->info) {
        case BLURAY_STREAM_PATH: {
            if (mParcel != NULL) {
                delete mParcel;
            }
            mParcel = new Parcel();
            mParcel->freeData();
            mParcel->writeString16(String16(info->stream_path));
            mParcel->writeInt32(info->stream_info_num);
            for (int i = 0; i < info->stream_info_num; i++) {
                mParcel->writeInt32(info->stream_info[i].type);
                mParcel->writeString16(String16(info->stream_info[i].lang));
            }
            mParcel->writeInt32(info->chapter_num);
            for (int i = 0; i < info->chapter_num; i++) {
                mParcel->writeInt32(info->chapter_info[i].start);
                mParcel->writeInt32(info->chapter_info[i].duration);
            }

            if (mParcel->dataSize() > 0) {
                sendEvent(MEDIA_BLURAY_INFO, MEDIA_INFO_AMLOGIC_BLURAY_STREAM_PATH, 0, mParcel);
            } else {
                sendEvent(MEDIA_BLURAY_INFO, MEDIA_INFO_AMLOGIC_BLURAY_STREAM_PATH, 0, NULL);
            }
            break;
        }
        default:
            break;
        }
        break;
    }
    default:
        break;
    }
    return 0;
}



#define DTSM6_EXCHANGE_INFO_NODE "/sys/class/amaudio/debug"
static void dtsm6_get_exchange_info(int *streamtype, int *APreCnt, int *APreSel, int *ApreAssetSel, int32_t *ApresAssetsArray, int *MulAssetHint, int *HPs_hint)
{
    int fd = open(DTSM6_EXCHANGE_INFO_NODE,  O_RDWR | O_TRUNC, 0644);
    int bytes = 0, i;
    if (fd >= 0) {
        uint8_t ubuf8[256] = {0};
        bytes = read(fd, ubuf8, 256);

        if (streamtype != NULL) {
            uint8_t *pStreamType = (uint8_t *)strstr((const char*)ubuf8, "StreamType");
            if (pStreamType != NULL) {
                pStreamType += 10;
                *streamtype = atoi((const char*)pStreamType);
            }
        }

        if (APreCnt != NULL) {
            uint8_t *pApreCnt = (uint8_t *)strstr((const char*)ubuf8, "ApreCnt");
            if (pApreCnt != NULL) {
                pApreCnt += 7;
                *APreCnt = atoi((const char*)pApreCnt);
            }
        }

        if (APreSel != NULL) {
            uint8_t *pApreSel = (uint8_t *)strstr((const char*)ubuf8, "ApreSel");
            if (pApreSel != NULL) {
                pApreSel += 7;
                *APreSel = atoi((const char*)pApreSel);
            }
        }

        if (ApreAssetSel != NULL) {
            uint8_t *pApreAssetSel = (uint8_t *)strstr((const char*)ubuf8, "ApreAssetSel");
            if (pApreAssetSel != NULL) {
                pApreAssetSel += 12;
                *ApreAssetSel = atoi((const char*)pApreAssetSel);
            }
        }

        if (ApresAssetsArray != NULL && APreCnt != NULL) {
            uint8_t *pApresAssetsArray = (uint8_t *)strstr((const char*)ubuf8, "ApresAssetsArray");
            if (pApresAssetsArray != NULL) {
                pApresAssetsArray += 16;
                for (i = 0; i < *APreCnt; i++) {
                    ApresAssetsArray[i] = pApresAssetsArray[i];
                    LOGI("[%s %d]ApresAssetsArray[%d]/%d", __FUNCTION__, __LINE__, i, ApresAssetsArray[i]);
                }
            }
        }
        if (MulAssetHint != NULL) {
            uint8_t *pMulAssetHint = (uint8_t *)strstr((const char*)ubuf8, "MulAssetHint");
            if (pMulAssetHint != NULL) {
                pMulAssetHint += 12;
                *MulAssetHint = atoi((const char*)pMulAssetHint);
            }
        }
        if (HPs_hint != NULL) {
            uint8_t *phps_hint = (uint8_t *)strstr((const char*)ubuf8, "HPSHint");
            if (phps_hint != NULL) {
                phps_hint += 7;
                *HPs_hint = atoi((const char*)phps_hint);
            }
        }
        close(fd);
    } else {
        LOGI("[%s %d]open %s failed!\n", __FUNCTION__, __LINE__, DTSM6_EXCHANGE_INFO_NODE);
        if (streamtype != NULL) {
            *streamtype = 0;
        }
        if (APreCnt != NULL) {
            *APreCnt = 0;
        }
        if (APreSel != NULL) {
            *APreSel = 0;
        }
        if (ApreAssetSel != NULL) {
            *ApreAssetSel = 0;
        }
        if (HPs_hint != NULL) {
            *HPs_hint = 0;
        }
        if (ApresAssetsArray != NULL && APreCnt != NULL) {
            memset(ApresAssetsArray, 0, *APreCnt);
        }
    }
}

static void dtsm6_set_exchange_info(int *APreSel, int *ApreAssetSel)
{
    int fd = open(DTSM6_EXCHANGE_INFO_NODE,  O_RDWR | O_TRUNC, 0644);
    int bytes, pos = 0;
    if (fd >= 0) {
        char ubuf8[128] = {0};
        if (APreSel != NULL) {
            bytes = sprintf(ubuf8, "dtsm6_apre_sel_set%d", *APreSel);
            write(fd, ubuf8, bytes);
        }
        if (ApreAssetSel != NULL) {
            bytes = sprintf(ubuf8, "dtsm6_apre_assets_sel_set%d", *ApreAssetSel);
            write(fd, ubuf8, bytes);
        }
        close(fd);
    } else {
        LOGI("[%s %d]open %s failed!\n", __FUNCTION__, __LINE__, DTSM6_EXCHANGE_INFO_NODE);
    }
}
int AmlogicPlayer::UpdateProcess(int pid, player_info_t *info)
{

    LOGV("update_process pid=%d, current=%d,status=[%s]\n", pid, info->current_time, player_status2str(info->status));
    if (mIgnoreMsg && info->status != PLAYER_ERROR) {
        return 0;
    }

    LatestPlayerState = info->status;
    mHWvideobuflevel = info->video_bufferlevel;
    mHWaudiobuflevel = info->audio_bufferlevel;
    if (info->status != PLAYER_ERROR && info->error_no != 0) {
        if (info->error_no == PLAYER_NO_VIDEO) {
            sendEvent(MEDIA_INFO, MEDIA_INFO_AMLOGIC_NO_VIDEO);
            LOGW("player no video\n");
        } else if (info->error_no == PLAYER_NO_AUDIO) {
            sendEvent(MEDIA_INFO, MEDIA_INFO_AMLOGIC_NO_AUDIO);
            LOGW("player no audio\n");
        } else if (info->error_no == PLAYER_UNSUPPORT_VCODEC || info->error_no == PLAYER_UNSUPPORT_VIDEO) {
            LOGW("player video not supported\n");
            sendEvent(MEDIA_INFO, MEDIA_INFO_AMLOGIC_VIDEO_NOT_SUPPORT);
        } else if (info->error_no == PLAYER_UNSUPPORT_ACODEC || info->error_no == PLAYER_UNSUPPORT_AUDIO) {
            LOGW("player audio not supported\n");
            sendEvent(MEDIA_INFO, MEDIA_INFO_AMLOGIC_AUDIO_NOT_SUPPORT);
            if (!mhasVideo) {
                sendEvent(MEDIA_PREPARED);
            }
        }
    } else if (info->status == PLAYER_BUFFERING) {
        if (mDuration > 0) {
            sendEvent(MEDIA_BUFFERING_UPDATE, mPlayTime * 100 / mDuration);
        }
        if (bufferTime <= 0) {
            bufferTime = ALooper::GetNowUs() / 1000;
        }
        if (!mInbuffering) {
            //mInbuffering = true;
            if (!mLowLevelBufMode) {
                if (mDelaySendBufferingInfo_s > 0) {
                    if (ALooper::GetNowUs() / 1000 - bufferTime > mDelaySendBufferingInfo_s * 1000) {
                        mInbuffering = true;
                        sendEvent(MEDIA_INFO, MEDIA_INFO_BUFFERING_START);
                    }
                } else {
                    mInbuffering = true;
                    sendEvent(MEDIA_INFO, MEDIA_INFO_BUFFERING_START);
                }
            }
        }
    } else if (info->status == PLAYER_INITOK) {
        updateMediaInfo();
        if (info->full_time_ms != -1) {
            mDuration = info->full_time_ms;
        } else if (info->full_time != -1) {
            mDuration = info->full_time * 1000;
        }
        if (video_rotation_degree == 1 || video_rotation_degree == 3) {
            sendEvent(MEDIA_SET_VIDEO_SIZE, mHeight, mWidth);    // 90du,or 270du
        } else {
            sendEvent(MEDIA_SET_VIDEO_SIZE, mWidth, mHeight);
        }
        if (!fastNotifyMode) { ///fast mode,will send before,do't send again
            sendEvent(MEDIA_PREPARED);
        }
        if (mDuration > 0) {
            sendEvent(MEDIA_BUFFERING_UPDATE, 1);/*add notify for some apk waiting.*/
        } else {
            sendEvent(MEDIA_BUFFERING_UPDATE, 0);
        }
    } else if (info->status == PLAYER_STOPED || info->status == PLAYER_PLAYEND) {
        LOGV("Player status:%s, playback complete", player_status2str(info->status));
        if (mHttpWV == false) {
            if ((!mEnded) && (info->status == PLAYER_PLAYEND)) {
                //sendEvent(MEDIA_PLAYBACK_COMPLETE);
                mShouldSendPlayComplete = true;
            }
        }
        if (info->current_ms >= 100) {
            LOGV("PLAYER_PLAYEND curtime:%d, \n", info->current_ms);
            mPlayTime = info->current_ms;
            mLastPlayTimeUpdateUS = ALooper::GetNowUs();
        }
        mEnded = true;
        mPaused = true;
    } else if (info->status == PLAYER_EXIT) {
        LOGV("Player status:%s, playback exit", player_status2str(info->status));
        mRunning = false;
        if (mHttpWV == false) {
            if (!mLoop && (mState != STATE_ERROR) && (!mEnded)) { //no errors & no loop^M
                //sendEvent(MEDIA_PLAYBACK_COMPLETE);
            }

            if (mShouldSendPlayComplete) {
                LOGV("[UpdateProcess]MEDIA_PLAYBACK_COMPLETE usleep 1000 * 10\n");
                usleep(1000 * 10);
                sendEvent(MEDIA_PLAYBACK_COMPLETE);
            }
        }
        mPaused = true;
        mEnded = true;
        if (isHDCPFailed == true) {
            set_sys_int(DISABLE_VIDEO, 2);
            isHDCPFailed = false;
            LOGV("[L%d]:Enable Video", __LINE__);
        }
        if (isWidevineStreaming) {
            //reset map
            char newsetting[128];
            char value[PROPERTY_VALUE_MAX];
            int ret = 1;
            if (property_get("media.decoder.vfm.defmap", value, NULL) > 0) {
                ret = IsTheSameVfmPathDefault(value);
                if (ret < 0) {
                    LOGE("reset DRM maping [%s]\n", value);
                    strcpy(newsetting, "add default ");
                    strcat(newsetting, value);
                    amsysfs_set_sysfs_str("/sys/class/vfm/map", "rm default");
                    amsysfs_set_sysfs_str("/sys/class/vfm/map", newsetting);
                }
            }
        }
    } else if (info->status == PLAYER_ERROR) {
        if (mHttpWV == false) {
            sendEvent(MEDIA_ERROR, MEDIA_ERROR_UNKNOWN, info->error_no);
            //sendEvent(MEDIA_ERROR,MEDIA_ERROR_UNKNOWN,info->error_no);
            LOGV("Player status:%s, error occur", player_status2str(info->status));
            //sendEvent(MEDIA_ERROR);
            //sendEvent(MEDIA_ERROR, MEDIA_ERROR_UNKNOWN, -1);
            mState = STATE_ERROR;
            if (isHDCPFailed == true) {
                set_sys_int(DISABLE_VIDEO, 2);
                isHDCPFailed = false;
                LOGV("[L%d]:Enable Video", __LINE__);
            }
        }
    } else {
        if (info->status == PLAYER_SEARCHING) {
            if (mDuration > 0) {
                sendEvent(MEDIA_BUFFERING_UPDATE, mPlayTime * 100 / mDuration);
            }
        }
        int percent = 0;
        if (mInbuffering && info->status != PLAYER_SEARCHING) {
            mInbuffering = false;
            sendEvent(MEDIA_INFO, MEDIA_INFO_BUFFERING_END);
            bufferTime = 0;
        }
        if (mFirstPlaying && info->status == PLAYER_RUNNING) {
            sendEvent(MEDIA_INFO, MEDIA_INFO_BUFFERING_END);
            mFirstPlaying = false;
        }
        if (mDuration > 0) {
            percent = (mPlayTime) * 100 / (mDuration);
        } else {
            percent = 0;
        }

        if (info->full_time_ms != -1) {
            mDuration = info->full_time_ms;
        } else if (info->full_time != -1) {
            mDuration = info->full_time * 1000;
        }

        mPlayTime = info->current_ms;
        mLastPlayTimeUpdateUS = ALooper::GetNowUs();

        if (info->current_pts != 0xffffffff) {
            mStreamTime = info->current_pts / 90; /*pts(90000hz)->ms*/
            mLastStreamTimeUpdateUS = ALooper::GetNowUs();
        }

        LOGV("Playing percent =%d,mPlayTime:%d,mStreamTime:%d\n", percent, mPlayTime, mStreamTime);
        if (streaminfo_valied && mDuration > 0 && info->bufed_time > 0) {
            percent = (info->bufed_time * 100 / (mDuration / 1000));
            LOGV("Playing percent on percent=%d,bufed time=%dS,Duration=%dS\n", percent, info->bufed_time, mDuration / 1000);
        } else if (streaminfo_valied && mDuration > 0 && info->bufed_pos > 0 && mStreamInfo.stream_info.file_size > 0) {

            percent = (info->bufed_pos * 100 / (mStreamInfo.stream_info.file_size));
            LOGV("Playing percent on percent=%d,bufed pos=%" PRId64 ",Duration=%" PRId64 "\n", percent, info->bufed_pos, (mStreamInfo.stream_info.file_size));
        } else if (mDuration > 0 && streaminfo_valied && mStreamInfo.stream_info.file_size > 0) {
            percent += ((long long)4 * 1024 * 1024 * 100 * info->audio_bufferlevel / mStreamInfo.stream_info.file_size);
            percent += ((long long)6 * 1024 * 1024 * 100 * info->video_bufferlevel / mStreamInfo.stream_info.file_size);
            /*we think the lowlevel buffer size is alsways 10M */
            LOGV("Playing buffer percent =%d\n", percent);
        } else {
            //percent+=info->audio_bufferlevel*4;
            //percent+=info->video_bufferlevel*6;
        }
        if (percent > 100) {
            percent = 100;
        } else if (percent < 0) {
            percent = 0;
        }

        if (info->status == PLAYER_SEARCHOK) {
            sendEvent(MEDIA_SEEK_COMPLETE);
            if (mFFStatus) {
                mFFStatus = false;
            }
        }

        if (mDuration > 0 && !mLowLevelBufMode) {
            sendEvent(MEDIA_BUFFERING_UPDATE, percent);
        }

    }
    if (info->status == PLAYER_START && mSetVolumeFlag == 1) {
        audio_set_lrvolume(mPlayer_id, mLeftVolume, mRightVolume);
        mSetVolumeFlag = 0;
    }
    if (AudioDualMonoNeed && AudioDualMonoSetOK != 1) {
        AudioDualMonoSetOK = !audio_lr_mix_set(mPlayer_id, 1);
    }
    return 0;
}
status_t AmlogicPlayer::GetFileType(char **typestr, int *videos, int *audios)
{
    if (!mTypeReady) {
        return ERROR_NOT_OPEN;
    }

    LOGV("GetFileType---Type=%s,videos=%d,audios=%d\n", mTypeStr, mhasVideo, mhasAudio);
    *typestr = mTypeStr;

    *videos = mhasVideo;

    *audios = mhasAudio;
    if (mSouceProtocol.get() != NULL) {
        mSouceProtocol->getDrmInfo(mDecryptHandle, &mDrmManagerClient);
        if (mDecryptHandle != NULL && mDrmManagerClient != NULL) {
            LOGV("L%d:getmDecryptHandle", __LINE__);
            if (RightsStatus::RIGHTS_VALID != mDecryptHandle->status) {
                //notifyListener_l(MEDIA_ERROR, MEDIA_ERROR_UNKNOWN, ERROR_DRM_NO_LICENSE);
                LOGE("L%d:getDrmInfo error", __LINE__);
            } else {
                mDrmManagerClient->setPlaybackStatus(mDecryptHandle,
                                                     Playback::START, (int64_t)(mPlay_ctl.t_pos * 1000));
            }
        }
    }

    return NO_ERROR;
}

int AmlogicPlayer::isUseExternalModule(const char* mod_name)
{
    int ret = -1;
    const char* ex_mod = "media.libplayer.modules";
    char value[PROPERTY_VALUE_MAX];
    ret = property_get(ex_mod, value, NULL);
    if (ret < 1) {
        return 0;
    }
    ret = ammodule_match_check(value, mod_name);

    if (ret > 0) {
        return 1;
    } else {
        return 0;
    }

}
status_t AmlogicPlayer::setdatasource(const char *path, int fd, int64_t offset, int64_t length, const KeyedVector<String8, String8> *headers)
{
    int num;
    char * file = NULL;

    if (path == NULL) {
        if (fd < 0 || offset < 0) {
            return -1;
        }
        file = (char *)malloc(128);
        if (file == NULL) {
            return NO_MEMORY;
        }
        mAmlogicFile.oldfd = fd;
        mAmlogicFile.fd = dup(fd);
        mAmlogicFile.fd_valid = 1;
        mAmlogicFile.mOffset = offset;
        mAmlogicFile.mLength = length;
        mPlay_ctl.t_pos = -1; /*don't seek to 0*/
        //mPlay_ctl.t_pos=0;/*don't seek to 0*/
        sprintf(file, "android:AmlogicPlayer=[%x:%x],AmlogicPlayer_fd=[%x:%x]",
                (unsigned int)this, (~(unsigned int)this),
                (unsigned int)&mAmlogicFile, (~(unsigned int)&mAmlogicFile));
    } else {
        int time;
        file = (char *)malloc(strlen(path) + 10);
        if (file == NULL) {
            return NO_MEMORY;
        }
#ifdef LIVEPLAY_SEEK
        float tmp_pos = 0.0;
        mPlay_ctl.is_livemode = 0;
#endif

        if (strncmp(path, "http", strlen("http")) == 0 && !IsManifestUrl(path)) {
#ifdef LIVEPLAY_SEEK
            char *tmp = strstr(path, "starttime");
            if (tmp) {
                tmp += 10;
                tmp_pos = atof(tmp);
            }
            tmp = strstr(path, "livemode=1");
            if (tmp) {
                mPlay_ctl.is_livemode = 1;
            }
#endif
            if (IS_M3U8_URL(path)) {
                if (isUseExternalModule("vhls_mod") > 0) {
                    num = sprintf(file, "vhls:s%s", path);
                } else {
                    num = sprintf(file, "list:s%s", path);
                }
                file[num] = '\0';
            } else {
                //http->shttp
                num = sprintf(file, "s%s", path);
                file[num] = '\0';
            }
        } else {
            num = sprintf(file, "%s", path);
            file[num] = '\0';
        }
        time = HistoryMgt(file, 0, 0);
        if (time > 0) {
            mPlay_ctl.t_pos = time;
        } else {
            mPlay_ctl.t_pos = -1;
        }
#ifdef LIVEPLAY_SEEK
        if (tmp_pos > 0) {
            mPlay_ctl.t_pos = tmp_pos / 1000.0;
        }
#endif
        if (mPlay_ctl.headers) {
            free(mPlay_ctl.headers);
            mPlay_ctl.headers = NULL;
        }
        if (headers) {
            //one huge string of the HTTP headers to add
            int len = 0;
            for (size_t i = 0; i < headers->size(); ++i) {
                len += strlen(headers->keyAt(i));
                len += strlen(": ");
                len += strlen(headers->valueAt(i));
                len += strlen("\r\n");
            }
            len += 1;
            mPlay_ctl.headers = (char *)malloc(len);
            if (mPlay_ctl.headers) {
                mPlay_ctl.headers[0] = 0;
                for (size_t i = 0; i < headers->size(); ++i) {
                    strcat(mPlay_ctl.headers, headers->keyAt(i));
                    strcat(mPlay_ctl.headers, ": ");
                    strcat(mPlay_ctl.headers, headers->valueAt(i));
                    strcat(mPlay_ctl.headers, "\r\n");
                }
                mPlay_ctl.headers[len - 1] = '\0';
            }
        }
        if (strncmp(path, "http", strlen("http")) == 0 ||
            strncmp(path, "shttp", strlen("shttp")) == 0 ||
            strncmp(path, "https", strlen("https")) == 0 ||
            strncmp(path, "rtsp", strlen("rtsp")) == 0 ||
            strncmp(path, "mms", strlen("mms")) == 0 ||
            strncmp(path, "ftp", strlen("ftp")) == 0 ||
            strncmp(path, "widevine", strlen("widevine")) == 0 ||
            IsVrVmUrl(path)) { /*if net work mode ,enable buffering*/
            mPlay_ctl.auto_buffing_enable = 1;
        }
        LOGV("setDataSource enable buffering\n");
    }
    mPlay_ctl.need_start = 1;
    mAmlogicFile.datasource = file;
    mPlay_ctl.file_name = (char*)mAmlogicFile.datasource;
    if (PropIsEnable("media.amplayer.disp_url", true)) {
        LOGV("setDataSource url=%s, len=%d\n", mPlay_ctl.file_name, strlen(mPlay_ctl.file_name));
    }
    mState = STATE_OPEN;
    return NO_ERROR;

}




status_t AmlogicPlayer::prepare()
{
    LOGV("prepare\n");
    if (prepareAsync() != NO_ERROR) {
        return UNKNOWN_ERROR;
    }
    while (player_get_state(mPlayer_id) != PLAYER_INITOK) {
        if ((player_get_state(mPlayer_id)) == PLAYER_ERROR ||
            player_get_state(mPlayer_id) == PLAYER_STOPED ||
            player_get_state(mPlayer_id) == PLAYER_PLAYEND ||
            player_get_state(mPlayer_id) == PLAYER_EXIT
           ) {
            return UNKNOWN_ERROR;
        }
        usleep(1000 * 10);
    }
    return NO_ERROR;
}


status_t AmlogicPlayer::prepareAsync()
{
    int check_is_playlist = -1;
    float level = PropGetFloat("media.amplayer.lpbufferlevel");
    float buftime = PropGetFloat("media.amplayer.buffertime", 5);
    float delaybuffering = (int)PropGetFloat("media.amplayer.delaybuffering");
    LOGV("prepareAsync\n");
    if (!strncasecmp("tvin:", mPlay_ctl.file_name, 5)) {
        return NO_ERROR;
    }
    mPlay_ctl.callback_fn.notify_fn = notifyhandle;
    mPlay_ctl.callback_fn.update_interval = 300;
    mPlay_ctl.audio_index = -1;
    mPlay_ctl.video_index = -1;
    mPlay_ctl.hassub = 1;  //enable subtitle
    mPlay_ctl.is_type_parser = 1;
    mPlay_ctl.lowbuffermode_limited_ms = mStopFeedingBuf_ms;
    mPlay_ctl.buffing_min = (level < 0.001 && level > 0.0) ? level / 10 : 0.001;
    mPlay_ctl.buffing_middle = level > 0 ? level : 0.02;
    mPlay_ctl.buffing_max = level < 0.8 ? 0.8 : level;
    mPlay_ctl.buffing_starttime_s = buftime;
    if (delaybuffering > 0) {
        mPlay_ctl.buffing_force_delay_s = delaybuffering;
    }
    if (mLowLevelBufMode) {
        mPlay_ctl.auto_buffing_enable = 0;
        mPlay_ctl.enable_rw_on_pause = 0; /**/
        mPlay_ctl.lowbuffermode_flag = 1;
    } else {
        mPlay_ctl.enable_rw_on_pause = 1;
    }
    mPlay_ctl.read_max_cnt = 10000; /*retry num*/
    mPlay_ctl.nosound = PropIsEnable("media.amplayer.noaudio") ? 1 : 0;
    mPlay_ctl.novideo = PropIsEnable("media.amplayer.novideo") ? 1 : 0;
    mPlay_ctl.displast_frame = PropIsEnable("media.amplayer.displast_frame") ? 1 : 0;
    mPlay_ctl.SessionID = mSessionID;
    streaminfo_valied = false;
    LOGV("buffer level setting is:%f-%f-%f\n",
         mPlay_ctl.buffing_min,
         mPlay_ctl.buffing_middle,
         mPlay_ctl.buffing_max
        );
    if (PropIsEnable("media.amplayer.disp_url", true)) {
        LOGV("prepareAsync,file_name=%s\n", mPlay_ctl.file_name);
    }
    mPlayer_id = player_start(&mPlay_ctl, (unsigned long)this);
    if (mPlayer_id >= 0) {
        LOGV("Start player,pid=%d\n", mPlayer_id);
        if (fastNotifyMode  && !mIgnoreMsg) {
            sendEvent(MEDIA_PREPARED);
        }
        return NO_ERROR;
    }
    return UNKNOWN_ERROR;
}

status_t AmlogicPlayer::start()
{
    LOGV("start\n");
    if (mState != STATE_OPEN) {
        return ERROR_NOT_OPEN;
    }
    if (CallingAPkName[0] == '\0') {
        GetCallingAPKName(CallingAPkName, sizeof(CallingAPkName));
        LOGI("GetCallingAPKName calling apk name...[%s]\n", CallingAPkName);
    }
    if (mRunning && !mPaused) {
        return NO_ERROR;
    }

    if (mhasVideo && !mRunning) {
        VideoViewOn();
        initVideoSurface();
        if (isHDCPFailed == true) {
            set_sys_int(DISABLE_VIDEO, 1);
            LOGV("HDCP authenticate failed, Disable Video");
        }
    }

    if (mPlayerRender.get() != NULL) {
        LOGV("mPlayerRender->start\n");
        int ret = mPlayerRender->Start();
        if (ret != OK) {
            LOGE("mPlayerRender->start error\n");
            return ERROR_OPEN_FAILED;

        }
    }

    player_start_play(mPlayer_id);

    if (mPaused) {
        if (mDecryptHandle != NULL) {
            mDrmManagerClient->setPlaybackStatus(mDecryptHandle,
                                                 Playback::RESUME, 0);
        }
        if (mNeedResetOnResume) {
            player_timesearch(mPlayer_id, -1);
        }
        player_resume(mPlayer_id);
        mNeedResetOnResume = false;
    }

    if (mhasAudio) {
        SetCpuScalingOnAudio(2);
        mChangedCpuFreq = true;
    }
    mPaused = false;
    mRunning = true;
    mEnded = false;
    mShouldSendPlayComplete = false;
    mOffsetBacFlag = false;
    mOffsetBac = 0;
    mLatestPauseState = false;
    mLastPlayTimeUpdateUS = ALooper::GetNowUs();
    mDelayUpdateTime = 1;

    /*
    //sendEvent(MEDIA_PLAYER_STARTED);
    // wake up render thread
    //sub ops
    if (mTextDriver != NULL) {
        status_t ret;
        ret = mTextDriver->start();
        LOGE("sub start ret:%d \n", ret);
    }else{
        //may has sub but we can not support
        //so we just set invalid num
        //player_sid(mPlayer_id, 0xffff);
    }*/
    sendEvent(MEDIA_STARTED, 0, 0);
    return NO_ERROR;
}

status_t AmlogicPlayer::stop()
{
    LOGV("stop\n");
    if (mState != STATE_OPEN) {
        return ERROR_NOT_OPEN;
    }
    if (mDecryptHandle != NULL) {
        mDrmManagerClient->setPlaybackStatus(mDecryptHandle,
                                             Playback::STOP, 0);
    }
    if (mPlayerRender.get() != NULL) {
        mPlayerRender->Stop();
    }
    /*
    //stop textdriver
    if (mTextDriver != NULL) {
        delete mTextDriver;
        mTextDriver = NULL;
        if (mSubSource != NULL) {
            SubSource *sub_src = (SubSource *)mSubSource.get();
            sub_src->stop();
        }
        mSubSource = NULL;
        LOGV("delete TextDriver\n");
    }*/
    mPaused = true;
    mRunning = false;
    mLatestPauseState = true;
    player_stop(mPlayer_id);
    mEnded = true;
    mShouldSendPlayComplete = false;
    mOffsetBacFlag = false;
    mOffsetBac = 0;
    ///sendEvent(MEDIA_PLAYBACK_COMPLETE);

    if (mAuto3DDetected) {
        mAuto3DDetected = false;
        mSeekPos = 0;
        amSCsetDisplay3DFormat(0); // close 3D
    }
    return NO_ERROR;
}

status_t AmlogicPlayer::seekTo(int position)
{
    if (position < 0) {
        /*cancel seek*/
        return NO_ERROR;
    }

    if (position < mPlayTime + 10 && position >= mPlayTime - 10) {
        sendEvent(MEDIA_SEEK_COMPLETE);
        return NO_ERROR;/*ignore the same same time seek.*/
    }
#if 0

    int time = position / 1000;
    LOGV("seekTo:%d\n", position);
    player_timesearch(mPlayer_id, time);
    if (!mRunning) { /*have  not start,we tell it seek end*/
        sendEvent(MEDIA_SEEK_COMPLETE);
    }
#endif

    if (mStreamInfo.stream_info.adif_file_flag == 1) {
        LOGI("mStreamInfo.stream_info.adif_file_flag=%d\n", mStreamInfo.stream_info.adif_file_flag);
        LOGI("NOTE:adif_aac seek forbiddend!!\n");
        sendEvent(MEDIA_SEEK_COMPLETE);
        return NO_ERROR;
    }
    if (isSmoothStreaming && mhasVideo == 0 && mhasAudio == 1) {
        LOGI("NOTE:SmoothStreaming pure audio seek forbiddend!!\n");
        sendEvent(MEDIA_SEEK_COMPLETE);
        return NO_ERROR;
    }

    mSeekPos = ALooper::GetNowUs();
    mAuto3DDetected = false;

    LOGI("seekTo:%d,player_get_state=%x,running=%d,Player time=%dms\n", position, player_get_state(mPlayer_id), mRunning, (int)(ALooper::GetNowUs() - PlayerStartTimeUS) / 1000);
    if (!mRunning || player_get_state(mPlayer_id) >= PLAYER_ERROR || player_get_state(mPlayer_id) == PLAYER_NOT_VALID_PID) {
        if (player_get_state(mPlayer_id) >= PLAYER_ERROR || player_get_state(mPlayer_id) == PLAYER_NOT_VALID_PID) {
            int watingrunning = 10;
            mIgnoreMsg = true;
            if (mDecryptHandle != NULL) {
                mDrmManagerClient->setPlaybackStatus(mDecryptHandle,
                                                     Playback::STOP, 0);
            }
            player_exit(mPlayer_id);
            mPlayer_id = -1;
            mPlay_ctl.t_pos = (float)position / 1000;
            mPlay_ctl.is_type_parser = 0;
            prepare();
            mIgnoreMsg = false;
            LatestPlayerState = PLAYER_INITOK;
            mEnded = false;
            if (mLatestPauseState == false) {
                mPaused = false;
            }
            if (mDecryptHandle != NULL) {
                mDrmManagerClient->setPlaybackStatus(mDecryptHandle,
                                                     Playback::START, position);
            }
            mDelayUpdateTime = 2;
            player_start_play(mPlayer_id);
            while (LatestPlayerState <= PLAYER_INITOK && watingrunning-- > 0) {
                usleep(100000);    /*wait player running*/
            }
            LatestPlayerState = PLAYER_RUNNING; /*make sure we are running,*/
            sendEvent(MEDIA_SEEK_COMPLETE);
            LOGI("seek watingrunning:%d,player_get_state=%x,running=%d,Player time=%dms\n", watingrunning, player_get_state(mPlayer_id), mRunning, (int)(ALooper::GetNowUs() - PlayerStartTimeUS) / 1000);
        } else {
            mDelayUpdateTime = 2;
            player_timesearch(mPlayer_id, (float)position / 1000);
            if (mDecryptHandle != NULL) {
                mDrmManagerClient->setPlaybackStatus(mDecryptHandle,
                                                     Playback::PAUSE, 0);
                mDrmManagerClient->setPlaybackStatus(mDecryptHandle,
                                                     Playback::START, position);
            }
        }
    } else {
        mDelayUpdateTime = 2;
        player_timesearch(mPlayer_id, (float)position / 1000);
    }
    mSeekdone = true;
    mPlayTime = position;
    mLastPlayTimeUpdateUS = ALooper::GetNowUs();
    return NO_ERROR;
}

status_t AmlogicPlayer::pause()
{
    LOGV("pause\n");
    if (mState != STATE_OPEN) {
        return ERROR_NOT_OPEN;
    }

    int flag = 0;
    if (mStreamInfo.stream_info.adif_file_flag) {
        LOGI("NOTE:adif_aac pause not allowed reset DSP!!\n");
        flag = mStreamInfo.stream_info.adif_file_flag;
    }
    if (isSmoothStreaming && mhasVideo == 0 && mhasAudio == 1) {
        LOGI("NOTE:SmoothStreaming pure audio pause not allowed reset DSP!!\n");
        flag = isSmoothStreaming;
    }
    if (mhasVideo || flag) { /*video mode,and no video,no audio*/
        if (mDecryptHandle != NULL) {
            mDrmManagerClient->setPlaybackStatus(mDecryptHandle,
                                                 Playback::PAUSE, 0);
        }
        player_pause(mPlayer_id);
    } else {
        mIgnoreMsg = true;
        mPlay_ctl.t_pos = mPlayTime / 1000;
        if (mDecryptHandle != NULL) {
            mDrmManagerClient->setPlaybackStatus(mDecryptHandle,
                                                 Playback::PAUSE, 0);
        }
        player_stop(mPlayer_id);
        player_exit(mPlayer_id);
        mPlayer_id = -1;
        mPlay_ctl.need_start = 1;
        prepare();
        mIgnoreMsg = false;
    }
    if (mPlayerRender.get() != NULL) {
        mPlayerRender->Pause();
    }
    mPaused = true;
    mLatestPauseState = true;
    if (mhasAudio && mChangedCpuFreq) {
        SetCpuScalingOnAudio(1);
        mChangedCpuFreq = false;
    }
    /*
    if (mTextDriver != NULL) {
        mTextDriver->pause();
    }*/
    LatestPlayerState = PLAYER_PAUSE;
    return NO_ERROR;
}

bool AmlogicPlayer::isPlaying()
{
    ///LOGV("isPlaying?----%d\n",mRender);
    if (!mPaused) {
        return mRunning;
    } else {
        return false;
    }
}
status_t    AmlogicPlayer::setPlaybackSettings(const AudioPlaybackRate& rate)
{
    Mutex::Autolock autoLock(mLock);
    TRACE();
    fastNotifyMode = 1;
    AudioPlaybackRate rate_local = rate;
    // cursory sanity check for non-audio and paused cases
    if ((rate.mSpeed != 0.f && rate.mSpeed < AUDIO_TIMESTRETCH_SPEED_MIN)
        || rate.mSpeed > AUDIO_TIMESTRETCH_SPEED_MAX
        || rate.mPitch < AUDIO_TIMESTRETCH_SPEED_MIN
        || rate.mPitch > AUDIO_TIMESTRETCH_SPEED_MAX) {
        return BAD_VALUE;
    }
    TRACE();
    status_t err = OK;
    if (rate.mSpeed == 0.f) {
        if (true & mRunning) {
            err = pause();
            mOffsetBacFlag = true;
        }
        if (err == OK) {
            // save settings (using old speed) in case player is resumed
            AudioPlaybackRate newRate = rate;
            newRate.mSpeed = mPlaybackSettings.mSpeed;
            mPlaybackSettings = newRate;
        }
        TRACE();
        return err;
    }
    mOffsetBacFlag = false;
    mOffsetBac = 0;
    if (!(true & mRunning)) {
        mIgnoreMsg = true;
        prepare();
        mIgnoreMsg = false;
        start();
        pause();
        usleep(100000);
    }
    int retry = 10;
retry:
    TRACE();
    err = audio_set_playback_rate(mPlayer_id, &rate_local);
    if (err != OK && retry-- > 0) {
        usleep(50000);
        goto retry;
    }
#if 0
    if (mAudioPlayer != NULL) {
        err = mAudioPlayer->setPlaybackRate(rate);
    }
#endif
    if (err == OK) {
        TRACE();
        mPlaybackSettings = rate;
        if (!(true & mRunning) || (mPaused == true)) {
            start();
        }
    }
    return err;
}
status_t AmlogicPlayer::getPlaybackSettings(AudioPlaybackRate *rate /* nonnull */)
{
    TRACE();
    /*TODO read the actual rate value from audio track */
    *rate = mPlaybackSettings;
    if (mPaused == true) {
        rate->mSpeed = 0.0f;
    }
    return OK;
}
const char* AmlogicPlayer::getStrAudioCodec(int type)
{
    const char* tmp = "unkown";
    switch (type) {
    case AFORMAT_MPEG:
        tmp = "MPEG";
        break;
    case AFORMAT_PCM_S16LE:
        tmp = "PCMS16LE";
        break;
    case AFORMAT_AAC:
        tmp = "AAC";
        break;
    case AFORMAT_AC3:
        tmp = "AC3";
        break;
    case AFORMAT_EAC3:
        tmp = "EAC3";
        break;
    case AFORMAT_ALAW:
        tmp = "ALAW";
        break;
    case AFORMAT_MULAW:
        tmp = "MULAW";
        break;
    case AFORMAT_DTS:
        tmp = "DTS";
        break;
    case AFORMAT_PCM_S16BE:
        tmp = "PCMS16BE";
        break;
    case AFORMAT_FLAC:
        tmp = "FLAC";
        break;
    case AFORMAT_COOK:
        tmp = "COOK";
        break;
    case AFORMAT_PCM_U8:
        tmp = "PCMU8";
        break;
    case AFORMAT_ADPCM:
        tmp = "ADPCM";
        break;
    case AFORMAT_AMR:
        tmp = "AMR";
        break;
    case AFORMAT_RAAC:
        tmp = "RAAC";
        break;
    case AFORMAT_WMA:
        tmp = "WMA";
        break;
    case AFORMAT_WMAPRO:
        tmp = "WMAPRO";
        break;
    case AFORMAT_PCM_BLURAY:
        tmp = "BLURAY";
        break;
    case AFORMAT_ALAC:
        tmp = "ALAC";
        break;
    case AFORMAT_VORBIS:
        tmp = "VORBIS";
        break;
    case AFORMAT_AAC_LATM:
        tmp = "AAC_LATM";
        break;
    case AFORMAT_APE:
        tmp = "APE";
        break;
    case AFORMAT_PCM_WIFIDISPLAY:
        tmp = "PCM_WIFIDISPLAY";
        break;
    case AFORMAT_DRA:
        tmp = "DRA";
        break;
    case AFORMAT_SIPR:
        tmp = "SIPR";
        break;
    case AFORMAT_MPEG1:
        tmp = "MP1";
        break;
    case AFORMAT_MPEG2:
        tmp = "MP2";
        break;
    }
    return tmp;
}

const char* AmlogicPlayer::getStrVideoCodec(int vtype)
{
    const char* tmp = "unkown";
    switch (vtype) {
    case VFORMAT_MPEG12:
        tmp = "MPEG12";
        break;
    case VFORMAT_MPEG4:
        tmp = "MPEG4";
        break;
    case VFORMAT_H264:
        tmp = "H264";
        break;
    case VFORMAT_MJPEG:
        tmp = "MJPEG";
        break;
    case VFORMAT_REAL:
        tmp = "REAL";
        break;
    case VFORMAT_JPEG:
        tmp = "JPEG";
        break;
    case VFORMAT_VC1:
        tmp = "VC1";
        break;
    case VFORMAT_AVS:
        tmp = "AVS";
        break;
    case VFORMAT_SW:
        tmp = "SW";
        break;
    case VFORMAT_H264MVC:
        tmp = "H264MVC";
        break;

    }
    return tmp;
}


status_t AmlogicPlayer::updateMediaInfo(void)
{
    bool audioIdxCheck = false;
    int ret;
    int i;
    if (mPlayer_id < 0) {
        return OK;
    }
    mInnerSubNum = 0;
    mAudioTrackNum = 0;


    ret = player_get_media_info(mPlayer_id, &mStreamInfo);
    if (ret != 0) {
        LOGV("player_get_media_info failed\n");
        return NO_INIT;
    }
    streaminfo_valied = true;
    const int buflen = 2048;
    char tmp[buflen + 4];
    int boffset = 0;

    mhasVideo = mStreamInfo.stream_info.total_video_num > 0 ? mStreamInfo.stream_info.total_video_num : 0;
    if (mStreamInfo.stream_info.total_video_num > 0 &&
        mStreamInfo.stream_info.cur_video_index >= 0) {
        memset(tmp, 0, buflen);
        ret = snprintf(tmp, buflen, "({");
        boffset += ret > 0 ? ret : 0;
        for (i = 0; i < mStreamInfo.stream_info.total_video_num; i ++) {
            if (mStreamInfo.video_info[i]) {
                if (mStreamInfo.video_info[i]->index == mStreamInfo.stream_info.cur_video_index) {
                    mWidth = mStreamInfo.video_info[i]->width;
                    mHeight = mStreamInfo.video_info[i]->height;
                    mAspect_ratio_num = mStreamInfo.video_info[i]->aspect_ratio_num;
                    mAspect_ratio_den = mStreamInfo.video_info[i]->aspect_ratio_den;
                    if (mAspect_ratio_num == 0 || mAspect_ratio_den == 0) {
                        mAspect_ratio_num = mAspect_ratio_den = 1;
                    }
                    if (mAspect_ratio_num > mAspect_ratio_den) {
                        mWidth = mWidth * mAspect_ratio_num / mAspect_ratio_den;
                    } else {
                        mHeight = mHeight * mAspect_ratio_den / mAspect_ratio_num;
                    }
                    video_rotation_degree = mStreamInfo.video_info[i]->video_rotation_degree;
                    LOGI("player current video info:w:%d,h:%d\n", mWidth, mHeight);
                    if (mStrCurrentVideoCodec) {
                        free(mStrCurrentVideoCodec);
                        mStrCurrentVideoCodec = NULL;
                    }
                    mStrCurrentVideoCodec = strdup(getStrVideoCodec(mStreamInfo.video_info[i]->format));
                    LOGI("player current video info:codec:%s\n", mStrCurrentVideoCodec);
                }

                ret = snprintf(tmp + boffset, buflen - boffset, "vid:%d,vcodec:%s,bitrate:%d", mStreamInfo.video_info[i]->id,
                               getStrVideoCodec(mStreamInfo.video_info[i]->format),
                               mStreamInfo.video_info[i]->bit_rate > 0 ? mStreamInfo.video_info[i]->bit_rate : mStreamInfo.stream_info.bitrate);
                boffset += ret > 0 ? ret : 0;
                if (i < mStreamInfo.stream_info.total_video_num) {

                    ret = snprintf(tmp + boffset, buflen - boffset, ";");
                    boffset += ret > 0 ? ret : 0;
                }

            }
        }

        ret = snprintf(tmp + boffset, buflen - boffset, "})");
        boffset += ret > 0 ? ret : 0;
        if (mVideoExtInfo) {
            free(mVideoExtInfo);
            mVideoExtInfo = NULL;
        }
        mVideoTrackNum = mStreamInfo.stream_info.total_video_num;
        if (strlen(tmp) > 0) {
            mVideoExtInfo = strdup(tmp);
        }

    }

    boffset = 0;

    mhasAudio = mStreamInfo.stream_info.total_audio_num > 0 ? mStreamInfo.stream_info.total_audio_num : 0;
    if (mStreamInfo.stream_info.total_audio_num > 0) {
        memset(tmp, 0, buflen);
        ret = snprintf(tmp, buflen - boffset, "({");
        boffset += ret > 0 ? ret : 0;
        for (i = 0; i < mStreamInfo.stream_info.total_audio_num  && boffset < buflen - 32 ; i ++) {
            if (mStreamInfo.audio_info[i]) {
                if (mhasVideo) {
                    audioIdxCheck = (mStreamInfo.stream_info.cur_audio_index >= 0 && mStreamInfo.audio_info[i]->index == mStreamInfo.stream_info.cur_audio_index);
                } else {
                    audioIdxCheck = (mStreamInfo.stream_info.cur_audio_index >= 0);
                }
                if (audioIdxCheck) {
                    if (mStrCurrentAudioCodec) {
                        free(mStrCurrentAudioCodec);
                        mStrCurrentAudioCodec = NULL;
                    }
                    mStrCurrentAudioCodec = strdup(getStrAudioCodec(mStreamInfo.audio_info[i]->aformat));
                }

                ret = snprintf(tmp + boffset, buflen - boffset, "aid:%d,acodec:%s,bitrate:%d,samplerate:%d", mStreamInfo.audio_info[i]->id, getStrAudioCodec(mStreamInfo.audio_info[i]->aformat), mStreamInfo.audio_info[i]->bit_rate, mStreamInfo.audio_info[i]->sample_rate);
                boffset += ret > 0 ? ret : 0;
                if (i < mStreamInfo.stream_info.total_audio_num) {

                    ret = snprintf(tmp + boffset, buflen - boffset, ";");
                    boffset += ret > 0 ? ret : 0;
                }
            }
        }
        ret = snprintf(tmp + boffset, buflen - boffset, "})");
        boffset += ret > 0 ? ret : 0;
        if (mAudioExtInfo) {
            free(mAudioExtInfo);
            mAudioExtInfo = NULL;
        }
        if (strlen(tmp) > 0) {
            mAudioExtInfo = strdup(tmp);
        }
        mAudioTrackNum = mStreamInfo.stream_info.total_audio_num;

    }

    boffset = 0;
    mhasSub = mStreamInfo.stream_info.total_sub_num > 0 ? mStreamInfo.stream_info.total_sub_num : 0;
    if (mStreamInfo.stream_info.total_sub_num > 0) {
        memset(tmp, 0, buflen);
        ret = snprintf(tmp, buflen - boffset, "({");
        boffset += ret > 0 ? ret : 0;
        for (i = 0; i < mStreamInfo.stream_info.total_sub_num; i ++) {
            if (mStreamInfo.sub_info[i] && mStreamInfo.sub_info[i]->internal_external == 0) {

                ret = snprintf(tmp + boffset, buflen - boffset, "sid:%d,lang:%s", mStreamInfo.sub_info[i]->id, mStreamInfo.sub_info[i]->sub_language);
                boffset += ret > 0 ? ret : 0;
                if (i < mStreamInfo.stream_info.total_sub_num) {

                    ret = snprintf(tmp + boffset, buflen - boffset, ";");
                    boffset += ret > 0 ? ret : 0;
                }
                mInnerSubNum++;
                //add inband sub, 3gpp support only,codec_id from ffmpeg
#define CODEC_ID_MOV_TEXT 0x17005
                if (mStreamInfo.sub_info[i]->sub_type == CODEC_ID_MOV_TEXT) { //CODEC_ID_MOV_TEXT
                    //if (mTextDriver == NULL) {
                    //    mTextDriver = new TimedTextDriver(mListener,mHTTPService);
                    //}
                    if (mSubSource == NULL) {
                        mSubSource = new SubSource;
                    }
                    SubSource *sub_src = (SubSource *)mSubSource.get();
                    if (sub_src->addType(i, 1) == -1) {
                        continue;
                    }

                    //ret = mTextDriver->addInBandTextSource(mStreamInfo.sub_info[i]->index, mSubSource);
                    LOGE("add inband sub index:%d id:%d , ret:%d \n", mStreamInfo.sub_info[i]->index, mStreamInfo.sub_info[i]->id, ret);
                }
            }
        }

        ret = snprintf(tmp + boffset, buflen - boffset, "})");
        boffset += ret > 0 ? ret : 0;
        if (mSubExtInfo) {
            free(mSubExtInfo);
            mSubExtInfo = NULL;
        }
        if (strlen(tmp) > 0) {
            mSubExtInfo = strdup(tmp);
        }
        LOGI("inner subtitle info:%s\n", mSubExtInfo);
    }
    return OK;
}

status_t AmlogicPlayer::getTrackInfo(Parcel* reply)
{
    //Mutex::Autolock autoLock(mLock);
    int mCurTrack = 0;
    int streamVail = 0;
    memset(mTrackMap, -1, sizeof(mTrackMap));

    size_t trackCount = mStreamInfo.stream_info.total_audio_num + mStreamInfo.stream_info.total_video_num + mStreamInfo.stream_info.total_sub_num;
    //if (mTextDriver != NULL) {
    //    trackCount += mTextDriver->countExternalTracks();
    //}
    //? fix it,need add subtitle.
    //trackCount+=
    LOGE("track_count:%d \n", trackCount);
    reply->writeInt32(trackCount);
    //**********************************
    // Each Item including following INfo
    // 1 fields
    // 2 type - a-v-s
    // 3 lang - und for now
    //**********************************
    char *lang = "und";

    for (int i = 0; i < mStreamInfo.stream_info.nb_streams; ++i) {
        streamVail = 0;
        if (mhasVideo) {
            for (int j = 0; j < mStreamInfo.stream_info.total_video_num; j++) {
                if (i == mStreamInfo.video_info[j]->index) {
                    reply->writeInt32(2);
                    reply->writeInt32(MEDIA_TRACK_TYPE_VIDEO);
                    reply->writeString16(String16("video/"));
                    reply->writeString16(String16("und"));

                    //reply->writeString16(String16(lang));
                    streamVail = 1;
                    mTrackMap[mCurTrack++] = i;
                    LOGE("found video index:%d  id:%d  i:%d \n", mStreamInfo.video_info[j]->index, mStreamInfo.video_info[j]->id, i);
                    //continue;
                    break;
                }
            }
        }
        if (mhasAudio) {
            for (int m = 0; m < mStreamInfo.stream_info.total_audio_num; m++) {
                char *p8tmp = NULL;
                if (i == mStreamInfo.audio_info[m]->index) {
                    reply->writeInt32(2);
                    reply->writeInt32(MEDIA_TRACK_TYPE_AUDIO);
                    reply->writeString16(String16("audio/"));

                    streamVail = 1;
                    if (strlen(mStreamInfo.audio_info[m]->language)) {
                        p8tmp = mStreamInfo.audio_info[m]->language;
                    } else {
                        p8tmp = lang;
                    }
                    reply->writeString16(String16(p8tmp));
                    mTrackMap[mCurTrack++] = i;
                    LOGE("found audio index:%d  id:%d  i:%d \n", mStreamInfo.audio_info[m]->index, mStreamInfo.audio_info[m]->id, i);
                    //continue;
                    break;
                }
            }
        }
        //need to judge type, support 3gpp inband sub only
        if (mhasSub) {
            for (int m = 0; m < mStreamInfo.stream_info.total_sub_num; m++) {
                char *p8tmp = NULL;
                if (i == mStreamInfo.sub_info[m]->index) {
                    reply->writeInt32(2);
                    reply->writeInt32(MEDIA_TRACK_TYPE_TIMEDTEXT);
                    streamVail = 1;
                    reply->writeString16(String16("text/"));
                    if (strlen(mStreamInfo.sub_info[m]->sub_language)) {
                        p8tmp = mStreamInfo.sub_info[m]->sub_language;
                    } else {
                        p8tmp = lang;
                    }
                    reply->writeString16(String16(p8tmp));
                    mTrackMap[mCurTrack++] = i;
                    LOGE("we found  sub index:%d  id:%d  i:%d \n", mStreamInfo.sub_info[m]->index, mStreamInfo.sub_info[m]->id, i);
                    break;
                    //continue;
                }
            }
        }

    }
    if (streamVail != 1) {
        reply->writeInt32(MEDIA_TRACK_TYPE_UNKNOWN);
    }
    //if (mTextDriver != NULL) {
    //mTextDriver->getExternalTrackInfo(reply);
    //}

    return OK;
}

int32_t AmlogicPlayer::getSelectedTrack(const Parcel& request) const
{
    size_t trackCount = mStreamInfo.stream_info.nb_streams;
    int32_t type = request.readInt32();
    int32_t trackId = -1;
    switch (type) {
    case MEDIA_TRACK_TYPE_VIDEO:
        if (mhasVideo) {
            for (int i = 0; i < mStreamInfo.stream_info.nb_streams; ++i) {
                for (int j = 0; j < mStreamInfo.stream_info.total_video_num; j++) {
                    if (i == mStreamInfo.video_info[j]->index) {
                        trackId = i;
                        return trackId;
                    }
                }
            }
        }
        break;
    case MEDIA_TRACK_TYPE_AUDIO:
        if (mhasAudio) {
            for (int i = 0; i < mStreamInfo.stream_info.nb_streams; ++i) {
                for (int j = 0; j < mStreamInfo.stream_info.total_audio_num; j++) {
                    if (i == mStreamInfo.audio_info[j]->index) {
                        trackId = i;
                        return trackId;
                    }
                }
            }
        }
        break;
    case MEDIA_TRACK_TYPE_TIMEDTEXT:
        //if (mhasSub && mTextDriver!= NULL) {
        //    return mTextDriver->getSelectedTrack();
        //}
        break;
    default:
        break;
    }
    return trackId;
}

status_t AmlogicPlayer::getStreamingTrackInfo(Parcel * reply) const
{
    int ret;
    int info_num = 0, i = 0;
    AVStreamInfo ** info_array = NULL;
    ret = player_get_streaming_track_info(mPlayer_id, &info_num, &info_array);
    if (ret) {
        LOGE("[%s:%d] get track info failed !", __FUNCTION__, __LINE__);
        return UNKNOWN_ERROR;
    }
    reply->writeInt32(info_num);
    for (; i < info_num; i++) {
        char * mime = NULL;
        if (!info_array[i]->stream_mime) {
            if (info_array[i]->stream_type == MEDIA_TRACK_TYPE_AUDIO) {
                mime = "audio/";
            } else if (info_array[i]->stream_type == MEDIA_TRACK_TYPE_VIDEO) {
                mime = "video/";
            } else {
                LOGE("invalid track(%d)!", info_array[i]->stream_type);
            }
        } else {
            mime = info_array[i]->stream_mime;
        }
        reply->writeInt32(2); // write something non-zero
        reply->writeInt32(info_array[i]->stream_type);
        reply->writeString16(String16(mime));
        if (info_array[i]->stream_lang) {
            reply->writeString16(String16(info_array[i]->stream_lang));
        } else {
            const char * lang = "und";
            reply->writeString16(String16(lang));
        }
        if (info_array[i]->stream_type == MEDIA_TRACK_TYPE_SUBTITLE) {
            reply->writeInt32(info_array[i]->stream_auto);
            reply->writeInt32(info_array[i]->stream_default);
            reply->writeInt32(info_array[i]->stream_forced);
        }
        // release info item
        if (info_array[i]->stream_lang) {
            free(info_array[i]->stream_lang);
        }
        if (info_array[i]->stream_mime) {
            free(info_array[i]->stream_mime);
        }
        free(info_array[i]);
    }
    free(info_array);
    return OK;
}

status_t AmlogicPlayer::selectStreamingTrack(int index, bool select) const
{
    int ret;
    ret = player_select_streaming_track(mPlayer_id, index, select);
    if (ret) {
        LOGE("[%s:%d] switch track(%d) failed !", __FUNCTION__, __LINE__, index);
        return UNKNOWN_ERROR;
    }
    return OK;
}

int AmlogicPlayer::getStreamingSelectedTrack(const Parcel& request) const
{
    int ret;
    int selected_index = -1;
    int type = request.readInt32();
    ret = player_get_streaming_selected_track(mPlayer_id, type, &selected_index);
    if (ret) {
        LOGE("[%s:%d] get selected track failed !", __FUNCTION__, __LINE__);
    }
    return selected_index;
}

void transferFileSize(int64_t size, char *filesize)
{
    //LOGE("[transferFileSize] size:%" PRId64 "\n", size);
    if (size <= 1024) {
        strcpy(filesize, "1KB");
    } else if (size <= 1024 * 1024) {
        size /= 1024;
        size += 1;
        sprintf(filesize, "%d", size);
        strcat(filesize, "KB");
    } else if (size > 1024 * 1024) {
        size /= 1024 * 1024;
        size += 1;
        sprintf(filesize, "%d", size);
        strcat(filesize, "MB");
    }
    //LOGE("[transferFileSize] filesize:%s\n", filesize);
}


/*
get media file details information

---------------------------
1 stream info
   filename-duration-filesize-bitrate-filetype

2 select info
   cur_vid - cur_aid - cur_sid

2 video info
   video count
   video1: index-id-format -width-height
   video2: index-id-format -width-height
   ...

3 audio info
   audio count
   audio1: index-id-format -channel - samplerate
   audio2: index-id-format -channel - samplerate
   ...

4 subtitle info
   subtitle count
   subtitle1: index-id-format-type-lan
---------------------------

*/
status_t AmlogicPlayer::getMediaInfo(Parcel* reply) const
{

    int datapos = reply->dataPosition();


    /*build stream info*/
    String8 result;
    const size_t SIZE = 256;
    char buffer[SIZE];
    char filesize[SIZE] = {0,};
    if (mStreamInfo.stream_info.filename != NULL) {
        reply->writeString16(String16(mStreamInfo.stream_info.filename));
    } else {
        reply->writeString16(String16("-1"));
    }
    if (mStreamInfo.stream_info.duration > 0) {
        reply->writeInt32(mStreamInfo.stream_info.duration);
    } else {
        reply->writeInt32(-1);
    }

    if (mStreamInfo.stream_info.file_size > 0) {
        transferFileSize((int64_t)mStreamInfo.stream_info.file_size, filesize);
        reply->writeString16(String16(filesize));
    } else {
        reply->writeString16(String16("-1"));
    }
    if (mStreamInfo.stream_info.bitrate > 0) {
        reply->writeInt32(mStreamInfo.stream_info.bitrate);
    } else {
        reply->writeInt32(-1);
    }
    reply->writeInt32(mStreamInfo.stream_info.type);
    ALOGV("--filename:%s duration:%d filesize:%" PRId64 " bitrate:%d \n", mStreamInfo.stream_info.filename, mStreamInfo.stream_info.duration, mStreamInfo.stream_info.file_size, mStreamInfo.stream_info.bitrate);
    /*select info*/
    reply->writeInt32(mStreamInfo.stream_info.cur_video_index);
    reply->writeInt32(mStreamInfo.stream_info.cur_audio_index);
    reply->writeInt32(mStreamInfo.stream_info.cur_sub_index);
    ALOGV("--cur video:%d cur audio:%d cur sub:%d \n", mStreamInfo.stream_info.cur_video_index, mStreamInfo.stream_info.cur_audio_index, mStreamInfo.stream_info.cur_sub_index);
    /*build video info*/
    reply->writeInt32(mStreamInfo.stream_info.total_video_num);
    for (int i = 0; i < mStreamInfo.stream_info.total_video_num; i ++) {
        reply->writeInt32(mStreamInfo.video_info[i]->index);
        reply->writeInt32(mStreamInfo.video_info[i]->id);
        reply->writeString16(String16(player_value2str("vformat", mStreamInfo.video_info[i]->format)));
        reply->writeInt32(mStreamInfo.video_info[i]->width);
        reply->writeInt32(mStreamInfo.video_info[i]->height);
        ALOGV("--video index:%d id:%d totlanum:%d width:%d height:%d \n", mStreamInfo.video_info[i]->index, mStreamInfo.video_info[i]->id, mStreamInfo.stream_info.total_video_num, mStreamInfo.video_info[i]->width, mStreamInfo.video_info[i]->height);
    }

    /*build audio info*/
    reply->writeInt32(mStreamInfo.stream_info.total_audio_num);
    for (int i = 0; i < mStreamInfo.stream_info.total_audio_num; i ++) {
        reply->writeInt32(mStreamInfo.audio_info[i]->index);
        reply->writeInt32(mStreamInfo.audio_info[i]->id);
        //reply->writeString16(String16(player_value2str("aformat", mStreamInfo.audio_info[i]->aformat)));
        reply->writeInt32(mStreamInfo.audio_info[i]->aformat);
        reply->writeInt32(mStreamInfo.audio_info[i]->channel);
        reply->writeInt32(mStreamInfo.audio_info[i]->sample_rate);
        ALOGV("--audio index:%d id:%d totlanum:%d channel:%d samplerate:%d \n", mStreamInfo.audio_info[i]->index, mStreamInfo.audio_info[i]->id, mStreamInfo.stream_info.total_audio_num, mStreamInfo.audio_info[i]->channel, mStreamInfo.audio_info[i]->sample_rate);
    }

    /*build subtitle info*/
    reply->writeInt32(mStreamInfo.stream_info.total_sub_num);
    for (int i = 0; i < mStreamInfo.stream_info.total_sub_num; i ++) {
        reply->writeInt32(mStreamInfo.sub_info[i]->index);
        reply->writeInt32(mStreamInfo.sub_info[i]->id);
        reply->writeInt32(mStreamInfo.sub_info[i]->sub_type);
        //if (mStreamInfo.sub_info[i]->sub_language != NULL)
        reply->writeString16(String16(mStreamInfo.sub_info[i]->sub_language));
    }

    reply->writeInt32(mStreamInfo.ts_programe_info.programe_num);
    for (int i = 0; i < mStreamInfo.ts_programe_info.programe_num; i++) {
        reply->writeInt32(mStreamInfo.ts_programe_info.ts_programe_detail[i].video_pid);
        //if (mStreamInfo.ts_programe_info.ts_programe_detail[i].programe_name != NULL)
        reply->writeString16(String16(mStreamInfo.ts_programe_info.ts_programe_detail[i].programe_name));
    }

    reply->setDataPosition(datapos);
    ALOGV("--file name:%s \n", reply->readCString());
    return OK;
}


size_t AmlogicPlayer::countTracks() const
{
    int n = mStreamInfo.stream_info.nb_streams;
    //if (mTextDriver != NULL) {
    //    n += mTextDriver->countExternalTracks();
    //}
    return n;
}

status_t AmlogicPlayer::selectPid(int video_pid) const
{
    int videoPidValid = 0;
    int audio_pid = 0xffff;
    const ts_programe_info_t* programe_info;
    const ts_programe_detail_t* programe_detail;

    programe_info = &(mStreamInfo.ts_programe_info);
    for (int i = 0; i < programe_info->programe_num; i++) {
        programe_detail = &(programe_info->ts_programe_detail[i]);
        if (programe_detail->video_pid == video_pid) {
            videoPidValid = 1;
            audio_pid = programe_detail->audio_pid[0];// get default audio pid
        }
    }

    if (mhasVideo && videoPidValid) {
        ALOGE("player_switch_program mPlayer_id:%d video_pid:%d audio_pid:%d", mPlayer_id);
        player_switch_program(mPlayer_id, video_pid, audio_pid);
    } else {
        return !OK;
    }

    return OK;
}

status_t AmlogicPlayer::selectTrack(int trackIndex, bool select)const //only audio track and timed text track.
{
    //Mutex::Autolock autoLock(mLock);
    int mTrackIndex = mTrackMap[trackIndex];
    if (mTrackIndex == -1) {
        ALOGV("WARNING, track index :%d is not a valid internal track, MAYBE EXT SUB \n", trackIndex);
    }
    ALOGV("selectTrack: trackIndex = %d and select=%d", trackIndex, select);
    if (mhasAudio) {
#if 0
        if (!select) {
            ALOGE("Deselect an audio track (%d) is not supported", trackIndex);
            return ERROR_UNSUPPORTED;
        }
#endif
        for (int m = 0; m < mStreamInfo.stream_info.total_audio_num; m++) {
            if (mTrackIndex == mStreamInfo.audio_info[m]->index) {
                if (mStreamInfo.audio_info[m]->id >= 0) {
                    LOGI("switch audio track,id:%d,pid:%d\n", mTrackIndex, mStreamInfo.audio_info[m]->id);
                    if (select) {
                        player_aid(mPlayer_id, mStreamInfo.audio_info[m]->id, mTrackIndex);
                    } else {
                        LOGE("Deselect an audio track (%d) is not supported", mTrackIndex);
                        return ERROR_UNSUPPORTED;
                    }
                    return OK;
                }
            }
        }

    }
    //inband sub case
    if (mhasSub) {

        //bool mUnTimedText = true;
        //mUnTimedText = PropIsEnable("sys.timedtext.disable",true);

        //ALOGD("%s, mUnTimedText=%d\n",__FUNCTION__,mUnTimedText);

        //if(mUnTimedText){

        for (int m = 0; m < mStreamInfo.stream_info.total_sub_num; m++) {
            if (mTrackIndex == mStreamInfo.sub_info[m]->index) {
                if (mStreamInfo.sub_info[m]->id >= 0) {
                    LOGE("switch audio track,id:%d,pid:%d\n", mTrackIndex, mStreamInfo.sub_info[m]->id);
                    SubSource *mSub = (SubSource *)mSubSource.get();
                    if (select) {
                        mSub->sub_cur_id = m;
                        if (true == mRunning) {
                            player_sid(mPlayer_id, mStreamInfo.sub_info[m]->id);
                        }
                        //mTextDriver->selectTrack(mTrackIndex);
                        //if (true == mRunning) {
                        //mTextDriver->start();
                        //}
                    } else {
                        status_t err;
                        //err = mTextDriver->unselectTrack(mTrackIndex);
                        mSub->sub_cur_id = m;
                        //return err;
                    }
                    return OK;
                }
            }
        }
        /*
        }else{
            for (int m = 0; m < mStreamInfo.stream_info.total_sub_num; m++) {
                if (mTrackIndex == mStreamInfo.sub_info[m]->index) {
                    if (mStreamInfo.sub_info[m]->id >= 0) {
                        LOGE("switch subtitle track,id:%d,pid:%d\n", mTrackIndex, mStreamInfo.sub_info[m]->id);
                        AmSubSource *mSub = (AmSubSource *)mSubSource.get();
                        if (select) {
                            if (true == mRunning) {
                                player_sid(mPlayer_id, mStreamInfo.sub_info[m]->id);
                            }

                            mTextDriver->selectTrack(mTrackIndex);

                            if (true == mRunning) {
                                mSub->initHandle(mPlayer_id);
                                mTextDriver->start();
                            }
                            mSub->mSubCurId = mStreamInfo.sub_info[m]->id;

                        } else {
                            status_t err;
                            err = mTextDriver->unselectTrack(mTrackIndex);
                            //mSub->sub_cur_id=-1;//no need to set
                            return err;
                        }
                        return OK;
                    }
                }
            }
        }
        */
    }
    //outband case
    status_t err = OK;
    /*
    if (mTextDriver != NULL) {
        if (select) {
            char filename[512];
            err = mTextDriver->selectTrack(trackIndex);
            mTextDriver->getFileName(filename);

            if (filename != NULL && strlen(filename)> 0) {
                AmSubSource *sub_src = (AmSubSource *)mSubSource.get();
                sub_src->setFileName(filename);
                LOGE("out band subtitle, filename =%s !\n",filename);
            }
            if (err == OK) {
                if (true == mRunning) {
                    mTextDriver->start();
                }
            }
        } else {
            err = mTextDriver->unselectTrack(trackIndex);
        }
    } else {
        LOGE("selectTrack mTextDriver:NULL\n");
    }
    */

    return err;
}

status_t    AmlogicPlayer::invoke(const Parcel& request, Parcel *reply)
{
    if (NULL == reply) {
        return android::BAD_VALUE;
    }
    int32_t methodId;
    status_t ret = request.readInt32(&methodId);
    if (ret != android::OK) {
        return ret;
    }
    switch (methodId) {
    case INVOKE_ID_SET_VIDEO_SCALING_MODE: {
        int mode = request.readInt32();
        mVideoScalingMode = mode;
        if (mPlayerRender.get() != NULL) {
            return mPlayerRender->setVideoScalingMode(mVideoScalingMode);
        }
        return OK;
    }

    case INVOKE_ID_GET_TRACK_INFO: {
        LOGV("Get track info\n");
        if (player_get_source_type(mPlayer_id) > 0) {
            return getStreamingTrackInfo(reply);
        } else {
            return getTrackInfo(reply);
        }
    }
    case INVOKE_ID_ADD_EXTERNAL_SOURCE: {
        Mutex::Autolock autoLock(mLock);
        //if (mTextDriver == NULL) {
        //    mTextDriver = new TimedTextDriver(mListener,mHTTPService);
        //}
        String8 uri(request.readString16());
        String8 mimeType(request.readString16());
        size_t nTracks = countTracks();
        //return mTextDriver->addOutOfBandTextSource(nTracks, uri, mimeType);
        //LOGV("Get ADD_EXTERNAL_SOURCE not support\n");
        //return ERROR_UNSUPPORTED;
    }
    case INVOKE_ID_ADD_EXTERNAL_SOURCE_FD: {
        Mutex::Autolock autoLock(mLock);
        //if (mTextDriver == NULL) {
        //mTextDriver = new TimedTextDriver(mListener,mHTTPService);
        //}
        int fd         = request.readFileDescriptor();
        off64_t offset = request.readInt64();
        off64_t length  = request.readInt64();
        String8 mimeType(request.readString16());
        size_t nTracks = countTracks();
        LOGV("add outof band trackindex:%d \n", nTracks);
        //return mTextDriver->addOutOfBandTextSource(
        //           nTracks, fd, offset, length, mimeType);
        //LOGV("Get INVOKE_ID_ADD_EXTERNAL_SOURCE_FD not support\n");
        //return ERROR_UNSUPPORTED;
    }
    case INVOKE_ID_SELECT_TRACK: {
        int index = request.readInt32();
        LOGV("select track,index:%d\n", index);
        if (player_get_source_type(mPlayer_id) > 0) {
            return selectStreamingTrack(index, true);
        } else {
            return selectTrack(index, true);
        }
    }
    case INVOKE_ID_UNSELECT_TRACK: {
        int index = request.readInt32();
        LOGV("unselect track,index:%d\n", index);
        if (player_get_source_type(mPlayer_id) > 0) {
            return selectStreamingTrack(index, false);
        } else {
            return selectTrack(index, false);
        }
    }
    case INVOKE_ID_NETWORK_GET_LPBUF_BUFFERED_SIZE: {
        int64_t buffed_size =  player_get_lpbufbuffedsize(mPlayer_id);
        reply->writeInt32((int)buffed_size);
        return OK;
    }
    case INVOKE_ID_NETWORK_GET_STREAMBUF_BUFFERED_SIZE: {
        int64_t buffed_size =  player_get_streambufbuffedsize(mPlayer_id);
        reply->writeInt32((int)buffed_size);
        return OK;
    }
    case INVOKE_ID_SET_TRACK_VOLUME: {
        float left_volume = request.readFloat();
        float right_volume = request.readFloat();
        LOGV("Set left volume:%f, right volume = %f\n", left_volume, right_volume);
        return setVolume(left_volume, right_volume);
    }
    case INVOKE_ID_GET_SELECTED_TRACK: {
        int32_t trackId;
        if (player_get_source_type(mPlayer_id) > 0) {
            trackId = getStreamingSelectedTrack(request);
        } else {
            trackId = getSelectedTrack(request);
        }
        LOGI("get select track id:%d\n", trackId);
        reply->writeInt32((int)trackId);
        return OK;
    }
    default: {
        return ERROR_UNSUPPORTED;
    }
    }
}

status_t AmlogicPlayer::getMetadata(
    const media::Metadata::Filter& ids, Parcel *records)
{
    using media::Metadata;
    LOGV("getMetadata\n");
    Metadata metadata(records);
    //seekble set
    if (mSupportSeek == 0) {
        metadata.appendBool(Metadata::kSeekBackwardAvailable, false);
        metadata.appendBool(Metadata::kSeekForwardAvailable, false);
        metadata.appendBool(Metadata::kSeekAvailable, false);
    } else {
        metadata.appendBool(
            Metadata::kPauseAvailable, true);
        metadata.appendBool(
            Metadata::kSeekBackwardAvailable, true);
        metadata.appendBool(
            Metadata::kSeekForwardAvailable, true);
    }
    updateMediaInfo();
    if (mhasVideo || mhasAudio) {
        if (strlen(mTypeStr) > 0) {
            metadata.appendCString(Metadata::kStreamType, mTypeStr);
        }
    }

    if (mhasVideo) {
        metadata.appendInt32(Metadata::kVideoWidth, mWidth);
        metadata.appendInt32(Metadata::kVideoHeight, mHeight);
        metadata.appendCString(Metadata::kVideoCodec, mStrCurrentVideoCodec != NULL ? mStrCurrentVideoCodec : "unkown");
        metadata.appendCString(Metadata::kVideoCodecAllInfo, mVideoExtInfo != NULL ? mVideoExtInfo : "unkown");
        metadata.appendInt32(Metadata::kVideoTrackNum, mVideoTrackNum);
        LOGV("set meta video info:%s\n", mVideoExtInfo);
    } else {
        metadata.appendInt32(Metadata::kVideoTrackNum, 0);
    }

    if (mhasAudio) {
        metadata.appendInt32(Metadata::kAudioTrackNum, mAudioTrackNum);
        metadata.appendCString(Metadata::kAudioCodec, mStrCurrentAudioCodec != NULL ? mStrCurrentAudioCodec : "unkown");
        metadata.appendCString(Metadata::kAudioCodecAllInfo, mAudioExtInfo != NULL ? mAudioExtInfo : "unkown");
        LOGV("set meta audio info:%s\n", mAudioExtInfo);
    } else {
        metadata.appendInt32(Metadata::kAudioTrackNum, 0);
    }

    if (mInnerSubNum > 0) {
        metadata.appendInt32(Metadata::kInnerSubtitleNum, mInnerSubNum);
        metadata.appendCString(Metadata::kInnerSubtitleAllInfo, mSubExtInfo != NULL ? mSubExtInfo : "unkown");
        LOGV("set meta sub info:%s\n", mSubExtInfo);
    } else {
        metadata.appendInt32(Metadata::kInnerSubtitleNum, 0);
    }

    metadata.appendInt32(Metadata::kPlayerType, AMLOGIC_PLAYER);
    LOGV("get meta data over");

    return OK;
}

int AmlogicPlayer::match_name(const char *name, const char *machsetting)
{
    const char * psets = machsetting;
    const char *psetend;
    int psetlen = 0;
    char codecstr[64] = "";
    if (name == NULL || machsetting == NULL) {
        return 0;
    }

    while (psets && psets[0] != '\0') {
        psetlen = 0;
        psetend = strchr(psets, ',');
        if (psetend != NULL && psetend > psets && psetend - psets < 64) {
            psetlen = psetend - psets;
            memcpy(codecstr, psets, psetlen);
            codecstr[psetlen] = '\0';
            psets = &psetend[1]; //skip ";"
        } else {
            strcpy(codecstr, psets);
            psets = NULL;
        }
        if (strlen(codecstr) > 0) {
            if (strstr(name, codecstr) != NULL) {
                return 1;
            }
        }
    }
    return 0;
}

status_t AmlogicPlayer::initVideoSurface(void)
{
    if (mPlayerRender.get() == NULL) {
        int needosdvideo = 0;
        if (enableOSDVideo) {
            needosdvideo = 1;
        } else if (AmlogicPlayer::PropIsEnable("media.amplayer.v4osd.all")) {
            needosdvideo = 1;
        } else {
            LOGI("calling name=[%s]\n", CallingAPkName);
            if (CallingAPkName[0] != '\0') {
                if (strcasestr(CallingAPkName, ".chrome")//chrome browser. //.android.chrome
                    || strcasestr(CallingAPkName, "browser") //all browsers
                    || strcasestr(CallingAPkName, ".oupeng.mobile") //opera modile,opera?
                    || strcasestr(CallingAPkName, "TunnyBrowser") //TunnyBrowser
                    || strcasestr(CallingAPkName, "phin.browser") //browser
                   ) {
                    needosdvideo = isHTTPSource ? 1 : 0;
                } else if (strcasestr(CallingAPkName, "lamo:benchmarks") || /*for com.quicinc.vellamo:benchmarks need osd for high score*/
                           strcasestr(CallingAPkName, "android.youtube") || //for android.youtube apk
                           strcasestr(CallingAPkName, "android.browser") || //for android.browser,4.4need it now apk
                           strcasestr(CallingAPkName, ".mobile.news.ww") || //for bbc news
                           strcasestr(CallingAPkName, "roid.youtube.tv") || //for youtube tv apk
                           strcasestr(CallingAPkName, "com.android.cts.verifier") //for com.android.cts.verifier
                          ) {
                    needosdvideo = 1;
                } else if (!needosdvideo) {
                    int ret;
                    char value[PROPERTY_VALUE_MAX];
                    /*
                                   setting apk comm for enable osdvideo
                                   cat proc/pidof(apk's name)/comm for stetting string;
                                   seprate it with ","
                                   maybe can ".chrome,.oupeng.mobile" for same as hard settings
                                   */
                    ret = property_get("media.amplayer.v4osd.apkcomm", value, NULL);
                    if (ret > 0 && match_name(CallingAPkName, value)) {
                        needosdvideo = 1;
                        LOGI("enable osdvideo by v4osd settings=[%s]\n", value);
                    }
                }
            }
        }
        LOGI("AmlogicPlayerRender,needosdvideo=%d,isHTTPSource=%d", needosdvideo, isHTTPSource);
        mPlayerRender = new AmlogicPlayerRender(mNativeWindow, needosdvideo | (isDvbTvinSource << 1));
        mPlayerRender->setVideoScalingMode(mVideoScalingMode);
        mPlayerRender->onSizeChanged(curLayout, Rect(mWidth, mHeight));
        if (video_rotation_degree == 1 || video_rotation_degree == 3) {
            sendEvent(MEDIA_SET_VIDEO_SIZE, mHeight, mWidth);    // 90du,or 270du
        } else {
            sendEvent(MEDIA_SET_VIDEO_SIZE, mWidth, mHeight);
        }
        sendEvent(MEDIA_INFO, MEDIA_INFO_RENDERING_START);
    }
    return OK;
}

status_t AmlogicPlayer:: setVideoSurfaceTexture(const sp<IGraphicBufferProducer>& bufferProducer)
{
    LOGV("setVideoSurfaceTexture \n");

    Mutex::Autolock autoLock(mMutex);
    status_t err;
    sp<ANativeWindow> tmpWindow = NULL;
    if (bufferProducer != NULL) {
        const char specApkName[4] = {0x63, 0x74, 0x73, 0x0};
        if (bufferProducer->getConsumerName().contains("SurfaceTexture-") ||
            bufferProducer->getConsumerName().contains("_GLES")
            || ((bufferProducer->getConsumerName().contains("CapturedActivity")
                 || bufferProducer->getConsumerName().contains("PixelCopyVideoSourceActivity"))
                && bufferProducer->getConsumerName().contains(specApkName))) {
            enableOSDVideo = true;
        }
        tmpWindow = new Surface(bufferProducer);
    }

    if (mPlayerRender.get() != NULL) {
        mPlayerRender->Pause();
        mPlayerRender->SwitchNativeWindow(tmpWindow);
        if (!isPlaying() && (tmpWindow == NULL)) {
            stop();// for cts do net stop video
        } else {
            int ret = mPlayerRender->Start();
            if (ret != OK) {
                LOGE("setVideoSurfaceTexture__start__err");
                return ERROR_OPEN_FAILED;
            }
        }
        if (false/*surfaceTexture.get() == NULL*/) {
            mNativeWindow.clear();
        } else {
            mNativeWindow = tmpWindow;
        }
    } else {
        if (true/*surfaceTexture.get() != NULL*/) { /*set new*/
            mNativeWindow = tmpWindow;
            if (mRunning && mhasVideo) { /*player has running*/
                initVideoSurface();
                mPlayerRender->Start();
            }
        }
    }
    return OK;
}

#if 0
status_t AmlogicPlayer::setVideoSurfaceTexture(const sp<ISurfaceTexture>& surfaceTexture)
{
    Mutex::Autolock autoLock(mMutex);
    //mPlayTime=mPlayTime+(int)(ALooper::GetNowUs()-mLastPlayTimeUpdateUS)/1000;/*save the time before*/
    if (mDelayUpdateTime > 0) {
        mDelayUpdateTime++;    /*ignore ++,don't clear the value before*/
    } else {
        mDelayUpdateTime = 1;
    }
    //mLastPlayTimeUpdateUS=ALooper::GetNowUs();
    if (mPlayerRender.get() != NULL) {
        sp<ANativeWindow> tmpWindow = NULL;
        if (surfaceTexture.get() != NULL) {
            tmpWindow = new SurfaceTextureClient(surfaceTexture);
        }
        mPlayerRender->Pause();
        mPlayerRender->SwitchNativeWindow(tmpWindow);
        mPlayerRender->Start();
        if (surfaceTexture.get() == NULL) {
            mNativeWindow.clear();
        } else {
            mNativeWindow = tmpWindow;
        }
    } else {
        if (surfaceTexture.get() != NULL) { /*set new*/
            mNativeWindow = new SurfaceTextureClient(surfaceTexture);
            if (mRunning && mhasVideo) { /*player has running*/
                initVideoSurface();
                mPlayerRender->Start();
            }
        }
    }
    LOGV("Set setVideoSurfaceTexture11\n");
    return OK;
}
#endif

int AmlogicPlayer::getintfromString8(String8 &s, const char*pre)
{
    int off;
    int val = 0;
    if ((off = s.find(pre, 0)) >= 0) {
        sscanf(s.string() + off + strlen(pre), "%d", &val);
    }
    return val;
}

status_t    AmlogicPlayer::setParameter(int key, const Parcel &request)
{
    Mutex::Autolock autoLock(mMutex);
    LOGI("setParameter %d\n", key);
    switch (key) {
    case KEY_PARAMETER_AML_VIDEO_POSITION_INFO: {
        int left, right, top, bottom;
        Rect newRect, oldRect;
        int off;
        const String16 uri16 = request.readString16();
        String8 keyStr = String8(uri16);
        LOGI("setParameter %d=[%s]\n", key, keyStr.string());
        left = getintfromString8(keyStr, ".left=");
        top = getintfromString8(keyStr, ".top=");
        right = getintfromString8(keyStr, ".right=");
        bottom = getintfromString8(keyStr, ".bottom=");
        newRect = Rect(left, top, right, bottom);
        LOGI("setParameter info to newrect=[%d,%d,%d,%d]\n",
             left, top, right, bottom);

        left = getintfromString8(keyStr, ".oldLeft=");
        top = getintfromString8(keyStr, ".oldTop=");
        right = getintfromString8(keyStr, ".oldRight=");
        bottom = getintfromString8(keyStr, ".oldBotton=");
        oldRect = Rect(left, top, right, bottom);
        LOGI("setParameter info oldrect=[%d,%d,%d,%d]\n",
             left, top, right, bottom);
        if (mPlayerRender != NULL && curLayout != newRect) {
            mPlayerRender->onSizeChanged(newRect, oldRect);
        }
        curLayout = newRect;
        break;
    }
    //case KEY_PARAMETER_TIMED_TEXT_TRACK_INDEX:
    //  break;
    case KEY_PARAMETER_AML_PLAYER_SET_DTS_ASSET: {
        int ApreID = 0, ApreAssetSel;
        const String16 uri16 = request.readString16();
        String8 keyStr = String8(uri16);
        LOGI("setParameter %d=[%s]\n", key, keyStr.string());
        ApreID = getintfromString8(keyStr, "dtsApre:");
        ApreAssetSel = getintfromString8(keyStr, "dtsAsset:");
        if (ApreID >= 0 && ApreAssetSel >= 0) {
            dtsm6_set_exchange_info(&ApreID, &ApreAssetSel);
        }
    }
    break;
    case KEY_PARAMETER_AML_PLAYER_SWITCH_VIDEO_TRACK:
        if (mPlayer_id >= 0 && mPlay_ctl.novideo == 0) {
            int vid = -1;
            const String16 uri16 = request.readString16();
            String8 keyStr = String8(uri16);
            vid = getintfromString8(keyStr, "vid:");
            for (int m = 0; m < mStreamInfo.stream_info.total_video_num; m++) {
                if (vid == mStreamInfo.video_info[m]->index) {
                    if (mStreamInfo.video_info[m]->id >= 0) {
                        LOGI("switch video track,id:%d,pid:%d\n", vid, mStreamInfo.video_info[m]->id);
                        selectPid(mStreamInfo.video_info[m]->id);
                        return OK;
                    }
                }
            }
        }
        break;
    case KEY_PARAMETER_AML_PLAYER_SWITCH_AUDIO_TRACK:
        //audio TRACK?
        if (mPlayer_id >= 0 && mPlay_ctl.nosound == 0) {
            int aid = -1;
            const String16 uri16 = request.readString16();
            String8 keyStr = String8(uri16);
            LOGI("setParameter %d=[%s]\n", key, keyStr.string());
            aid = getintfromString8(keyStr, "aid:");
            for (int m = 0; m < mStreamInfo.stream_info.total_audio_num; m++) {
                if (aid == mStreamInfo.audio_info[m]->index) {
                    if (mStreamInfo.audio_info[m]->id >= 0) {
                        LOGI("switch audio track,id:%d,pid:%d\n", aid, mStreamInfo.audio_info[m]->id);
                        player_aid(mPlayer_id, mStreamInfo.audio_info[m]->id, aid);
                        return OK;
                    }
                }
            }

        }
        break;
    //case KEY_PARAMETER_TIMED_TEXT_ADD_OUT_OF_BAND_SOURCE:
    //_ADD_OUT_OF_BAND_SOURCE?
    //  break;
    case KEY_PARAMETER_CACHE_STAT_COLLECT_FREQ_MS:
        //FREQ_MS?
        break;
    case KEY_PARAMETER_AML_PLAYER_SWITCH_SOUND_TRACK:
        //sound track
        if (mPlayer_id >= 0) {
            const String16 uri16 = request.readString16();
            String8 keyStr = String8(uri16);
            LOGI("setParameter %d=[%s]\n", key, keyStr.string());
            if (!keyStr.compare(String8("lmono"))) {
                audio_left_mono(mPlayer_id);
                audio_lr_mix_set(mPlayer_id, 0);
                AudioDualMonoNeed = 0;
            } else if (!keyStr.compare(String8("rmono"))) {
                audio_right_mono(mPlayer_id);
                audio_lr_mix_set(mPlayer_id, 0);
                AudioDualMonoNeed = 0;
            } else if (!keyStr.compare(String8("stereo"))) {
                audio_stereo(mPlayer_id);
                audio_lr_mix_set(mPlayer_id, 0);
                AudioDualMonoNeed = 0;
            } else if (!keyStr.compare(String8("lrmix"))) {
                AudioDualMonoSetOK = !audio_lr_mix_set(mPlayer_id, 1);
                AudioDualMonoNeed = 1;
            }
        }
        break;
    case KEY_PARAMETER_AML_PLAYER_TRICKPLAY_FORWARD:
        if (mPlayer_id >= 0) {
            int speed = 0;
            const String16 uri16 = request.readString16();
            String8 keyStr = String8(uri16);
            speed = getintfromString8(keyStr, "forward:");
            if (speed >= 0) {
                player_forward(mPlayer_id, speed);
            }
        }
        break;
    case KEY_PARAMETER_AML_PLAYER_TRICKPLAY_BACKWARD:
        if (mPlayer_id >= 0) {
            int speed = 0;
            const String16 uri16 = request.readString16();
            String8 keyStr = String8(uri16);
            LOGI("setParameter %d=[%s]\n", key, keyStr.string());

            speed = getintfromString8(keyStr, "backward:");
            if (speed >= 0) {
                player_backward(mPlayer_id, speed);
            }
            mFFStatus = true;

        }
        break;
    case KEY_PARAMETER_AML_PLAYER_RESET_BUFFER:
        if (mPlayer_id >= 0) {
            LOGI("Do player buffer reset now.\n", 0);
            mNeedResetOnResume = true;
        }
        break;
    case KEY_PARAMETER_AML_PLAYER_FREERUN_MODE:
        if (mPlayer_id >= 0) {
            int delay = 0;
            const String16 uri16 = request.readString16();
            String8 keyStr = String8(uri16);
            delay = getintfromString8(keyStr, "freerun_mode:");
            player_cmd_t cmd;
            memset(&cmd, 0, sizeof(cmd));
            LOGI("set freerun mode %d\n", delay);
            cmd.set_mode = CMD_SET_FREERUN_MODE;
            cmd.param = delay;
            player_send_message(mPlayer_id, &cmd);
        }
        break;
    case KEY_PARAMETER_AML_PLAYER_ENABLE_OSDVIDEO: {
        const String16 uri16 = request.readString16();
        String8 keyStr = String8(uri16);
        enableOSDVideo = getintfromString8(keyStr, "osdvideo:") > 0;
    }
    break;
    case KEY_PARAMETER_AML_PLAYER_DIS_AUTO_BUFFER:
        mPlay_ctl.auto_buffing_enable = 0;
        break;
    case KEY_PARAMETER_AML_PLAYER_ENA_AUTO_BUFFER:
        mPlay_ctl.auto_buffing_enable = 1;
        break;
    case KEY_PARAMETER_AML_PLAYER_USE_SOFT_DEMUX: {
        int isUseSoftDemux = request.readInt32();
        mPlay_ctl.is_ts_soft_demux = isUseSoftDemux;
    }
    break;
    case KEY_PARAMETER_AML_PLAYER_FORCE_SCREEN_MODE: {
        int mode = request.readInt32();
        if (mode < 0 || mode > 3) {
            LOGI("invalid mode:%d(0-3)\n", mode);
            break;
        }
        int fd;
        int bytes;
        char  bcmd[16];
        fd = open(SCREEN_MODE_SET_PATH, O_CREAT | O_RDWR | O_TRUNC, 0644);
        if (fd >= 0) {
            sprintf(bcmd, "%d", mode);
            bytes = write(fd, bcmd, strlen(bcmd));
            close(fd);
            return 0;
        } else {
            LOGE("unable to open file %s \n", SCREEN_MODE_SET_PATH);
        }
    }
    case KEY_PARAMETER_AML_PLAYER_SET_DISPLAY_MODE: {
        int mode = request.readInt32();
        if (set_cur_dispmode(mode) == -1) {
            return 1;
        } else {
            return 0;    // OK, it will change boolean value as true in android_media_MediaPlayer.cpp
        }
    }
    default:
        LOGI("unsupport setParameter value!=%d\n", key);
    }
    return OK;
}

status_t    AmlogicPlayer::getParameter(int key, Parcel *reply)
{
    Mutex::Autolock autoLock(mMutex);
    TRACE();
    if (key == KEY_PARAMETER_AML_PLAYER_VIDEO_OUT_TYPE) {
        reply->writeInt32(VIDEO_OUT_HARDWARE);
        return 0;
    } else if (key == KEY_PARAMETER_AML_PLAYER_HWBUFFER_STATE) {
        const int bufsize = 128;
        char hwbuf[bufsize];
        memset(hwbuf, 0, bufsize);

        snprintf(hwbuf, bufsize, "{\"abuf_level\":%f,\"vbuf_level\":%f,\"buf_min\":%f,\"buf_mid\":%f,\"buf_max\":%f,}",

                 mHWaudiobuflevel, mHWvideobuflevel, mPlay_ctl.buffing_min, mPlay_ctl.buffing_middle, mPlay_ctl.buffing_max);

        LOGI("Get amplayer streaming buffer info: %s\n", hwbuf);
        reply->writeCString(hwbuf);
        return 0;
    } else if (key == KEY_PARAMETER_AML_PLAYER_GET_MEDIA_INFO) {
        getMediaInfo(reply);
        return OK;
    } else if (key == KEY_PARAMETER_AML_PLAYER_GET_DTS_ASSET_TOTAL) {
        if (mStrCurrentAudioCodec != NULL && !strncmp(mStrCurrentAudioCodec, "DTS", 3)) {
            int32_t ApresAssetsArray[32] = {0};
            dtsm6_get_exchange_info(NULL, &DtshdApreTotal, NULL, NULL, ApresAssetsArray, NULL, NULL);
            reply->writeInt32(DtshdApreTotal);
            reply->writeInt32Array(32, ApresAssetsArray);
        } else {
            int32_t ApresAssetsArray[32] = {0};
            reply->writeInt32(0);
            reply->writeInt32Array(32, ApresAssetsArray);
        }
        return OK;
    }

    return OK;
}

status_t AmlogicPlayer::getCurrentPosition(int* position)
{
    //LOGV("getCurrentPosition\n");
    LOGI(" getCurrentPosition Player time=%dms\n", (int)(ALooper::GetNowUs() - PlayerStartTimeUS) / 1000);
    Mutex::Autolock autoLock(mMutex);

    if (fastNotifyMode) {
        if (mStreamTime <= 0) { /*mStreamTime is have not set,just set a big value for netflix may pause bug.*/
            mStreamTime += mStreamTimeExtAddS * 1000;
        }
        if (LatestPlayerState == PLAYER_PAUSE && mOffsetBacFlag) {
            mOffsetBac = (ALooper::GetNowUs() - mLastStreamTimeUpdateUS) / 1000;
            mOffsetBacFlag = false;
        }
        *position = (int)(mStreamTime + (ALooper::GetNowUs() - mLastStreamTimeUpdateUS) / 1000); /*jast let uplevel know,we are playing,they don't care it.(netflix's bug/)*/
        /* for CTS, round the timestamp  */
        if (mPlaybackSettings.mSpeed != 1.0f) {
            *position = (*position + 500) / 1000 * 1000;
        }
        ///*position+=mStreamTimeExtAddS*1000;

    } else {
        if (!mPaused && LatestPlayerState == PLAYER_RUNNING) {
            int64_t realposition;
#if 0
            if (mPlayTimeBac != mPlayTime) { //do not update position for mPlayTime no change, add by wxl
                mPlayTimeBac = mPlayTime;
                if (mPlayTime > mLastPlaytime || mSeekdone) {
                    realposition = mPlayTime + (int64_t)(ALooper::GetNowUs() - mLastPlayTimeUpdateUS) / 1000;
                    mLastPlaytime = mPlayTime;
                    mLastPosition = realposition;
                    mSeekdone = false;
                } else {
                    realposition = mLastPosition + (int64_t)(ALooper::GetNowUs() - mLastPlayTimeUpdateUS) / 1000;
                    mLastPlaytime = mPlayTime;
                }
                LOGI(" getCurrentPosition mPlayTime=%d,mLastPlayTimeUpdateUS=%" PRId64 "*1000,GetNowUs()=%" PRId64 "*1000,realposition=%" PRId64 "\n",
                     mPlayTime, mLastPlayTimeUpdateUS / 1000, ALooper::GetNowUs() / 1000, realposition);
                //*position=((realposition+500)/1000)*1000;/*del small  changes,<500ms*/
                *position = realposition;
                realpositionBac = realposition;
            } else {
                if (mPlayTimeBac == 0 && mPlayTime == 0) {
                    realpositionBac = 0;
                }
                *position = realpositionBac;
            }
#endif
            realposition = mPlayTime + (int64_t)(ALooper::GetNowUs() - mLastPlayTimeUpdateUS) / 1000;
            LOGI(" getCurrentPosition mPlayTime=%d,mLastPlayTimeUpdateUS=%" PRId64 "*1000,GetNowUs()=%" PRId64 "*1000,realposition=%" PRId64 "\n",
                 mPlayTime, mLastPlayTimeUpdateUS / 1000, ALooper::GetNowUs() / 1000, realposition);
            *position = realposition;

            if (realposition >= 3000 && !mAuto3DDetected) {//will cost 3s at least to detect 3D format by di
                if (mSeekPos > 0 && (int64_t)(ALooper::GetNowUs() - mSeekPos) / 1000 >= 3000 || mSeekPos == 0) {
                    mSeekPos = 0;
                    mAuto3DDetected = true;
                    amSCautoDetect3DForMbox();
                }
            }
        } else {
            //*position=((mPlayTime+500)/1000)*1000;
            *position = mPlayTime;
        }
    }
    if (mDuration > 0 && LatestPlayerState == PLAYER_RUNNING && *position >= mDuration && mDelayUpdateTime != 2) {
        LOGV("Maybe CurrentPosition exceed mDuration,just do minor adjustment(minus 100ms)\n");
        if (mDuration % 1000 > 100) {
            *position = mDuration - 100;
        } else {
            *position = mDuration - mDuration % 1000;
        }
        mPlayTime = *position;
    }

    if (fastNotifyMode) {
        if (LatestPlayerState == PLAYER_STOPED || LatestPlayerState == PLAYER_INITOK) {//wxl add for 7.0cts 20161125
            *position = 0;
        } else if (LatestPlayerState == PLAYER_PAUSE) {
            *position = (int)(mStreamTime + mOffsetBac);
        }
    }

    if (mStrCurrentAudioCodec != NULL && !strncmp(mStrCurrentAudioCodec, "DTS", 3)) {
        int stream_type = 0;
        int TotalApre = 0;
        int MulAssetHint = 0;
        int HPS_hint = 0;
        dtsm6_get_exchange_info(&stream_type, &TotalApre, NULL, NULL, NULL, &MulAssetHint, &HPS_hint);
        if (TotalApre != DtshdApreTotal && TotalApre > 0) {
            LOGI("[%s %d]TotalApre changed:%d-->%d\n", __FUNCTION__, __LINE__, DtshdApreTotal, TotalApre);
            DtshdApreTotal = TotalApre;
            sendEvent(MEDIA_INFO, MEDIA_INFO_AMLOGIC_SHOW_DTS_ASSET);
        }
        if (stream_type != DtsHdStreamType) {
            LOGI("[%s %d]DtsHdStreamType changed:%d-->%d\n", __FUNCTION__, __LINE__, DtsHdStreamType, stream_type);
            DtsHdStreamType = stream_type;
            if (DtsHdStreamType == 0x1) {
                sendEvent(MEDIA_INFO, MEDIA_INFO_AMLOGIC_SHOW_DTS_EXPRESS);
            } else if (DtsHdStreamType == 0x2) {
                sendEvent(MEDIA_INFO, MEDIA_INFO_AMLOGIC_SHOW_DTS_HD_MASTER_AUDIO);
            }

        }
        if (DtsHdMulAssetHint != MulAssetHint && MulAssetHint) {//TOTO:xiangliang.wang
            LOGI("[%s %d]MulAssetHint event send\n", __FUNCTION__, __LINE__);
            sendEvent(MEDIA_INFO, MEDIA_INFO_AMLOGIC_SHOW_DTS_MULASSETHINT);
            DtsHdMulAssetHint = MulAssetHint;
        }

        if (HPS_hint && DtsHdHpsHint == 0) {
            sendEvent(MEDIA_INFO, MEDIA_INFO_AMLOGIC_SHOW_DTS_HPS_NOTSUPPORT);
            DtsHdHpsHint = 1;
        }
    }
    LOGV("CurrentPosition=%dmS,mStreamTime=%d\n", *position, mStreamTime);
    return NO_ERROR;
}

status_t AmlogicPlayer::getDuration(int* duration)
{
    Mutex::Autolock autoLock(mMutex);
    LOGV("getDuration\n");
    if (mDuration <= 0) {
        *duration = -1;
    } else {
        *duration = mDuration;
    }
    return NO_ERROR;
}

status_t AmlogicPlayer::release()
{
    int exittime;
    LOGV("release\n");
    if (mPlayer_id >= 0) {
        if (mDecryptHandle != NULL) {
            mDrmManagerClient->setPlaybackStatus(mDecryptHandle,
                                                 Playback::STOP, 0);
            mDecryptHandle = NULL;
            mDrmManagerClient = NULL;
        }

        player_stop(mPlayer_id);
        player_exit(mPlayer_id);
        if (mhasVideo) {
            VideoViewClose();
        }

    }
    TRACE();
    mPlayer_id = -1;
    if (mPlayerRender.get() != NULL) {
        mPlayerRender->Stop();
        mPlayerRender.clear();
    }
    if (mNativeWindow.get()) {
        mNativeWindow.clear();
    }
    if (mDuration > 1000 && mPlayTime > 1000 && mPlayTime < mDuration - 5000) { //if 5 seconds left,I think end plaing
        exittime = mPlayTime / 1000;
    } else {
        exittime = 0;
    }
    if (mAmlogicFile.datasource && mDuration > 0) {
        HistoryMgt(mAmlogicFile.datasource, 1, exittime);
    }

    if (mAmlogicFile.datasource != NULL) {
        free(mAmlogicFile.datasource);
    }
    mAmlogicFile.datasource = NULL;
    if (mAmlogicFile.fd_valid) {
        close(mAmlogicFile.fd);
    }
    mAmlogicFile.fd_valid = 0;
    if (mPlay_ctl.headers) {
        free(mPlay_ctl.headers);
        mPlay_ctl.headers = NULL;
    }
    if (mStreamSource.get() != NULL) {
        mStreamSource.clear();
    }
    if (mSouceProtocol.get() != NULL) {
        mSouceProtocol.clear();
    }
    if (mhasAudio && mChangedCpuFreq) {
        SetCpuScalingOnAudio(1);
        mChangedCpuFreq = false;
    }
    ///sendEvent(MEDIA_PLAYBACK_COMPLETE);
    return NO_ERROR;
}

status_t AmlogicPlayer::reset()
{
    //Mutex::Autolock autoLock(mMutex);
    mIgnoreMsg = true;
    mShouldSendPlayComplete = false;
    mOffsetBacFlag = false;
    mOffsetBac = 0;
    LOGV("reset\n");
    if (mhasVideo || !mPaused) { //wxl del for music play
        player_exit(mPlayer_id);
    }
    if (mPlayerRender.get() != NULL) {
        mPlayerRender->Stop();
    }
    mPlayTime = 0;
    //pause();
    //mPaused = true;
    mRunning = false;
    mIgnoreMsg = false;
    if (mAuto3DDetected) {
        mAuto3DDetected = false;
        mSeekPos = 0;
        amSCsetDisplay3DFormat(0); // close 3D
    }
    /*
    if (mTextDriver != NULL) {
        delete mTextDriver;
        mTextDriver = NULL;
        if (mSubSource != NULL) {
            SubSource *sub_src = (SubSource *)mSubSource.get();
            sub_src->stop();
        }
        mSubSource = NULL;
    }*/
    return NO_ERROR;
}

// always call with lock held
status_t AmlogicPlayer::reset_nosync()
{
    Mutex::Autolock autoLock(mMutex);
    LOGV("reset_nosync\n");
    // close file
    //player_stop_async(mPlayer_id);
    return NO_ERROR;
}

status_t AmlogicPlayer::setLooping(int loop)
{
    Mutex::Autolock autoLock(mMutex);
    LOGV("setLooping\n");
    bool isLoop = (loop != 0);
    if (isLoop == mLoop) {
        LOGV("drop same message,is loop:%s", isLoop ? "YES" : "NO");
        return NO_ERROR;
    } else {
        mLoop = isLoop;
    }
    if (mLoop) {
        player_loop(mPlayer_id);
    } else {
        player_noloop(mPlayer_id);
    }
    return NO_ERROR;
}

status_t  AmlogicPlayer::setVolume(float leftVolume, float rightVolume)
{
    Mutex::Autolock autoLock(mMutex);
    LOGV("setVolume\n");
    mLeftVolume = leftVolume;
    mRightVolume = rightVolume;
    int ret = audio_set_lrvolume(mPlayer_id, leftVolume, rightVolume);
    if (ret < 0) {
        LOGV("Amadec is not ready now, after start, set volume once more!\n");
        mSetVolumeFlag = 1;
    }
    return NO_ERROR;
}

status_t AmlogicPlayer::dump_streaminfo(int fd, media_info_t mInfo)const
{
    String8 result;
    const size_t SIZE = 256;
    char buffer[SIZE];
    if (mInfo.stream_info.filename != NULL) {
        snprintf(buffer, SIZE, "  %s\n", mInfo.stream_info.filename);
        result.append(buffer);
    }
    if (mStreamInfo.stream_info.duration > 0) {
        snprintf(buffer, SIZE, "  duaraiont:%d s", mInfo.stream_info.duration);
        result.append(buffer);
    }

    if (mStreamInfo.stream_info.file_size > 0) {
        snprintf(buffer, SIZE, " file_size:%" PRId64 " bytes", mInfo.stream_info.file_size);
        result.append(buffer);
    }
    if (mStreamInfo.stream_info.bitrate > 0) {
        snprintf(buffer, SIZE, " total_bitrate:%d b/s\n", mInfo.stream_info.bitrate);
        result.append(buffer);
    }
    write(fd, result.string(), result.size());
    return NO_ERROR;
}

status_t AmlogicPlayer::dump_videoinfo(int fd, media_info_t mStreamInfo)const
{
    String8 result;
    const size_t SIZE = 256;
    char buffer[SIZE];
    for (int i = 0; i < mStreamInfo.stream_info.total_video_num; i ++) {
        snprintf(buffer, SIZE, "  Video[%d/%d]", i, mStreamInfo.stream_info.total_video_num);
        result.append(buffer);
        snprintf(buffer, SIZE, " Index[%d]", mStreamInfo.video_info[i]->index);
        result.append(buffer);
        snprintf(buffer, SIZE, " Id[%d]", mStreamInfo.video_info[i]->id);
        result.append(buffer);
        snprintf(buffer, SIZE, " Format[%s]", player_value2str("vformat", mStreamInfo.video_info[i]->format));
        result.append(buffer);
        snprintf(buffer, SIZE, " Size[w%d h%d]", mStreamInfo.video_info[i]->width, mStreamInfo.video_info[i]->height);
        result.append(buffer);
        snprintf(buffer, SIZE, " AspectRatio[%d:%d]", mStreamInfo.video_info[i]->aspect_ratio_num, mStreamInfo.video_info[i]->aspect_ratio_den);
        result.append(buffer);
        snprintf(buffer, SIZE, " FrameRate[%.2f]", mStreamInfo.video_info[i]->frame_rate_num / mStreamInfo.video_info[i]->frame_rate_den);
        result.append(buffer);
        result.append("\n");
        write(fd, result.string(), result.size());
        result.clear();
    }
    return NO_ERROR;
}

status_t AmlogicPlayer::dump_audioinfo(int fd, media_info_t mStreamInfo)const
{
    String8 result;
    const size_t SIZE = 256;
    char buffer[SIZE];
    for (int i = 0; i < mStreamInfo.stream_info.total_audio_num; i ++) {
        snprintf(buffer, SIZE, "  Audio[%d/%d]", i, mStreamInfo.stream_info.total_audio_num);
        result.append(buffer);
        snprintf(buffer, SIZE, " Index[%d]", mStreamInfo.audio_info[i]->index);
        result.append(buffer);
        snprintf(buffer, SIZE, " Id[%d]", mStreamInfo.audio_info[i]->id);
        result.append(buffer);
        snprintf(buffer, SIZE, " Format[%s]", player_value2str("aformat", mStreamInfo.audio_info[i]->aformat));
        result.append(buffer);
        snprintf(buffer, SIZE, " Channel[%d]", mStreamInfo.audio_info[i]->channel);
        result.append(buffer);
        snprintf(buffer, SIZE, " SampleRate[%d]", mStreamInfo.audio_info[i]->sample_rate);
        result.append(buffer);
        result.append("\n");
        write(fd, result.string(), result.size());
        result.clear();
    }
    return NO_ERROR;
}

status_t AmlogicPlayer::dump_subtitleinfo(int fd, media_info_t mStreamInfo)const
{
    String8 result;
    const size_t SIZE = 256;
    char buffer[SIZE];
    for (int i = 0; i < mStreamInfo.stream_info.total_sub_num; i ++) {
        snprintf(buffer, SIZE, " Sub[%d/%d]", i, mStreamInfo.stream_info.total_sub_num);
        result.append(buffer);
        snprintf(buffer, SIZE, " Index[%d]", mStreamInfo.sub_info[i]->index);
        result.append(buffer);
        snprintf(buffer, SIZE, " Id[%d]", mStreamInfo.sub_info[i]->id);
        result.append(buffer);
        snprintf(buffer, SIZE, " InternalOrExternal[%d]", mStreamInfo.sub_info[i]->internal_external);
        result.append(buffer);
        snprintf(buffer, SIZE, " Size[w%d h%d]", mStreamInfo.sub_info[i]->width, mStreamInfo.sub_info[i]->height);
        result.append(buffer);
        snprintf(buffer, SIZE, " SubType[%d]", mStreamInfo.sub_info[i]->sub_type);
        result.append(buffer);
        snprintf(buffer, SIZE, " SubtitleSize[%" PRId64 "]", mStreamInfo.sub_info[i]->subtitle_size);
        result.append(buffer);
        //if (mStreamInfo.sub_info[i]->sub_language != NULL) {
        snprintf(buffer, SIZE, " SubLanguage[%s]", mStreamInfo.sub_info[i]->sub_language);
        //}
        result.append(buffer);
        result.append("\n");
        write(fd, result.string(), result.size());
        result.clear();
    }
    return NO_ERROR;
}


status_t AmlogicPlayer::dump(int fd, const Vector<String16> &args) const
{
    size_t i;
    bool dumpMediaInfo = true;
    bool dumpPlayerInfo = true;
    bool dumpBufferInfo = true;
    bool dumpTsyncInfo = true;

    for (i = 0; i < args.size(); i++) {
        if (args[i] == String16("-m")) {
            dumpMediaInfo = true;
        }
        if (args[i] == String16("-p")) {
            dumpPlayerInfo = true;
        }
        if (args[i] == String16("-b")) {
            dumpBufferInfo = true;
        }
        if (args[i] == String16("-t")) {
            dumpTsyncInfo = true;
        }
    };

    FILE *out = fdopen(dup(fd), "w");
    fprintf(out, " \n");
    fprintf(out, " AmlogicPlayer\n");

#if 1
    //dump media info
    if (dumpMediaInfo && streaminfo_valied) {
        dump_streaminfo(fd, mStreamInfo);
        if (mStreamInfo.stream_info.has_video) {
            fprintf(out, " Video Stream Info\n");
            dump_videoinfo(fd, mStreamInfo);
            fprintf(out, "  current video stream index %d\n", mStreamInfo.stream_info.cur_video_index);
        }

        if (mStreamInfo.stream_info.has_audio) {
            fprintf(out, " Audio Stream Info\n");
            dump_audioinfo(fd, mStreamInfo);
            fprintf(out, "  current audio stream index %d\n", mStreamInfo.stream_info.cur_audio_index);
        }

        if (mStreamInfo.stream_info.has_sub) {
            fprintf(out, " Subtitle Info\n");
            dump_subtitleinfo(fd, mStreamInfo);
            fprintf(out, "  current audio stream index %d\n", mStreamInfo.stream_info.cur_sub_index);
        }
    }

    if (dumpPlayerInfo) {
        fprintf(out, " player playback status\n");
        player_dump_playinfo(mPlayer_id, fd);
    }

    if (dumpBufferInfo) {
        fprintf(out, " player buffer state\n");
        player_dump_bufferinfo(mPlayer_id, fd);
    }

    if (dumpTsyncInfo) {
        fprintf(out, " player sync info\n");
        player_dump_tsyncinfo(mPlayer_id, fd);
    }
#endif
    fprintf(out, "\n");
    fclose(out);
    out = NULL;

    return OK;
}


/* 3D Part
 *
 * change disp mode,switch between 2d and 3d
 * param:
 * 0 2d
 * 1 3d lr
 * 2 3d tb
 *
 * ret:
 * -1 failed
 *  0 ok
 * */

int AmlogicPlayer::get_cur_dispmode()
{
    //NOTE: NEED TO FIX
    return 0;
}

/*
 * 1 setprop
 * 2 set hdmi config
 * 3 set axis
 * 4 set request 2x scale
 * */

int AmlogicPlayer::set_cur_dispmode(int mode)
{
    const char *mHDMIConfigFile = "/sys/class/amhdmitx/amhdmitx0/config";
    //const char *mHDMIDisp3DFile = "/sys/class/amhdmitx/amhdmitx0/disp_cap_3d";
    const char *mHDMI3DSupport = "/sys/class/amhdmitx/amhdmitx0/support_3d";
    const char * mRequest2XScaleFile = "/sys/class/graphics/fb0/request2XScale";
    const char *prop_3d = "mbx.video.mode.3d";
    const char *video_axis = "/sys/class/video/axis";
    const char *out_mode = "ubootenv.var.outputmode";

    int output_mode = 0; // 720p default 0, 1 for 1080p, -1 other not support 3d
    int enable_3d = 0;

    char value[PROPERTY_VALUE_MAX];

    /*
    char dis3DSupportMode[32] = {0};
    int ret = amsysfs_get_sysfs_str(mHDMIDisp3DFile, dis3DSupportMode, 32);
    if(ret < 0) {
        return -1;
    }
    else {
        if((NULL == strstr(dis3DSupportMode, "FramePacking")) &&
            (NULL == strstr(dis3DSupportMode, "TopBottom")) &&
            (NULL == strstr(dis3DSupportMode, "SidebySide")))
            return -1;
    }*/
    char is3DSupport[32] = {0};
    int ret = amsysfs_get_sysfs_str(mHDMI3DSupport, is3DSupport, 32);
    if (ret < 0) {
        return -1;
    } else {
        if (NULL == strstr(is3DSupport, "1")) {
            return -1;
        }
    }

    if (property_get(out_mode, value, NULL) > 0)
        if (strstr(value, "720p") != NULL) {
            output_mode = 0;
        } else if (strstr(value, "1080p") != NULL) {
            output_mode = 1;
        } else {
            output_mode = -1;    // no 3d support
        }

    LOGV("set display mode, output_mode:%d \n", output_mode);
    if (mode > 0) {
        if (output_mode == 0) {
            amsysfs_set_sysfs_str(video_axis, "0 0 1279 719");
        }
        if (output_mode == 1) {
            amsysfs_set_sysfs_str(video_axis, "0 0 1919 1079");
        }
        if (output_mode == -1) {
            return -1;
        }
    }

    switch (mode) {
    case 0:
        property_set(prop_3d, "0");
        amsysfs_set_sysfs_str(mHDMIConfigFile, "3doff");
        //SurfaceComposerClient::setDisplay2Stereoscopic(0,0);
        break;
    case 1:
        property_set(prop_3d, "1");
        amsysfs_set_sysfs_str(mHDMIConfigFile, "3dlr");
        //SurfaceComposerClient::setDisplay2Stereoscopic(0,8);
        LOGV("set display mode, set 3dlr \n");
        break;
    case 2:
        property_set(prop_3d, "2");
        amsysfs_set_sysfs_str(mHDMIConfigFile, "3dtb");
        //SurfaceComposerClient::setDisplay2Stereoscopic(0,16);
        LOGV("set display mode, set 3dtb \n");
        break;
    default:
        break;
    }
    SurfaceComposerClient::openGlobalTransaction();
    SurfaceComposerClient::closeGlobalTransaction();

    //cur_mode = mode;
    return 0;
}

} // end namespace android
