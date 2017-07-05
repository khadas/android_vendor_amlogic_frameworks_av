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



#define LOG_TAG "amSystemControl"

#include <ISystemControlService.h>
#include <binder/Binder.h>
#include <binder/IServiceManager.h>
#include <utils/Atomic.h>
#include <utils/Log.h>
#include <utils/RefBase.h>
#include <utils/String8.h>
#include <utils/String16.h>
#include <utils/threads.h>
#include <AmSysControl.h>
#include <unistd.h>
#include <fcntl.h>

using namespace android;

class DeathNotifier: public IBinder::DeathRecipient {
    public:
        DeathNotifier() {}
        void binderDied(const wp < IBinder > &who) {
            ALOGW("system_write died!");
        }
};

static sp < ISystemControlService > amSystemControlService;
static sp < DeathNotifier > amDeathNotifier;
static Mutex amLock;
static Mutex amgLock;

const sp < ISystemControlService > &getSystemControlService() {
    Mutex::Autolock _l(amgLock);
    if (amSystemControlService.get() == 0) {
        sp < IServiceManager > sm = defaultServiceManager();
        sp < IBinder > binder;
        do {
            binder = sm->getService(String16("system_control"));
            if (binder != 0)
                break;
            ALOGW("SystemControl not published, waiting...");
            usleep(500000); // 0.5 s
        } while (true);

        if (amDeathNotifier == NULL) {
            amDeathNotifier = new DeathNotifier();
        }
        binder->linkToDeath(amDeathNotifier);
        amSystemControlService = interface_cast < ISystemControlService > (binder);
    }
    ALOGE_IF(amSystemControlService == 0, "no System Control Service!?");
    return amSystemControlService;
}

int amSCsetDisplay3DFormat(int format) {
    int ret = -1;
    const sp < ISystemControlService > &scs = getSystemControlService();
    if (scs != 0) {
        String16 value;
        if (scs->getProperty(String16("sys.auto3ddetect.enable"), value) && strcmp(String8(value).string(), "true") == 0) {
            if (scs->setDisplay3DFormat(format)) {
                ret = 0;
            }
            else {
                ALOGE("amSCsetDisplay3DFormat getSystemControlService() is null");
            }
        }
    }
    return ret;
}

int amSCgetDisplay3DFormat(void) {
    const sp < ISystemControlService > &scs = getSystemControlService();
    if (scs != 0) {
        return scs->getDisplay3DFormat();
    }
    else {
        ALOGE("amSCgetDisplay3DFormat getSystemControlService() is null");
    }
    return -1;
}

void amSCautoDetect3DForMbox() {
    const sp < ISystemControlService > &scs = getSystemControlService();
    if (scs != 0) {
        String16 value;
        if (scs->getProperty(String16("sys.auto3ddetect.enable"), value) && strcmp(String8(value).string(), "true") == 0) {
            ALOGE("[amSCautoDetect3DForMbox]autoDetect3DForMbox");
            scs->autoDetect3DForMbox();
        }
    }
    else {
        ALOGE("amSCautoDetect3DForMbox getSystemControlService() is null");
    }
}
