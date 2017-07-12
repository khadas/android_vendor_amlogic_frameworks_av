LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
        SoftAmadec.cpp

LOCAL_C_INCLUDES := \
        $(BOARD_AML_VENDOR_PATH)/external/ffmpeg \
        $(TOP)/frameworks/av/media/libstagefright/include \
        $(TOP)/frameworks/native/include/media/openmax \
		$(BOARD_AML_VENDOR_PATH)/external/ffmpeg \
        $(LOCAL_PATH)/../../AmFFmpegAdapter/include

LOCAL_SHARED_LIBRARIES := \
        libamffmpegadapter libstagefright libstagefright_omx libstagefright_foundation libutils liblog
LOCAL_SHARED_LIBRARIES += libmedia_amlogic_support

LOCAL_CFLAGS := -D__STDC_CONSTANT_MACROS # For stdint macros used in FFmpeg.
include  $(AMLOGIC_FRAMEWORKS_AV_CONFIG_MK)
LOCAL_MODULE := libstagefright_soft_amsoftadec

LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
