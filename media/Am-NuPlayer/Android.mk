LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)
include $(TOP)/hardware/amlogic/media/media_base_config.mk

LOCAL_SRC_FILES:=                       \
        AmGenericSource.cpp               \
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
	$(TOP)/vendor/amlogic/frameworks/av/media/Am-NuPlayer/Am-Httplive \
    $(TOP)/vendor/amlogic/frameworks/av/media/Am-NuPlayer/Am-mpeg2ts \
	$(TOP)/frameworks/av/media/libstagefright/include             \
	$(TOP)/frameworks/av/media/libstagefright/rtsp                \
	$(TOP)/frameworks/av/media/libstagefright/timedtext           \
	$(TOP)/frameworks/av/media/libmediaplayerservice              \
	$(TOP)/frameworks/native/include/media/openmax                \
    $(TOP)/external/curl/include                                  \
    $(TOP)/vendor/amlogic/frameworks/av/LibPlayer/third_parts/libcurl-ffmpeg/include \
    $(AMAVUTILS_PATH)/include \
    $(TOP)/vendor/amlogic/external/ffmpeg/\
    $(TOP)/vendor/amlogic/frameworks/services/systemcontrol

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
        libamffmpeg \
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
include  $(TOP)/frameworks/av/amlogic/config.mk
LOCAL_MODULE:= libamnuplayer

LOCAL_MODULE_TAGS := eng

ifeq ($(TARGET_ARCH),arm)
    LOCAL_CFLAGS += -Wno-psabi
endif

include $(BUILD_SHARED_LIBRARY)

################################################

include $(call all-makefiles-under,$(LOCAL_PATH))
