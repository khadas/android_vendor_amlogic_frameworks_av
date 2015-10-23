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

//#define LOG_NDEBUG 0
#define LOG_TAG "IScreenMediaSource"
#include <utils/Log.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/MediaBuffer.h>
#include <media/stagefright/MediaSource.h>
#include <sys/types.h>

#include <media/stagefright/MediaCodec.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MetaData.h>
#include <media/hardware/MetadataBufferType.h>

#include "IScreenMediaSource.h"
#include <media/stagefright/foundation/AMessage.h>

#include <binder/IMemory.h>
#include <binder/Parcel.h>

namespace android {

struct MediaSource;

enum {
	REGISTERCLIENT = IBinder::FIRST_CALL_TRANSACTION,
	UNREGISTERCLIENT,
    START,
    STOP,
    SETRESOLUTIONRATIO,
    SETFRAMERATE,
    SETVIDEOROTATION,
    READBUFFER,
    FREEBUFFER,
    GETFORMATE,
    SETVIDEOCROP,
};

struct BpScreenMediaSource : public BpInterface<IScreenMediaSource> {
    BpScreenMediaSource(const sp<IBinder> &impl)
        : BpInterface<IScreenMediaSource>(impl) {
    }

    virtual status_t registerClient(const sp<IScreenMediaSourceClient> &client,
                                int32_t width,
                                int32_t height,
                                int32_t framerate,
                                SCREENMEDIASOURCEDATATYPE data_type,
                                int32_t* client_id,
                                const sp<IGraphicBufferProducer> &gbp) {


        ALOGV("[%s %d] client:0x%x, width:%d height:%d framerate:%d data_type:%d", __FUNCTION__, __LINE__, client.get(), width, height, framerate, data_type);

        Parcel data, reply;
        data.writeInterfaceToken(IScreenMediaSource::getInterfaceDescriptor());
        //data.writeStrongBinder(client->asBinder());
        data.writeStrongBinder(IInterface::asBinder(client));
        data.writeInt32(width);
        data.writeInt32(height);
        data.writeInt32(framerate);
        data.writeInt32((int32_t)data_type);
        if (data_type == SCREENMEDIASOURC_HANDLE_TYPE && gbp != NULL) {
            ALOGV("[%s %d] gbp: 0x%x", __FUNCTION__, __LINE__, gbp.get());
            //data.writeStrongBinder(gbp->asBinder());
            data.writeStrongBinder(IInterface::asBinder(gbp));
        }

        remote()->transact(REGISTERCLIENT, data, &reply);

        status_t err = reply.readInt32();
        if (err == OK) {
            *client_id = reply.readInt32();
        } else {
            *client_id = -1;
        }

        ALOGV("[%s %d] client_id:%d", __FUNCTION__, __LINE__, *client_id);
        return err;
    }

    virtual status_t unregisterClient(int32_t client_id) {
        Parcel data, reply;
        data.writeInterfaceToken(IScreenMediaSource::getInterfaceDescriptor());
        ALOGV("[%s %d] client_id:%d", __FUNCTION__, __LINE__, client_id);
        data.writeInt32(client_id);

        status_t status = remote()->transact(UNREGISTERCLIENT, data, &reply);
        return status;
    }

    virtual status_t start(int32_t client_id)
    {
        ALOGV("[%s %d] client_id:%d", __FUNCTION__, __LINE__, client_id);
        Parcel data, reply;
        data.writeInterfaceToken(IScreenMediaSource::getInterfaceDescriptor());
        data.writeInt32(client_id);
        remote()->transact(START, data, &reply);
        return reply.readInt32();
    }

    virtual status_t stop(int32_t client_id)
    {
        ALOGV("[%s %d]", __FUNCTION__, __LINE__);
        Parcel data, reply;
        data.writeInterfaceToken(IScreenMediaSource::getInterfaceDescriptor());
        data.writeInt32(client_id);
        remote()->transact(STOP, data, &reply);
        return reply.readInt32();
    }

    virtual status_t setVideoRotation(int32_t client_id, const int32_t degree) {
        ALOGV("[%s %d]", __FUNCTION__, __LINE__);
        Parcel data, reply;
        data.writeInterfaceToken(IScreenMediaSource::getInterfaceDescriptor());
        data.writeInt32(client_id);
        data.writeInt32(degree);
        remote()->transact(SETVIDEOROTATION, data, &reply);
        return reply.readInt32();
    }

    virtual status_t setVideoCrop(int32_t client_id, const int32_t x, const int32_t y, const int32_t width, const int32_t height) {
        ALOGV("[%s %d] x:%d y:%d width:%d height:%d", __FUNCTION__, __LINE__, x, y, width, height);
        Parcel data, reply;
        data.writeInterfaceToken(IScreenMediaSource::getInterfaceDescriptor());
        data.writeInt32(client_id);
        data.writeInt32(x);
        data.writeInt32(y);
        data.writeInt32(width);
        data.writeInt32(height);
        remote()->transact(SETVIDEOCROP, data, &reply);
        return reply.readInt32();
    }

    virtual status_t readBuffer(int32_t client_id, sp<IMemory> buffer, int64_t* pts) {

        Parcel data, reply;
        data.writeInterfaceToken(IScreenMediaSource::getInterfaceDescriptor());
        data.writeInt32(client_id);
        //data.writeStrongBinder(buffer->asBinder());
        data.writeStrongBinder(IInterface::asBinder(buffer));

        remote()->transact(READBUFFER, data, &reply);

        status_t err = reply.readInt32();
        if (err == OK) {
            *pts = reply.readInt64();
        }else
            return err;

        return OK;
    }

