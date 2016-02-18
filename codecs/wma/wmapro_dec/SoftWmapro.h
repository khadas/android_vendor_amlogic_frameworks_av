#ifndef __SOFTWMASTD_H__
#define __SOFTWMASTD_H__

#include "SimpleSoftOMXComponent.h"
#include "wmapro_dec_api.h"

namespace android {

struct SoftWmapro : public SimpleSoftOMXComponent
{
    SoftWmapro(const char *name,
            const OMX_CALLBACKTYPE *callbacks,
            OMX_PTR appData,
            OMX_COMPONENTTYPE **component);

protected:
    virtual ~SoftWmapro();

    virtual OMX_ERRORTYPE internalGetParameter(
            OMX_INDEXTYPE index, OMX_PTR params);

    virtual OMX_ERRORTYPE internalSetParameter(
            OMX_INDEXTYPE index, const OMX_PTR params);

    virtual void onQueueFilled(OMX_U32 portIndex);
    virtual void onPortFlushCompleted(OMX_U32 portIndex);
    virtual void onPortEnableCompleted(OMX_U32 portIndex, bool enabled);

private:
	int Pkt_Reserve_buf_offset;
	
	int init_flag;
    waveformat_t wfext;
    AudioContext *wmactx;
	int64_t mAnchorTimeUs;
	int mInputBufferCount;
	enum {
        kNumBuffers = 2
    };
	enum 
	{
        NONE,
        AWAITING_DISABLED,
        AWAITING_ENABLED
    } mOutputPortSettingsChange;
	
	bool isConfigured() const;
    void initPorts();
    void initDecoder();

};


}  // namespace android

#endif