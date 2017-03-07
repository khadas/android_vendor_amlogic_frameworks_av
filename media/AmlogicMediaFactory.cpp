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

#define LOG_TAG "AmlogicMediaFactory"
#include <utils/Log.h>
#include <cutils/properties.h>
#include <media/IMediaPlayer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <utils/Errors.h>
#include <utils/misc.h>
#include <libsonivox/eas.h>

#include "AmSuperPlayer.h"
#include "AmlogicPlayer.h"
#include "AmNuPlayerDriver.h"

#include "../libmediaplayerservice/MediaPlayerFactory.h"
namespace android
{


class AmlogicPlayerFactory : public MediaPlayerFactory::IFactory
{
public:
    virtual float scoreFactory(const sp<IMediaPlayer>& /*client*/,
                               const char* url,
                               float /*curScore*/) {
        if (AmlogicPlayer::PropIsEnable("media.amplayer.enable", true)) {
            return 0.8;
        }

        return 0.0;
    }

    virtual float scoreFactory(const sp<IMediaPlayer>& /*client*/,
                              const sp<IStreamSource>& /*source*/,
                              float /*curScore*/) {
       if (AmlogicPlayer::PropIsEnable("media.amplayer.enable", true)) {
           return 1.0;
       }

       return 0.0;
    }

    virtual sp<MediaPlayerBase> createPlayer(pid_t /* pid */) {
        ALOGV("Create AmlogicPlayer stub");
        return new AmlogicPlayer();
    }
};


class AmSuperPlayerFactory : public MediaPlayerFactory::IFactory
{
public:
    virtual float scoreFactory(const sp<IMediaPlayer>& /*client*/,
                               const char* url,
                               float /*curScore*/) {
        if (AmlogicPlayer::PropIsEnable("media.amsuperplayer.enable", true)) {
            return 0.9;
        }

        return 0.0;
    }
    virtual float scoreFactory(const sp<IMediaPlayer>& /*client*/,
                               int fd,
                               int64_t offset,
                               int64_t length,
                               float /*curScore*/) {
        char buf[32];
        lseek(fd, offset, SEEK_SET);
        read(fd, buf, sizeof(buf));
        lseek(fd, offset, SEEK_SET);
        long ident = *((long*)buf);
        if (!AmlogicPlayer::PropIsEnable("media.amsuperplayer.enable", true)) {
            return 0.0;
        }
    /* Ogg vorbis?
         * ogm header syntax:
         * number_page_segments:1 byte -----> buf[28]
         * egment_table : buf[29] .....
         * We  just use partial  of header info to check if it is a music file
         */
        if (ident == 0x5367674f) {
            if ((buf[28] == 1)&&  (!(((buf[29] == 'v') && (buf[30] == 'i') && (buf[31] == 'd' ))  || buf[29] == 't'))) {
                return 0.0;  //ogg
            }
        }
        /*
        // Some kind of MIDI?
        EAS_DATA_HANDLE easdata;
        if (EAS_Init(&easdata) == EAS_SUCCESS) {
            EAS_FILE locator;
            locator.path = NULL;
            locator.fd = fd;
            locator.offset = offset;
            locator.length = length;
            EAS_HANDLE  eashandle;
            if (EAS_OpenFile(easdata, &locator, &eashandle) == EAS_SUCCESS) {
                EAS_CloseFile(easdata, eashandle);
                EAS_Shutdown(easdata);
                return 0.0;
            }
            EAS_Shutdown(easdata);
        }
        */

        return 0.9;
    }

    virtual float scoreFactory(const sp<IMediaPlayer>& /*client*/,
                               const sp<IStreamSource>& /*source*/,
                               float /*curScore*/) {
        if (!AmlogicPlayer::PropIsEnable("media.amsuperplayer.enable", true)) {
            return 0.0;
        }
        return 1.0;
    }

    virtual sp<MediaPlayerBase> createPlayer(pid_t pid) {
        ALOGV("Create Amsuperplayer stub");
        return new AmSuperPlayer(pid);
    }
};

class AmNuPlayerFactory : public MediaPlayerFactory::IFactory {
  public:
    virtual float scoreFactory(const sp<IMediaPlayer>& /*client*/,
                               const char* url,
                               float curScore)
   {
        static const float kOurScore = 1.0;
        bool udpenable = false;
        char value[PROPERTY_VALUE_MAX];
        if (property_get("media.hls.disable-nuplayer", value, NULL)
            && (!strcasecmp(value, "true") || !strcmp(value, "1"))) {
            return 0.0;
        }
        if (property_get("media.udp.use-nuplayer", value, "1")//default use amnuplayer
            && (!strcasecmp(value, "true") || !strcmp(value, "1"))) {
            udpenable = true;
        }

        if (kOurScore <= curScore)
            return 0.0;

        // use amnuplayer to play hls.
        // add other stream type afterwards.
        if (!strncasecmp("http://", url, 7)
            || !strncasecmp("https://", url, 8) || (!strncasecmp("udp:", url, 4) && udpenable)) {
            size_t len = strlen(url);

            // skip over DASH & MS-SS.
            if ((len >= 4 && !strcasecmp(".mpd", &url[len - 4]))
                || (len >= 4 && !strcasecmp(".wvm", &url[len - 4]))
                || (strstr(url, ".ism/") || strstr(url, ".isml/"))) {
                return 0.0;
            }

            return kOurScore;
        }

        return 0.0;
    }

    virtual float scoreFactory(const sp<IMediaPlayer>& /*client*/,
                               const sp<IStreamSource>& /*source*/,
                               float /*curScore*/)
    {
        if(AmlogicPlayer::PropIsEnable("media.hls.disable-nuplayer", false))
        {
            return 0.0;
        }
        return 0.8;
    }
    virtual float scoreFactory(const sp<IMediaPlayer>& /*client*/,
                               int fd,
                               int64_t offset,
                               int64_t length,
                               float /*curScore*/)
    {
        if(AmlogicPlayer::PropIsEnable("media.hls.disable-nuplayer", false))
        {
            return 0.0;
        }
        return 0.5;
    }
    virtual sp<MediaPlayerBase> createPlayer(pid_t  pid ) {
        ALOGV(" create AmNuPlayer");
        return new AmNuPlayerDriver(pid);
    }
};

int AmlogicMediaFactoryInit(void)
{

    status_t err;
#ifdef BUILD_WITH_AMLOGIC_PLAYER
    AmlogicPlayer::BasicInit();
    err = MediaPlayerFactory::registerFactory(new AmlogicPlayerFactory(), AMLOGIC_PLAYER);
    ALOGV("register  AmlogicPlayerFactory err =%d\n", err);
    err = MediaPlayerFactory::registerFactory(new AmSuperPlayerFactory(), AMSUPER_PLAYER);
    ALOGV("register  AmSuperPlayerFactory err =%d\n", err);
#endif
#ifdef BUILD_WITH_AMNUPLAYER
    err = MediaPlayerFactory::registerFactory(new AmNuPlayerFactory(), AMNUPLAYER);
    ALOGV("register  AmNuPlayerFactory err =%d\n", err);
#endif

    DataSource::RegisterDefaultSniffers();
    ALOGV("register default sniffers\n");

    return 0;
}

}
