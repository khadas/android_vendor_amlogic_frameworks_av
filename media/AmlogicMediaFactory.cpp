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
#define LOG_TAG "AmlogicMediaFactory"
#include <utils/Log.h>
#include <cutils/properties.h>
#include <media/IMediaPlayer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <utils/Errors.h>
#include <utils/misc.h>

#include "AmSuperPlayer.h"
#include "AmlogicPlayer.h"

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

    virtual sp<MediaPlayerBase> createPlayer() {
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
                               int64_t /*length*/,
                               float /*curScore*/) {
        return 0.9;
    }

    virtual float scoreFactory(const sp<IMediaPlayer>& /*client*/,
                               const sp<IStreamSource>& /*source*/,
                               float /*curScore*/) {
        return 1.0;
    }

    virtual sp<MediaPlayerBase> createPlayer() {
        ALOGV("Create Amsuperplayer stub");
        return new AmSuperPlayer();
    }
};



int AmlogicMediaFactoryInit(void)
{
    status_t err;
    AmlogicPlayer::BasicInit();
    err = MediaPlayerFactory::registerFactory(new AmlogicPlayerFactory(), AMLOGIC_PLAYER);
    ALOGV("register  AmlogicPlayerFactory err =%d\n", err);
    err = MediaPlayerFactory::registerFactory(new AmSuperPlayerFactory(), AMSUPER_PLAYER);
    ALOGV("register  AmSuperPlayerFactory err =%d\n", err);
    return 0;
}

}
