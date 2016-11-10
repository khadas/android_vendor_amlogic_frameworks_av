/*
 * Copyright (C) 2011 The Android Open Source Project
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
//#define LOG_NDEBUG 1
#define LOG_TAG "ScreenMediaSource"

#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MetaData.h>
#include <OMX_IVCommon.h>
#include <MetadataBufferType.h>

#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <binder/MemoryHeapBase.h>
#include <binder/MemoryBase.h>

#include <ui/GraphicBuffer.h>
#include <gui/ISurfaceComposer.h>
#include <gui/IGraphicBufferAlloc.h>
#include <OMX_Component.h>

#include <utils/Log.h>
#include <utils/String8.h>

#include <private/gui/ComposerService.h>

#include "ScreenMediaSource.h"

#include <LibScreenSource/IScreenmediasource/IScreenMediaSource.h>
#include <LibScreenSource/IScreenmediasource/IScreenMediaSourceClient.h>

#include <stdio.h>
#include <assert.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <linux/videodev2.h>
#include <hardware/hardware.h>
#include <hardware/aml_screen.h>


namespace android {

#define BOUNDRY 32
#define ALIGN(x) (x + (BOUNDRY) - 1)& ~((BOUNDRY) - 1)

#define MAX_CLIENT 4
static const int64_t VDIN_MEDIA_SOURCE_TIMEOUT_NS = 3000000000LL;

static void VdinDataCallBack(void *user, aml_screen_buffer_info_t *buffer){
    ScreenMediaSource *source = static_cast<ScreenMediaSource *>(user);
    source->dataCallBack(buffer);
    return;
}

ScreenMediaSource::ScreenMediaSource() :
    mCurrentTimestamp(0),
    mFrameRate(30),
    mStarted(false),
    mError(false),
    mNumFramesReceived(0),
    mNumFramesEncoded(0),
    mFirstFrameTimestamp(0),
    mMaxAcquiredBufferCount(4),  // XXX double-check the default
    mUseAbsoluteTimestamps(false),
    mDropFrame(2),
    mBufferGet(NULL),
    mCanvasClientExist(0),
    bufferTimeUs(0),
    mFrameCount(0),
    mTimeBetweenFrameCaptureUs(0),
    mScreenModule(NULL),
    mScreenDev(NULL){

    mCorpX = mCorpY = mCorpWidth = mCorpHeight =0;

    mScreenMediaSourceDealer = new MemoryDealer(10240, "ScreenSource");

    ALOGV("[%s %d] Construct", __FUNCTION__, __LINE__);
}

ScreenMediaSource::~ScreenMediaSource() {
    ALOGV("~ScreenMediaSource");
    CHECK(!mStarted);

    reset();

    if (mScreenDev)
        mScreenDev->common.close((struct hw_device_t *)mScreenDev);

    mScreenMediaSourceDealer.clear();
}

status_t ScreenMediaSource::registerClient(const sp<IScreenMediaSourceClient> &client,
	                                        int32_t width,
                                          int32_t height,
                                          int32_t framerate,
                                          SCREENMEDIASOURCEDATATYPE data_type,
                                          int32_t* client_id,
                                          const sp<IGraphicBufferProducer> &gbp) {
    Mutex::Autolock autoLock(mLock);
    int clientTotalNum;
    int clientNum = -1;
    clientTotalNum = mClientList.size();

    ALOGV("[%s %d]  native window:0x%x clientTotalNum:%d width:%d height:%d framerate:%d data_type:%d", __FUNCTION__, __LINE__,
             gbp.get(), clientTotalNum, width, height, framerate, data_type);

    if (clientTotalNum >= MAX_CLIENT) {
        ALOGV("[%s %d] clientTotalNum:%d ", __FUNCTION__, __LINE__, clientTotalNum);
        return !OK;
    }

    ScreenMediaSourceClient* Client_tmp = (ScreenMediaSourceClient*)malloc(sizeof(ScreenMediaSourceClient));

    Client_tmp->width = width;
    Client_tmp->height = height;
    Client_tmp->framerate = framerate;
    Client_tmp->isPrimateClient = 0;
    Client_tmp->data_type = data_type;

    if (clientTotalNum == 0) {
        clientNum = 1;
    } else {
        ScreenMediaSourceClient* client_temp;
        for (int i = 0; i < clientTotalNum; i++) {
            client_temp = mClientList.valueAt(i);
            if (client_temp->mClient_id != i + 1) {
                clientNum = i + 1;
        }
    }

    if (clientNum == -1)
        clientNum = clientTotalNum + 1;
    }

    ALOGV("[%s %d] clientNum:%d clientTotalNum:%d", __FUNCTION__, __LINE__,  clientNum, clientTotalNum);

    Client_tmp->mClient_id = clientNum;
    *client_id = clientNum;

    if (SCREENMEDIASOURC_CANVAS_TYPE == data_type) {
        int client_num = 0;
        client_num = mClientList.size();
        ScreenMediaSourceClient* client_local;

        for (int i = 0; i < client_num; i++) {
            client_local = mClientList.valueAt(i);
            if (client_local->data_type == SCREENMEDIASOURC_CANVAS_TYPE) {
                ALOGE("[%s %d] screen source owned canvas client already, so reject another canvas client", __FUNCTION__, __LINE__);
                return !OK;
            }
        }

        mWidth = width;
        mHeight = height;
        mCanvasclient = client;
        mBufferSize = mWidth * mHeight * 3/2;

    } else if (SCREENMEDIASOURC_RAWDATA_TYPE == data_type) {
        ALOGV("[%s %d] clientTotalNum:%d width:%d height:%d framerate:%d data_type:%d", __FUNCTION__, __LINE__,
            clientTotalNum, width, height, framerate, data_type);
        if (clientTotalNum == 0) {
            mWidth = width;
            mHeight = height;
        }
    }else if(SCREENMEDIASOURC_HANDLE_TYPE == data_type) {
        if (gbp != NULL) {
            mANativeWindow = new Surface(gbp);
            if (mANativeWindow != NULL) {
                // Set gralloc usage bits for window.
                int err = native_window_set_usage(mANativeWindow.get(), SCREENSOURCE_GRALLOC_USAGE);
                if (err != 0) {
                    ALOGE("native_window_set_usage failed: %s\n", strerror(-err));
                    if (ENODEV == err) {
                        ALOGE("Preview surface abandoned!");
                        mANativeWindow = NULL;
                    }
                }

                ///Set the number of buffers needed for camera preview
                err = native_window_set_buffer_count(mANativeWindow.get(), 4);
                if (err != 0) {
                    ALOGE("native_window_set_buffer_count failed: %s (%d)", strerror(-err), -err);
                    if (ENODEV == err) {
                        ALOGE("Preview surface abandoned!");
                        mANativeWindow = NULL;
                    }
                }

                // Set window geometry
                err = native_window_set_buffers_geometry(
                    mANativeWindow.get(),
                    ALIGN(1280),
                    720,
                    HAL_PIXEL_FORMAT_YCrCb_420_SP);

                if (err != 0) {
                    ALOGE("native_window_set_buffers_geometry failed: %s", strerror(-err));
                    if ( ENODEV == err ) {
                        ALOGE("Surface abandoned!");
                        mANativeWindow = NULL;
                    }
                }
                err = native_window_set_scaling_mode(mANativeWindow.get(), NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW);
                if (err != 0) {
                    ALOGW("Failed to set scaling mode: %d", err);
                }
            }
        }

        if (clientTotalNum == 0) {
            mWidth = 1280;
            mHeight = 720;
            mBufferSize = mWidth * mHeight * 3/2;
        }
    }

    ALOGV("[%s %d] clientNum:%d", __FUNCTION__, __LINE__, clientNum);
    mClientList.add(clientNum, Client_tmp);

    return OK;
}

status_t ScreenMediaSource::unregisterClient(int32_t client_id) {
    ALOGV("[%s %d] client_id:%d", __FUNCTION__, __LINE__, client_id);
    Mutex::Autolock autoLock(mLock);

    ScreenMediaSourceClient* client;
    client = mClientList.valueFor(client_id);
    mClientList.removeItem(client_id);

    free(client);
    return OK;
}

nsecs_t ScreenMediaSource::getTimestamp() {
    ALOGV("[%s %d]", __FUNCTION__, __LINE__);
    Mutex::Autolock autoLock(mLock);
    return mCurrentTimestamp;
}

bool ScreenMediaSource::isMetaDataStoredInVideoBuffers() const {
    ALOGV("[%s %d]", __FUNCTION__, __LINE__);
    return true;
}

int32_t ScreenMediaSource::getFrameRate( )
{
    ALOGV("[%s %d]", __FUNCTION__, __LINE__);
    Mutex::Autolock autoLock(mLock);
    return mFrameRate;
}

status_t ScreenMediaSource::setVideoRotation(int32_t client_id, int degree)
{
    int angle;

    ALOGV("[%s %d] setVideoRotation degree:%x", __FUNCTION__, __LINE__, degree);

    if (degree == 0)
        angle = 0;
    else if (degree == 1)
        angle = 270;
    else if (degree == 2)
        angle = 180;
    else if (degree == 3)
        angle = 90;
    else {
        ALOGV("degree is not right");
        return !OK;
    }

    if (mScreenDev != NULL) {
        ALOGV("[%s %d] setVideoRotation angle:%x", __FUNCTION__, __LINE__, angle);
        mScreenDev->ops.set_rotation(mScreenDev, angle);
    }

    return OK;
}

status_t ScreenMediaSource::setVideoCrop(int32_t client_id, const int32_t x, const int32_t y, const int32_t width, const int32_t height)
{
    ALOGV("[%s %d] setVideoCrop x:%d y:%d width:%d height:%d", __FUNCTION__, __LINE__, x, y, width, height);

    mCorpX = x;
    mCorpY = y;
    mCorpWidth = width;
    mCorpHeight = height;

    return OK;
}

status_t ScreenMediaSource::start(int32_t client_id)
{
    Mutex::Autolock autoLock(mLock);

    int client_num = mClientList.size();

    if (!mScreenModule || client_num == 1) {
        if (hw_get_module(AML_SCREEN_HARDWARE_MODULE_ID, (const hw_module_t **)&mScreenModule) < 0) {
            ALOGE("[%s %d] can`t get AML_SCREEN_HARDWARE_MODULE_ID module", __FUNCTION__, __LINE__);
            return !OK;
        }

        if (mScreenModule->common.methods->open((const hw_module_t *)mScreenModule, "1",
                (struct hw_device_t**)&mScreenDev) < 0) {
            mScreenModule = NULL;
            ALOGE("[%s %d] open AML_SCREEN_SOURCE fail", __FUNCTION__, __LINE__);
            return !OK;
        }

        ALOGV("[%s %d] start AML_SCREEN_SOURCE", __FUNCTION__, __LINE__);

        mScreenDev->ops.set_port_type(mScreenDev, 0x1100C000);
        mScreenDev->ops.set_frame_rate(mScreenDev, 30);
        mScreenDev->ops.set_format(mScreenDev, mWidth, mHeight, V4L2_PIX_FMT_NV21);
        mScreenDev->ops.setDataCallBack(mScreenDev, VdinDataCallBack, (void*)this);
        mScreenDev->ops.set_amlvideo2_crop(mScreenDev, mCorpX, mCorpY, mCorpWidth, mCorpHeight);
        mScreenDev->ops.start(mScreenDev);
    }

    SCREENMEDIASOURCEDATATYPE source_data_type;
    ScreenMediaSourceClient* client;
    ALOGV("[%s %d] client_id:%d client_num:%d", __FUNCTION__, __LINE__, client_id, client_num);
    client = mClientList.valueFor(client_id);
    source_data_type = client->data_type;

    if (SCREENMEDIASOURC_HANDLE_TYPE == source_data_type && mANativeWindow != NULL) {
        mANativeWindow->incStrong((void*)ANativeWindow_acquire);
    }

    if (SCREENMEDIASOURC_CANVAS_TYPE == source_data_type) {
        mCanvasClientExist = 1;
    }

    mStartTimeOffsetUs = 0;
    mNumFramesReceived = mNumFramesEncoded = 0;
    mStartTimeUs = systemTime(SYSTEM_TIME_MONOTONIC)/1000;

    mStarted = true;

    return OK;
}

status_t ScreenMediaSource::setMaxAcquiredBufferCount(size_t count) {
    ALOGV("setMaxAcquiredBufferCount(%d)", count);
    Mutex::Autolock autoLock(mLock);

    CHECK_GT(count, 1);
    mMaxAcquiredBufferCount = count;

    return OK;
}

status_t ScreenMediaSource::setUseAbsoluteTimestamps() {
    ALOGV("[%s %d]", __FUNCTION__, __LINE__);
    Mutex::Autolock autoLock(mLock);
    mUseAbsoluteTimestamps = true;

    return OK;
}

status_t ScreenMediaSource::stop(int32_t client_id)
{
    Mutex::Autolock autoLock(mLock);

    int client_num = mClientList.size();
    ALOGV("[%s %d] client_num:%d client_id:%d", __FUNCTION__, __LINE__, client_num, client_id);

    if (1 == client_num)
        reset();

    SCREENMEDIASOURCEDATATYPE source_data_type;
    ScreenMediaSourceClient* client;
    client = mClientList.valueFor(client_id);
    source_data_type = client->data_type;

    if (SCREENMEDIASOURC_CANVAS_TYPE == source_data_type)
        mCanvasClientExist = 0;

    if (SCREENMEDIASOURC_HANDLE_TYPE == source_data_type && mANativeWindow != NULL) {
        mANativeWindow->decStrong((void*)ANativeWindow_acquire);
    }

    if (SCREENMEDIASOURC_RAWDATA_TYPE == source_data_type) {
        while (!mRawBufferQueue.empty()) {
            ALOGE("[%s %d] free buffer", __FUNCTION__, __LINE__);
            MediaBuffer* rawBuffer = *mRawBufferQueue.begin();
            mRawBufferQueue.erase(mRawBufferQueue.begin());
            rawBuffer->release();
        }
    }
    return OK;
}

sp<MetaData> ScreenMediaSource::getFormat(int32_t client_id)
{
    ALOGV("[%s %d]", __FUNCTION__, __LINE__);

    Mutex::Autolock autoLock(mLock);
    sp<MetaData> meta = new MetaData;

    meta->setInt32(kKeyWidth, mWidth);
    meta->setInt32(kKeyHeight, mHeight);
    meta->setInt32(kKeyColorFormat, OMX_COLOR_FormatYUV420Planar);
    meta->setInt32(kKeyStride, mWidth);
    meta->setInt32(kKeySliceHeight, mHeight);

    meta->setInt32(kKeyFrameRate, mFrameRate);
    meta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_VIDEO_RAW);
    return meta;
}

status_t ScreenMediaSource::readBuffer(int32_t client_id, sp<IMemory> buffer, int64_t* pts)
{
    Mutex::Autolock autoLock(mLock);

    long buff_info[3] = {0,0,0};
    int ret = 0;
    int count = 0;
    FrameBufferInfo* frame = NULL;
    SCREENMEDIASOURCEDATATYPE source_data_type;

    ScreenMediaSourceClient* client;
    client = mClientList.valueFor(client_id);
    source_data_type = client->data_type;

    if (!mStarted) {
        ALOGE("[%s %d]", __FUNCTION__, __LINE__);
        return !OK;
    }

    if (SCREENMEDIASOURC_CANVAS_TYPE == source_data_type) {
        if (mCanvasFramesReceived.empty())
            return !OK;

        frame = *mCanvasFramesReceived.begin();
        mCanvasFramesReceived.erase(mCanvasFramesReceived.begin());

        //ALOGE("ptr:%x canvas:%d", frame->buf_ptr, frame->canvas);

        buff_info[0] = kMetadataBufferTypeCanvasSource;
        buff_info[1] = (long)frame->buf_ptr;
        buff_info[2] = (long)frame->canvas;
        memcpy((uint8_t *)buffer->pointer(), &buff_info[0],sizeof(buff_info));

        *pts = frame->timestampUs;

        ALOGV("[%s %d] buf_ptr:%x canvas:%x pts:%llx size:%d OK:%d", __FUNCTION__, __LINE__,
                frame->buf_ptr, frame->canvas, frame->timestampUs, mCanvasFramesReceived.size(), OK);

        if (frame)
            delete frame;

        return OK;

    }

    if (SCREENMEDIASOURC_RAWDATA_TYPE == source_data_type && !mRawBufferQueue.empty()) {
        MediaBuffer* rawBuffer = *mRawBufferQueue.begin();
        mRawBufferQueue.erase(mRawBufferQueue.begin());
        memcpy((char *)buffer->pointer(), (char *)rawBuffer->data(), mWidth*mHeight*3/2);
        rawBuffer->release();
        *pts = 0;
    } else {
        //ALOGE("[%s %d] read raw data fail", __FUNCTION__, __LINE__);
        return !OK;
    }

    if (frame)
        delete frame;

    return OK;
}

status_t ScreenMediaSource::freeBuffer(int32_t client_id, sp<IMemory>buffer) {

    Mutex::Autolock autoLock(mLock);

    if (mStarted == false)
        return OK;

    SCREENMEDIASOURCEDATATYPE source_data_type;
    ScreenMediaSourceClient* client;
    client = mClientList.valueFor(client_id);
    source_data_type = client->data_type;

    if (SCREENMEDIASOURC_CANVAS_TYPE == source_data_type) {
        long buff_info[3] = {0,0,0};
        memcpy(&buff_info[0],(uint8_t *)buffer->pointer(), sizeof(buff_info));

        if (mScreenDev)
            mScreenDev->ops.release_buffer(mScreenDev, (long *)buff_info[1]);
        }

    ++mNumFramesEncoded;

    return OK;
}

int ScreenMediaSource::dataCallBack(aml_screen_buffer_info_t *buffer){
    int ret = NO_ERROR;
    long buff_info[3] = {0,0,0};
    int status = OK;
    ANativeWindowBuffer* buf;
    void *src = NULL;
    void *dest = NULL;

    if ((mStarted) && (mError == false)) {
        if (buffer == NULL || (buffer->buffer_mem == 0)) {
            ALOGE("aquire_buffer fail, ptr:0x%x", buffer);
            return BAD_VALUE;
        }
        if ((mCanvasMode == true) && (buffer->buffer_canvas == 0)) {
            mError = true;
            ALOGE("Could get canvas info from device!");
            return BAD_VALUE;
        }

        FrameBufferInfo* frame = new FrameBufferInfo;
        if (!frame) {
            mScreenDev->ops.release_buffer(mScreenDev, buffer->buffer_mem);
            mError = true;
            ALOGE("Could Alloc Frame!");
            return BAD_VALUE;
        }

        mFrameCount++;

        ++mNumFramesReceived;
        {
            Mutex::Autolock autoLock(mLock);

            int client_num = 0;
            client_num = mClientList.size();
            ScreenMediaSourceClient* client;

            //first, process hdmi and screencatch.
            for (int i = 0; i < client_num; i++) {
                client = mClientList.valueAt(i);
                switch (client->data_type) {
                    case SCREENMEDIASOURC_HANDLE_TYPE: {
                        src = buffer->buffer_mem;

                        if (mANativeWindow.get() == NULL) {
                            ALOGE("Null window");
                            return BAD_VALUE;
                        }
                        ret = mANativeWindow->dequeueBuffer_DEPRECATED(mANativeWindow.get(), &buf);
                        if (ret != 0) {
                            ALOGE("dequeue buffer failed :%s (%d)",strerror(-ret), -ret);
                            return BAD_VALUE;
                        }
                        mANativeWindow->lockBuffer_DEPRECATED(mANativeWindow.get(), buf);
                        sp<GraphicBuffer> graphicBuffer(new GraphicBuffer(buf, false));
                        graphicBuffer->lock(SCREENSOURCE_GRALLOC_USAGE, (void **)&dest);
                        if (dest == NULL) {
                            ALOGE("Invalid Gralloc Handle");
                            return BAD_VALUE;
                        }
                        memcpy(dest, src, mBufferSize);
                        graphicBuffer->unlock();
                        mANativeWindow->queueBuffer_DEPRECATED(mANativeWindow.get(), buf);
                        graphicBuffer.clear();
                        ALOGV("queue one buffer to native window");

                        if (client_num == 1) {//release buffer
                            mScreenDev->ops.release_buffer(mScreenDev, buffer->buffer_mem);
                        }
                    } break;
                    case SCREENMEDIASOURC_RAWDATA_TYPE:{
                        if (mRawBufferQueue.size() < 60) {
                            MediaBuffer* accessUnit = new MediaBuffer(client->width*client->height*3/2);
                            memcpy(accessUnit->data(), buffer->buffer_mem, client->width*client->height*3/2);
                            mRawBufferQueue.push_back(accessUnit);
                        }

                        if (mCanvasClientExist == 0) {//release buffer
                            mScreenDev->ops.release_buffer(mScreenDev, buffer->buffer_mem);
                        }
                    } break;
                    default:{
                        if (mCanvasClientExist == 0) {//release buffer
                            mScreenDev->ops.release_buffer(mScreenDev, buffer->buffer_mem);
                        }
                    }
                }
            }

           //second, process canvas
            if (mCanvasClientExist == 1) {
                for (int i = 0; i < client_num; i++) {
                    client = mClientList.valueAt(i);
                    if (client->data_type == SCREENMEDIASOURC_CANVAS_TYPE) {
                        buff_info[0] = kMetadataBufferTypeCanvasSource;
                        buff_info[1] = (long)buffer->buffer_mem;
                        buff_info[2] = buffer->buffer_canvas;

                        if (mBufferGet == NULL)
                            mBufferGet = mScreenMediaSourceDealer->allocate(3*sizeof(unsigned));

                        FrameBufferInfo* frame = new FrameBufferInfo;

                        frame->buf_ptr = buffer->buffer_mem;
                        frame->canvas = buffer->buffer_canvas;
                        frame->timestampUs = 0;
                        mCanvasFramesReceived.push_back(frame);

                        if (status != OK) {
                            mScreenDev->ops.release_buffer(mScreenDev, buffer->buffer_mem);
                        }
                        mCanvasClientExist = 1;
                    }
                }
            }
            mFrameAvailableCondition.signal();
        }
    }
    return ret;
}

status_t ScreenMediaSource::reset(void) {

    ALOGV("[%s %d]", __FUNCTION__, __LINE__);

    FrameBufferInfo* frame = NULL;

    if (!mStarted) {
        ALOGE("ScreenSource::reset X Do nothing");
        return OK;
    }

    {
		    mStarted = false;
    }

    if (mScreenDev)
        mScreenDev->ops.stop(mScreenDev);

    {
        mFrameAvailableCondition.signal();
        while (!mCanvasFramesReceived.empty()) {
            frame = *mCanvasFramesReceived.begin();
            mCanvasFramesReceived.erase(mCanvasFramesReceived.begin());
            delete frame;
        }
    }

    if (mScreenDev)
        mScreenDev->common.close((struct hw_device_t *)mScreenDev);

    mScreenModule = NULL;

    ALOGV("ScreenSource::reset done");
    return OK;
}


int AmlogicScreenMediaSourceInit(void)
{
    ALOGV("[%s %d]", __FUNCTION__, __LINE__);

    sp<ProcessState> proc(ProcessState::self());
    sp<IServiceManager> sm = defaultServiceManager();

    ScreenMediaSource::instantiate();

    ALOGV("AmlogicScreenMediaSourceInit");

    return 0;
}

} // end of namespace android
