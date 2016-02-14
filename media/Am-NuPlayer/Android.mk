LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:=                       \
        AmGenericSource.cpp               \
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
    $(TOP)/vendor/amlogic/frameworks/av/LibPlayer/amavutils/include

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
        libamavutils

LOCAL_STATIC_LIBRARIES := \
        libstagefright_rtsp \
        libamhttplive \
        libammpeg2ts \
        libstagefright_hevcutils \
        libcurl_base \
        libcurl_common

LOCAL_MODULE:= libamnuplayer

LOCAL_MODULE_TAGS := eng

ifeq ($(TARGET_ARCH),arm)
    LOCAL_CFLAGS += -Wno-psabi
endif

include $(BUILD_SHARED_LIBRARY)

################################################

include $(call all-makefiles-under,$(LOCAL_PATH))