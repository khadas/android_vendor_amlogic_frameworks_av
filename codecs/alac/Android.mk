LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES :=               \
        SoftAlac.cpp             \
        ./libavcodec/alac.c      \
        ./libavutil/mem.c        \
        ./libavutil/log.c        \
        ./libavutil/mathematics.c     
        

LOCAL_C_INCLUDES := \
        frameworks/av/media/libstagefright/include \
        frameworks/native/include/media/openmax \
        $(LOCAL_PATH)/../include
#        frameworks/base/media/libstagefright/include \
#        frameworks/base/include/media/stagefright/openmax \

LOCAL_SHARED_LIBRARIES := \
        libvorbisidec libstagefright libstagefright_omx \
        libstagefright_foundation libutils liblog
include  $(AMLOGIC_FRAMEWORKS_AV_CONFIG_MK)
LOCAL_SHARED_LIBRARIES += libmedia_amlogic_support
LOCAL_MODULE := libstagefright_soft_alacdec
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

