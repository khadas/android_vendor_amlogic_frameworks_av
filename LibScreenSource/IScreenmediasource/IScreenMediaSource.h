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

#ifndef ANDROID_ISCREENMEDIASOURCE_H_

#define ANDROID_ISCREENMEDIASOURCE_H_

#include <media/stagefright/MetaData.h>
#include <media/stagefright/MediaBuffer.h>

#include <binder/IInterface.h>
#include "IScreenMediaSourceClient.h"
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/MediaDefs.h>

#include <media/stagefright/MediaSource.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/MediaBufferGroup.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/MediaBuffer.h>

#include <sys/types.h>
#include <utils/Vector.h>

#include <utils/Errors.h>  // for status_t
#include <utils/KeyedVector.h>
#include <utils/String8.h>

#include <utils/List.h>
#include <utils/RefBase.h>
#include <utils/threads.h>

namespace android {

struct AMessage;
struct IMemory;
struct ReadOptions;

//struct IScreenSource : public IInterface, public MediaSource {
struct IScreenMediaSource : public IInterface {
    DECLARE_META_INTERFACE(ScreenMediaSource);

    virtual status_t registerClient(const sp<IScreenMediaSourceClient> &client,
                                          int32_t width,
                                          int32_t height,
                                          int32_t framerate,
                                          SCREENMEDIASOURCEDATATYPE data_type,
                                          int32_t* client_id,
                                          const sp<IGraphicBufferProducer> &gbp) = 0;
    virtual status_t unregisterClient(int32_t client_id) = 0;
    virtual status_t setVideoRotation(int32_t client_id, const int degree) = 0;
    virtual status_t start(int32_t client_id) = 0;
    virtual status_t stop(int32_t client_id) = 0;
    virtual status_t readBuffer(int32_t client_id, sp<IMemory> buffer, int64_t* pts) = 0;
    virtual status_t freeBuffer(int32_t client_id, sp<IMemory> buffer) = 0;
    virtual sp<MetaData> getFormat(int32_t client_id) = 0;
    virtual status_t setVideoCrop(int32_t client_id, const int32_t x, const int32_t y, const int32_t width, const int32_t height) = 0;
};

////////////////////////////////////////////////////////////////////////////////

struct BnScreenMediaSource : public BnInterface<IScreenMediaSource> {
    virtual status_t onTransact(
            uint32_t code, const Parcel &data, Parcel *reply,
            uint32_t flags = 0);
};

}  // namespace android

#endif  // ANDROID_ISCREENSOURCE_H_
