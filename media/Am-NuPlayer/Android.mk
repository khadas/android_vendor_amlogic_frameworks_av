LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)
include $(BOARD_AML_MEDIA_HAL_CONFIG)

LOCAL_SRC_FILES:=                       \
        AmGenericSource.cpp               \
        AmSubtitle.cpp                    \
        AmSocketClient.cpp                \
        AmHTTPLiveSource.cpp              \
        AmNuPlayer.cpp                    \
        AmNuPlayerCCDecoder.cpp           \
        AmNuPlayerDecoder.cpp             \
        AmNuPlayerDecoderBase.cpp         \
        AmNuPlayerDecoderPassThrough.cpp  \
        AmNuPlayerDriver.cpp              \
        AmNuPlayerRenderer.cpp            \
        AmNuPlayerStreamListener.cpp      \
        AmRTSPSource.cpp                  \
        AmStreamingSource.cpp             \
		AmUDPSource.cpp					 \

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/Am-Httplive \
    $(LOCAL_PATH)/Am-mpeg2ts \
	$(TOP)/frameworks/av/media/libstagefright/include             \
	$(TOP)/frameworks/av/media/libstagefright/rtsp                \
	$(TOP)/frameworks/av/media/libstagefright/timedtext           \
	$(TOP)/frameworks/av/media/libmediaplayerservice              \
	$(TOP)/frameworks/native/include/media/openmax                \
    $(TOP)/external/curl/include                                  \
    $(LOCAL_PATH)/../..//LibPlayer/third_parts/libcurl-ffmpeg/include \
    $(AMAVUTILS_PATH)/include \
    $(BOARD_AML_VENDOR_PATH)/external/ffmpeg/\
    $(BOARD_AML_VENDOR_PATH)/frameworks/services/systemcontrol

LOCAL_SHARED_LIBRARIES := \
        libbinder \
        libcrypto \
        libcutils \
        libgui \
        libmedia \
        libdrmframework \
        libstagefright \
        libstagefright_foundation \
        libutils \
        libcurl \
        libamavutils \
        libsystemcontrolservice

LOCAL_STATIC_LIBRARIES := \
        libstagefright_rtsp \
        libamhttplive \
        libammpeg2ts \
        libhevcutils \
        libstagefright_timedtext \
        libcurl_base \
        libcurl_common

LOCAL_CFLAGS := -DDOLBY_UDC -D__STDC_CONSTANT_MACROS # For stdint macros used in FFmpeg.

include  $(AMLOGIC_FRAMEWORKS_AV_CONFIG_MK)
LOCAL_SHARED_LIBRARIES += libmedia_amlogic_support

LOCAL_MODULE:= libamnuplayer

LOCAL_MODULE_TAGS := eng

ifeq ($(TARGET_ARCH),arm)
    LOCAL_CFLAGS += -Wno-psabi
endif

include $(BUILD_SHARED_LIBRARY)

################################################

include $(call all-makefiles-under,$(LOCAL_PATH))
