/*
** Copyright 20014, The Android Open Source Project
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
#define LOG_TAG "AmlogicMetadataRetrieverFactory"
#include <utils/Log.h>
#include <cutils/properties.h>
#include <media/IMediaPlayer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <utils/Errors.h>
#include <utils/misc.h>

#include "AmlPlayerMetadataRetriever.h"

#include "../libmediaplayerservice/MetadataRetrieverFactory.h"
namespace android
{


class AmlogicMetadataRetrieverFactory : public MediaMetadataRetrieverFactory::RFactory
{
public:
    virtual sp<MediaMetadataRetrieverBase> createRetriever() {
        ALOGV("Create Amlogic MetadataRetriever");
        return new AmlPlayerMetadataRetriever();
    }
};

int AmlogicMetadataRetrieverFactoryInit(void)
{
    status_t err;
    err = MediaMetadataRetrieverFactory::registerFactory(new AmlogicMetadataRetrieverFactory(), AMLOGIC_PLAYER);
    ALOGV("register  AmlogicMetadataRetrieverFactory err =%d\n", err);
    err = MediaMetadataRetrieverFactory::registerFactory(new AmlogicMetadataRetrieverFactory(), AMSUPER_PLAYER);
    ALOGV("register  AmSuperMetadataRetrieverFactory err =%d\n", err);
    return 0;
}

}

