LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
include $(BOARD_AML_MEDIA_HAL_CONFIG)
LOCAL_SRC_FILES:=                 \
        HEVC_utils.cpp

LOCAL_C_INCLUDES:= \
	$(BOARD_AML_VENDOR_PATH)/external/ffmpeg/ \
	$(TOP)/frameworks/av/media/libstagefright \

LOCAL_MODULE:= libhevcutils

include $(BUILD_STATIC_LIBRARY)
