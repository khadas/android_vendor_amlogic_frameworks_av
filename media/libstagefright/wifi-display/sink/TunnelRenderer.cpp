/*
 * Copyright 2012, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "TunnelRenderer"
#include <utils/Log.h>

#include "TunnelRenderer.h"

#include "ATSParser.h"
#include <binder/IMemory.h>
#include <binder/IServiceManager.h>
#include <gui/SurfaceComposerClient.h>
#include <gui/ISurfaceComposer.h>
#include <media/IMediaPlayerService.h>
#include <media/IStreamSource.h>
#include <media/mediaplayer.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include <ui/DisplayInfo.h>
#include <cutils/properties.h>
namespace android
{

    struct TunnelRenderer::PlayerClient : public BnMediaPlayerClient
    {
        PlayerClient() {}

        virtual void notify(int msg, int ext1, int ext2, const Parcel *obj)
        {
            ALOGI("notify %d, %d, %d", msg, ext1, ext2);
        }

    protected:
        virtual ~PlayerClient()
        {
            ALOGI("~PlayerClient");
        }

    private:
        DISALLOW_EVIL_CONSTRUCTORS(PlayerClient);
    };

    struct TunnelRenderer::StreamSource : public BnStreamSource
    {
        StreamSource(TunnelRenderer *owner);

        virtual void setListener(const sp<IStreamListener> &listener);
        virtual void setBuffers(const Vector<sp<IMemory> > &buffers);

        virtual void onBufferAvailable(size_t index);

        virtual uint32_t flags() const;

        void doSomeWork();

    protected:
        virtual ~StreamSource();

    private:
        mutable Mutex mLock;

        TunnelRenderer *mOwner;

        sp<IStreamListener> mListener;

        Vector<sp<IMemory> > mBuffers;
        List<size_t> mIndicesAvailable;

        size_t mNumDeqeued;

        DISALLOW_EVIL_CONSTRUCTORS(StreamSource);
    };

    ////////////////////////////////////////////////////////////////////////////////

    TunnelRenderer::StreamSource::StreamSource(TunnelRenderer *owner)
        : mOwner(owner),
          mNumDeqeued(0)
    {
    }

    TunnelRenderer::StreamSource::~StreamSource()
    {
        ALOGI("~StreamSource");
    }

    void TunnelRenderer::StreamSource::setListener(
        const sp<IStreamListener> &listener)
    {
        mListener = listener;
    }

    void TunnelRenderer::StreamSource::setBuffers(
        const Vector<sp<IMemory> > &buffers)
    {
        mBuffers = buffers;
    }

    void TunnelRenderer::StreamSource::onBufferAvailable(size_t index)
    {
        if (index >= 0x80000000)
        {
            doSomeWork();
            return ;/*require buffer command,for amplayer only. ignore.now*/
        }
        CHECK_LT(index, mBuffers.size());

        {
            Mutex::Autolock autoLock(mLock);
            mIndicesAvailable.push_back(index);
        }

        doSomeWork();
    }

    uint32_t TunnelRenderer::StreamSource::flags() const
    {
        return kFlagAlignedVideoData;
    }

    void TunnelRenderer::StreamSource::doSomeWork()
    {
        Mutex::Autolock autoLock(mLock);

        while (!mIndicesAvailable.empty())
        {
            sp<ABuffer> srcBuffer = mOwner->dequeueBuffer();
            if (srcBuffer == NULL)
            {
                break;
            }

            ++mNumDeqeued;

            if (mNumDeqeued == 1)
            {
                ALOGI("fixing real time now.");

                sp<AMessage> extra = new AMessage;

                extra->setInt32(
                    IStreamListener::kKeyDiscontinuityMask,
                    ATSParser::DISCONTINUITY_ABSOLUTE_TIME);

                extra->setInt64("timeUs", ALooper::GetNowUs());

                mListener->issueCommand(
                    IStreamListener::DISCONTINUITY,
                    false /* synchronous */,
                    extra);
            }

            ALOGV("dequeue TS packet of size %d", srcBuffer->size());

            size_t index = *mIndicesAvailable.begin();
            mIndicesAvailable.erase(mIndicesAvailable.begin());

            sp<IMemory> mem = mBuffers.itemAt(index);
            CHECK_LE(srcBuffer->size(), mem->size());
            CHECK_EQ((srcBuffer->size() % 188), 0u);

            memcpy(mem->pointer(), srcBuffer->data(), srcBuffer->size());
            mListener->queueBuffer(index, srcBuffer->size());
        }
    }

    ////////////////////////////////////////////////////////////////////////////////

    TunnelRenderer::TunnelRenderer(
        const sp<AMessage> &notifyLost,
        const sp<IGraphicBufferProducer> &bufferProducer,
        const sp<AMessage> &stopNotify)
        : mNotifyLost(notifyLost),
          mBufferProducer(bufferProducer),
          mTotalBytesQueued(0ll),
          mMaxBytesQueued(0ll),
          mMinBytesQueued(0ll),
          mRetryTimes(0ll),
          mDebugEnable(false),
          mBytesQueued(0),
          mLastDequeuedExtSeqNo(-1),
          mFirstFailedAttemptUs(-1ll),
          mPackageSuccess(0),
          mPackageFailed(0),
          mPackageRequest(0),
          mRequestedRetry(false),
          mIsHDCP(false),
          mRequestedRetransmission(false),
          mStopNotify(stopNotify)
    {

        sp<IServiceManager> sm = defaultServiceManager();
        if (sm == NULL)
        {
            ALOGE("Couldn't get default ServiceManager\n");
            return ;
        }
        mCurTime = ALooper::GetNowUs();
        mSystemControlService = interface_cast<ISystemControlService>(sm->getService(String16("system_control")));
        if (mSystemControlService == NULL)
        {
            ALOGE("Couldn't get connection to SystemControlService\n");
            return ;
        }
        int d = mSystemControlService->getPropertyInt(String16("sys.wfddump"), 0) ;
        d &= 0x02;
        if (d == 2)
            mDebugEnable = true;
        else
            mDebugEnable = false;
        ALOGE("Miracast debug info enable? d=%d, mDebugEnable =%d\n", d , mDebugEnable );
        if (mDebugEnable)
            ALOGE("Miracast debug info enabled\n");
        mSystemControlService->setProperty(String16("sys.pkginfo"), String16("suc:0,fail:0,req:0,total:0, max:0,min:0,retry:0,band:0"));
    }

    TunnelRenderer::~TunnelRenderer()
    {
        destroyPlayer();
    }

    void TunnelRenderer::queueBuffer(const sp<ABuffer> &buffer)
    {
        Mutex::Autolock autoLock(mLock);

        mTotalBytesQueued += buffer->size();
        mBytesQueued += buffer->size();
        mMaxBytesQueued = mMaxBytesQueued > mTotalBytesQueued ? mMaxBytesQueued : mTotalBytesQueued;
        if (mPackets.empty())
        {
            mPackets.push_back(buffer);
            return;
        }

        int32_t newExtendedSeqNo = buffer->int32Data();

        List<sp<ABuffer> >::iterator firstIt = mPackets.begin();
        List<sp<ABuffer> >::iterator it = --mPackets.end();
        for (;;)
        {
            int32_t extendedSeqNo = (*it)->int32Data();

            if (extendedSeqNo == newExtendedSeqNo)
            {
                // Duplicate packet.
                return;
            }

            if (extendedSeqNo < newExtendedSeqNo)
            {
                // Insert new packet after the one at "it".
                mPackets.insert(++it, buffer);
                return;
            }

            if (it == firstIt)
            {
                // Insert new packet before the first existing one.
                mPackets.insert(it, buffer);
                return;
            }

            --it;
        }
    }

    sp<ABuffer> TunnelRenderer::dequeueBuffer()
    {
        Mutex::Autolock autoLock(mLock);

        sp<ABuffer> buffer;
        int32_t extSeqNo;
        char pkg_info[128];
        int64_t curUs;

        while (!mPackets.empty())
        {
            buffer = *mPackets.begin();
            extSeqNo = buffer->int32Data();

            if (mLastDequeuedExtSeqNo < 0 || extSeqNo > mLastDequeuedExtSeqNo)
            {
                break;
            }

            // This is a retransmission of a packet we've already returned.

            mTotalBytesQueued -= buffer->size();
            mMinBytesQueued = mMinBytesQueued < mTotalBytesQueued ? mMinBytesQueued : mTotalBytesQueued;
            buffer.clear();
            extSeqNo = -1;

            mPackets.erase(mPackets.begin());
        }

        if (mPackets.empty())
        {
            if (mFirstFailedAttemptUs < 0ll)
            {
                mFirstFailedAttemptUs = ALooper::GetNowUs();
                mRequestedRetry = false;
                mRequestedRetransmission = false;
            }
            else
            {
                float  noPacketTime = (ALooper::GetNowUs() - mFirstFailedAttemptUs) / 1E6;
                ALOGV("no packets available for %.2f secs", noPacketTime);
                if (noPacketTime > 12.0) //beyond 12S
                {
                    ALOGI("no packets available beyond 12 secs,stop WifiDisplaySink now");
                    sp<AMessage> notify = mStopNotify->dup();
                    notify->post();
                    mFirstFailedAttemptUs = ALooper::GetNowUs();
                }


            }

            return NULL;
        }

        if (mLastDequeuedExtSeqNo < 0 || extSeqNo == mLastDequeuedExtSeqNo + 1)
        {
            if (mRequestedRetransmission)
            {
                ALOGI("Recovered after requesting retransmission of %d",
                      extSeqNo);
            }

            if (!mRequestedRetry)
            {
                mPackageSuccess++;
            }
            else
            {
                mRequestedRetry = false;
                mPackageRequest++;
            }
            if (mDebugEnable)
            {
                /*calculate bandwidth every 1s once*/
                curUs = ALooper::GetNowUs();
                if (curUs - mCurTime >= 1000000ll)
                {
                    mBandwidth = mBytesQueued * 1000000ll / (curUs - mCurTime);
                    //ALOGI("309  curUs - mCurTime =%lld, bytes=%ld", curUs-mCurTime, mBytesQueued);
                    mBytesQueued = 0;
                    mCurTime = ALooper::GetNowUs();
                    sprintf(pkg_info, "suc:%d,fail:%d,req:%d, total:%lld, max:%lld,min:%lld,retry:%lld, band:%lld",
                            mPackageSuccess, mPackageFailed, mPackageRequest,
                            mTotalBytesQueued, mMaxBytesQueued, mMinBytesQueued,
                            mRetryTimes, mBandwidth);
                    mSystemControlService->setProperty(String16("sys.pkginfo"), String16(pkg_info));
                }
            }
            mLastDequeuedExtSeqNo = extSeqNo;
            mFirstFailedAttemptUs = -1ll;
            mRequestedRetransmission = false;

            mPackets.erase(mPackets.begin());

            mTotalBytesQueued -= buffer->size();
            mMinBytesQueued = mMinBytesQueued < mTotalBytesQueued ? mMinBytesQueued : mTotalBytesQueued;
            return buffer;
        }

        if (mFirstFailedAttemptUs < 0ll)
        {
            mFirstFailedAttemptUs = ALooper::GetNowUs();

            ALOGI("failed to get the correct packet the first time.");
            return NULL;
        }

        if (mFirstFailedAttemptUs + 50000ll > ALooper::GetNowUs())
        {
            // We're willing to wait a little while to get the right packet.

            if (!mRequestedRetransmission)
            {
                ALOGI("requesting retransmission of seqNo %d",
                      (mLastDequeuedExtSeqNo + 1) & 0xffff);

                sp<AMessage> notify = mNotifyLost->dup();
                notify->setInt32("seqNo", (mLastDequeuedExtSeqNo + 1) & 0xffff);
                notify->post();

                mRequestedRetry = true;
                mRequestedRetransmission = true;
                mRetryTimes++;
            }
            else
            {
                ALOGI("still waiting for the correct packet to arrive.");
            }

            return NULL;
        }

        ALOGI("dropping packet. extSeqNo %d didn't arrive in time",
              mLastDequeuedExtSeqNo + 1);

        // Permanent failure, we never received the packet.
        mPackageFailed++;

        if (mDebugEnable)
        {
            /*calculate bandwidth every 2s once*/
            curUs = ALooper::GetNowUs();
            if (curUs - mCurTime >= 1000000ll)
            {
                mBandwidth = mBytesQueued * ((float)1000000ll / (curUs - mCurTime));
                //ALOGI("367 curUs - mCurTime =%lld, bytes=%ld", curUs-mCurTime, mBytesQueued);
                mBytesQueued = 0;
                mCurTime = ALooper::GetNowUs();
                sprintf(pkg_info, "suc:%d,fail:%d,req:%d, total:%lld, max:%lld,min:%lld,retry:%lld, band:%lld",
                        mPackageSuccess, mPackageFailed, mPackageRequest,
                        mTotalBytesQueued, mMaxBytesQueued, mMinBytesQueued,
                        mRetryTimes, mBandwidth);
                mSystemControlService->setProperty(String16("sys.pkginfo"), String16(pkg_info));
            }
        }
        mLastDequeuedExtSeqNo = extSeqNo;
        mFirstFailedAttemptUs = -1ll;
        mRequestedRetransmission = false;
        mRequestedRetry = false;

        mTotalBytesQueued -= buffer->size();
        mMinBytesQueued = mMinBytesQueued < mTotalBytesQueued ? mMinBytesQueued : mTotalBytesQueued;
        mPackets.erase(mPackets.begin());

        return buffer;
    }

    void TunnelRenderer::onMessageReceived(const sp<AMessage> &msg)
    {
        switch (msg->what())
        {
        case kWhatQueueBuffer:
        {
            sp<ABuffer> buffer;
            CHECK(msg->findBuffer("buffer", &buffer));

            queueBuffer(buffer);

            if (mStreamSource == NULL)
            {
                if (mTotalBytesQueued > 0ll)
                {
                    initPlayer();
                }
                else
                {
                    ALOGI("Have %lld bytes queued...", mTotalBytesQueued);
                }
            }
            else
            {
                mStreamSource->doSomeWork();
            }
            break;
        }

        default:
            TRESPASS();
        }
    }

    void TunnelRenderer::initPlayer()
    {
        if (mBufferProducer == NULL)
        {
            mComposerClient = new SurfaceComposerClient;
            CHECK_EQ(mComposerClient->initCheck(), (status_t)OK);

            DisplayInfo info;
            SurfaceComposerClient::getDisplayInfo(SurfaceComposerClient::getBuiltInDisplay(ISurfaceComposer::eDisplayIdMain), &info);
            ssize_t displayWidth = info.w;
            ssize_t displayHeight = info.h;

            mSurfaceControl =
                mComposerClient->createSurface(
                    String8("A Surface"),
                    displayWidth,
                    displayHeight,
                    PIXEL_FORMAT_RGB_565,
                    0);

            CHECK(mSurfaceControl != NULL);
            CHECK(mSurfaceControl->isValid());

            SurfaceComposerClient::openGlobalTransaction();
            CHECK_EQ(mSurfaceControl->setLayer(INT_MAX), (status_t)OK);
            CHECK_EQ(mSurfaceControl->show(), (status_t)OK);
            SurfaceComposerClient::closeGlobalTransaction();

            mSurface = mSurfaceControl->getSurface();
            CHECK(mSurface != NULL);
        }

        sp<IServiceManager> sm = defaultServiceManager();
        sp<IBinder> binder = sm->getService(String16("media.player"));
        sp<IMediaPlayerService> service = interface_cast<IMediaPlayerService>(binder);
        CHECK(service.get() != NULL);

        mStreamSource = new StreamSource(this);

        mPlayerClient = new PlayerClient;

        //mPlayer = service->create(getpid(), mPlayerClient, 0);
        mPlayer = service->create( mPlayerClient, 0);
        CHECK(mPlayer != NULL);
        CHECK_EQ(mPlayer->setDataSource(mStreamSource), (status_t)OK);


        mSystemControlService->setProperty(String16("media.libplayer.wfd"), String16("1"));
        mSystemControlService->setProperty(String16("media.libplayer.fastswitch"), String16("2"));
        if (mIsHDCP)
        {
            ALOGI("HDCP Enabled!!!");
            Parcel data;
            data.writeInt32(1);
            mPlayer->setParameter(KEY_PARAMETER_AML_PLAYER_USE_SOFT_DEMUX, data);
        }
        //mPlayer->setVideoSurfaceTexture(
        //        mBufferProducer != NULL ? mBufferProducer : mSurface->getSurfaceTexture());
        mPlayer->setVideoSurfaceTexture(mSurface->getIGraphicBufferProducer());
        Parcel request;
        mPlayer->setParameter(KEY_PARAMETER_AML_PLAYER_DIS_AUTO_BUFFER, request);
        mPlayer->prepareAsync();
        request.writeString16(String16("freerun_mode:60"));
        mPlayer->setParameter(KEY_PARAMETER_AML_PLAYER_FREERUN_MODE, request);
        mPlayer->start();
    }

    void TunnelRenderer::destroyPlayer()
    {
        Parcel request;
        mPlayer->setParameter(KEY_PARAMETER_AML_PLAYER_ENA_AUTO_BUFFER, request);
        request.writeString16(String16("freerun_mode:0"));
        mPlayer->setParameter(KEY_PARAMETER_AML_PLAYER_FREERUN_MODE, request);
        mPlayerClient.clear();
        mStreamSource.clear();
        if (mIsHDCP)
        {
            Parcel data;
            data.writeInt32(0);
            mPlayer->setParameter(KEY_PARAMETER_AML_PLAYER_USE_SOFT_DEMUX, data);
        }
        mSystemControlService->setProperty(String16("media.libplayer.wfd"), String16("0"));
        mSystemControlService->setProperty(String16("media.libplayer.fastswitch"), String16("0"));
        mPlayer->stop();
        mPlayer.clear();

        if (mBufferProducer == NULL)
        {
            mSurface.clear();
            mSurfaceControl.clear();

            mComposerClient->dispose();
            mComposerClient.clear();
        }
    }

    void TunnelRenderer::setIsHDCP(bool isHDCP)
    {
        mIsHDCP = isHDCP;
    }

}  // namespace android

