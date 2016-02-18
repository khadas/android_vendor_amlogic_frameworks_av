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
        frameworks/native/include/media/openmax 
#        frameworks/base/media/libstagefright/include \
#        frameworks/base/include/media/stagefright/openmax \

LOCAL_SHARED_LIBRARIES := \
        libvorbisidec libstagefright libstagefright_omx \
        libstagefright_foundation libutils liblog

LOCAL_MODULE := libstagefright_soft_alacdec
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

