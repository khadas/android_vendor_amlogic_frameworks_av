#ifndef __SOFTAPE_H__
#define __SOFTAPE_H__

#include "SimpleSoftOMXComponent.h"
#include "apedec.h"

namespace android {

struct SoftApe : public SimpleSoftOMXComponent
{
    SoftApe(const char *name, const OMX_CALLBACKTYPE *callbacks, OMX_PTR appData,OMX_COMPONENTTYPE **component);
protected:
    virtual  ~SoftApe();
    virtual  OMX_ERRORTYPE internalGetParameter(OMX_INDEXTYPE index, OMX_PTR params);
    virtual  OMX_ERRORTYPE internalSetParameter(OMX_INDEXTYPE index, const OMX_PTR params);
    virtual  void onQueueFilled(OMX_U32 portIndex);
    virtual  void onPortFlushCompleted(OMX_U32 portIndex);
    virtual  void onPortEnableCompleted(OMX_U32 portIndex, bool enabled);
private:
    APEContext ApeCtx;
	int      init_flag;
	int64_t  mAnchorTimeUs;
	int      mInputBufferCount;
    uint8_t   *extradata;
    int      extradata_size;
    int      sample_rate;
    int      channels;
    int      bits_per_coded_sample;
	enum {
        kNumBuffers = 3
    };

	enum 
	{
        NONE,
        AWAITING_DISABLED,
        AWAITING_ENABLED
    }mOutputPortSettingsChange;
	
	bool isConfigured() const;
    void initPorts();
};


}  // namespace android

#endif