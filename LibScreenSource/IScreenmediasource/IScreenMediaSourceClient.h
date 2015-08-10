/*
 * Copyright (C) 2008 The Android Open Source Project
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

#ifndef ANDROID_ISCREENMEDIASOURCECLIENT_H
#define ANDROID_ISCREENMEDIASOURCECLIENT_H

#include <utils/RefBase.h>
#include <binder/IInterface.h>
#include <binder/Parcel.h>
#include <binder/IMemory.h>
#include <android/native_window.h>
#include <gui/Surface.h>

namespace android {

enum SCREENMEDIASOURCEDATATYPE{
    SCREENMEDIASOURC_CANVAS_TYPE,
    SCREENMEDIASOURC_HANDLE_TYPE,
    SCREENMEDIASOURC_RAWDATA_TYPE,
};

class IScreenMediaSourceClient: public IInterface
{
public:
    DECLARE_META_INTERFACE(ScreenMediaSourceClient);
    virtual void notify(int msg, int ext1, int ext2, const Parcel *obj) = 0;
    virtual int dataCallback(const sp<IMemory>& data) = 0;
};

// ----------------------------------------------------------------------------

class BnScreenMediaSourceClient: public BnInterface<IScreenMediaSourceClient>
{
public:
    virtual status_t    onTransact( uint32_t code,
                                    const Parcel& data,
                                    Parcel* reply,
                                    uint32_t flags = 0);
};

}; // namespace android

#endif // ANDROID_IMEDIAPLAYERCLIENT_H