    virtual status_t freeBuffer(int32_t client_id, sp<IMemory> buffer) {
        Parcel data, reply;
        data.writeInterfaceToken(IScreenMediaSource::getInterfaceDescriptor());
        data.writeInt32(client_id);
        //data.writeStrongBinder(buffer->asBinder());
        data.writeStrongBinder(IInterface::asBinder(buffer));

        remote()->transact(FREEBUFFER, data, &reply);
        return OK;
    }

    virtual sp<MetaData> getFormat(int32_t client_id) {
        Parcel data, reply;
        data.writeInterfaceToken(IScreenMediaSource::getInterfaceDescriptor());
        remote()->transact(GETFORMATE, data, &reply);

        sp<MetaData> meta;
        return meta;
    }

};

IMPLEMENT_META_INTERFACE(ScreenMediaSource, "android.media.IScreenMediaSource");

status_t BnScreenMediaSource::onTransact(
        uint32_t code, const Parcel &data, Parcel *reply, uint32_t flags) {

    switch (code) {
        case REGISTERCLIENT: {
            CHECK_INTERFACE(IScreenMediaSource, data, reply);

            int32_t client_id;
            sp<IScreenMediaSourceClient> client = IScreenMediaSourceClient::asInterface(data.readStrongBinder());
            int32_t width = data.readInt32();
            int32_t height = data.readInt32();
            int32_t framerate = data.readInt32();
            SCREENMEDIASOURCEDATATYPE data_type = static_cast<SCREENMEDIASOURCEDATATYPE>(data.readInt32());
            sp<IGraphicBufferProducer> gbp = NULL;
            if (data_type == SCREENMEDIASOURC_HANDLE_TYPE) {
                gbp = interface_cast<IGraphicBufferProducer>(data.readStrongBinder());
            }

            ALOGV("[%s %d] client:0x%x, gbp:0x%x, width:%d height:%d framerate:%d data_type:%d", __FUNCTION__, __LINE__, client.get(), gbp.get(), width, height, framerate, data_type);

            status_t err = registerClient(client, width, height, framerate, data_type, &client_id, gbp);

            ALOGV("[%s %d] client_id:%d", __FUNCTION__, __LINE__, client_id);

            reply->writeInt32(err);
            if (err == OK) {
                reply->writeInt32(client_id);
            }

            return err;
        } break;
        case UNREGISTERCLIENT: {
            CHECK_INTERFACE(IScreenMediaSource, data, reply);
            int32_t client_id = data.readInt32();
            ALOGV("[%s %d] client_id:%d", __FUNCTION__, __LINE__, client_id);
            status_t status = unregisterClient(client_id);
            return status;
        } break;
        case START: {
            CHECK_INTERFACE(IScreenMediaSource, data, reply);
            int32_t client_id = data.readInt32();
            ALOGV("[%s %d] client_id:%d", __FUNCTION__, __LINE__, client_id);
            reply->writeInt32(start(client_id));
            return NO_ERROR;
        } break;
        case STOP: {
            CHECK_INTERFACE(IScreenMediaSource, data, reply);
            int32_t client_id = data.readInt32();
            ALOGV("[%s %d] client_id:%d", __FUNCTION__, __LINE__, client_id);
            reply->writeInt32(stop(client_id));
            return NO_ERROR;
        } break;
        case SETVIDEOROTATION: {
            CHECK_INTERFACE(IScreenMediaSource, data, reply);
            int32_t client_id = data.readInt32();
            int32_t degree = data.readInt32();
            reply->writeInt32(setVideoRotation(client_id, degree));
            return NO_ERROR;
        } break;
        case READBUFFER: {
            CHECK_INTERFACE(IScreenMediaSource, data, reply);
            int32_t client_id = data.readInt32();
            sp<IMemory> buffer = interface_cast<IMemory>(data.readStrongBinder());
            int64_t pts;
            status_t err = readBuffer(client_id, buffer, &pts);
            reply->writeInt32(err);
            if (err == OK) {
                reply->writeInt64(pts);
            } else {
                return err;
            }
            return NO_ERROR;
        } break;
        case FREEBUFFER: {
            CHECK_INTERFACE(IScreenMediaSource, data, reply);

            int32_t client_id = data.readInt32();

            sp<IMemory> buffer;
            buffer = interface_cast<IMemory>(data.readStrongBinder());
            reply->writeInt32(freeBuffer(client_id, buffer));
        } break;
        case GETFORMATE: {
            CHECK_INTERFACE(IScreenMediaSource, data, reply);
            int32_t client_id = data.readInt32();

            sp<MetaData> params = getFormat(client_id);

            return NO_ERROR;

        } break;
		    case SETVIDEOCROP: {
            CHECK_INTERFACE(IScreenMediaSource, data, reply);
            int32_t client_id = data.readInt32();
            int32_t x = data.readInt32();
            int32_t y = data.readInt32();
            int32_t width = data.readInt32();
            int32_t height = data.readInt32();
            ALOGV("[%s %d] x:%d y:d width:%d height:%d", __FUNCTION__, __LINE__, x, y, width, height);
            reply->writeInt32(setVideoCrop(client_id, x, y, width, height));
            return NO_ERROR;
		    }
        default:
        return BBinder::onTransact(code, data, reply, flags);
    }

    return OK;
}

}  // namespace android
