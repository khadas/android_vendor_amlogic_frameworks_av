LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)
LOCAL_SRC_FILES := \
        SoftWmapro.cpp  \
        bitstream.c     \
        WmaproApi.c           \
        wmaprodec.c        \        

LOCAL_C_INCLUDES := \
        frameworks/av/media/libstagefright/include \
        frameworks/native/include/media/openmax \
        $(LOCAL_PATH)/../../include

LOCAL_SHARED_LIBRARIES := \
        libstagefright libstagefright_omx \
        libstagefright_foundation libutils  liblog

include  $(AMLOGIC_FRAMEWORKS_AV_CONFIG_MK)
LOCAL_SHARED_LIBRARIES += libmedia_amlogic_support
LOCAL_MODULE := libstagefright_soft_wmaprodec
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

