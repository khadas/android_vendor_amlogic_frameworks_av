LOCAL_PATH:= $(call my-dir)

include $(BOARD_AML_MEDIA_HAL_CONFIG)
#
# libmedia_amlogic.so
#

include $(CLEAR_VARS)

LOCAL_KK=0
ifeq ($(GPU_TYPE),t83x)
LOCAL_KK:=1
endif
ifeq ($(GPU_ARCH),midgard)
LOCAL_KK:=1
endif
ifeq ($(LOCAL_KK),1)
LOCAL_CFLAGS += -DMALI_AFBC_GRALLOC=1
else
LOCAL_CFLAGS += -DMALI_AFBC_GRALLOC=0
endif

MESON_GRALLOC_DIR?=$(BOARD_AML_HEADWARE_PATH)/gralloc


ifeq ($(BOARD_WIDEVINE_OEMCRYPTO_LEVEL),1)
LOCAL_CFLAGS += -DBOARD_WIDEVINE_SUPPORTLEVEL=1
else
LOCAL_CFLAGS += -DBOARD_WIDEVINE_SUPPORTLEVEL=3
endif

ifeq ($(LIVEPLAY_SEEK), true)
 LOCAL_CFLAGS += -DLIVEPLAY_SEEK
endif

ifeq ($(BUILD_WITH_AMLOGIC_PLAYER),true)
    LOCAL_SRC_FILES +=                          \
        AmSuperPlayer.cpp                       \
        AmlogicPlayer.cpp                       \
        SubSource.cpp                       \
        AmlogicPlayerRender.cpp                 \
        AmlogicPlayerStreamSource.cpp           \
        AmlogicPlayerStreamSourceListener.cpp   \
        AmlogicPlayerExtractorDemux.cpp         \
        AmlogicPlayerExtractorDataSource.cpp    \
        AmlogicPlayerDataSouceProtocol.cpp      \
        AmlPlayerMetadataRetriever0.cpp \
        AmlogicMediaFactory.cpp \
        SStreamingExtractor.cpp \
		AmSysControl.cpp


else
ifeq ($(TARGET_WITH_AMNUPLAYER),true)
LOCAL_SRC_FILES +=  AmlogicMediaFactory.cpp
endif
endif




LOCAL_SHARED_LIBRARIES :=       \
    libbinder                   \
    libcamera_client            \
    libcutils                   \
    libutils \
    liblog                      \
    libdl                       \
    libgui                      \
    libmedia                    \
    libsonivox                  \
    libstagefright              \
    libutils                    \
    libvorbisidec               \
    libdrmframework \
    libmediaplayerservice              \
    libstagefright              \
    libstagefright_foundation \
    libion \
    libsystemcontrolservice


ifeq ($(TARGET_WITH_AMNUPLAYER),true)
LOCAL_SHARED_LIBRARIES +=\
	 libamnuplayer

##flags for NUPLAYER
LOCAL_CFLAGS += -DBUILD_WITH_AMNUPLAYER=1
endif

LOCAL_C_INCLUDES :=                                                 \
    $(call include-path-for, graphics corecg)                       \
    $(TOP)/frameworks/av/media/libstagefright                       \
    $(TOP)/frameworks/av/media/libstagefright/include               \
    $(TOP)/frameworks/av/media/libstagefright/rtsp                  \
    $(TOP)/frameworks/av/media/libstagefright/wifi-display          \
    $(TOP)/frameworks/native/include/media/openmax                  \
    $(TOP)/external/tremolo/Tremolo                                 \
    $(TOP)/frameworks/av/media/libmediaplayerservice                                 \
    $(TOP)/frameworks/av/include/media					\
    $(TOP)/external/icu/icu4c/source/common				\
    $(TOP)/external/icu/icu4c/source/i18n					\
    $(TOP)/$(MESON_GRALLOC_DIR) \
    $(TOP)/system/core/libion/include/\
    $(BOARD_AML_VENDOR_PATH)/frameworks/av/media/Am-NuPlayer \
    $(TOP)/frameworks/native/include \
    $(BOARD_AML_VENDOR_PATH)/frameworks/services/systemcontrol

ifeq ($(BOARD_PLAYREADY_LP_IN_SS), true)
LOCAL_CFLAGS += -DSS_MSPLAYREADY_TEST
endif
ifeq ($(BOARD_PLAYREADY_TVP),true)
LOCAL_CFLAGS += -DBOARD_PLAYREADY_TVP
endif
ifeq ($(BUILD_WITH_AMLOGIC_PLAYER),true)
    AMPLAYER_APK_DIR=$(LOCAL_PATH)/../LibPlayer/
    LOCAL_C_INCLUDES += \
        $(AMPLAYER_APK_DIR)/amplayer/player/include     \
        $(AMPLAYER_APK_DIR)/amplayer/control/include    \
        $(AMPLAYER_APK_DIR)/amffmpeg/\
		$(AMCODEC_NEED_INCLUDE)\
		$(AMVDEC_PATH)/include

   LOCAL_SHARED_LIBRARIES += libui
   LOCAL_SHARED_LIBRARIES += libamplayer libamavutils libamvdec
   LOCAL_CFLAGS += -DBUILD_WITH_AMLOGIC_PLAYER=1
endif

ifdef UNUSE_SCREEN_MODE
LOCAL_CFLAGS += -DUNUSE_SCREEN_MODE
endif

LOCAL_MODULE:= libmedia_amlogic

LOCAL_32_BIT_ONLY := true

include $(BUILD_SHARED_LIBRARY)

include $(call all-makefiles-under,$(LOCAL_PATH))


