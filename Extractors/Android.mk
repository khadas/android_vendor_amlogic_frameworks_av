LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)
include $(BOARD_AML_MEDIA_HAL_CONFIG)
#include frameworks/av/media/libstagefright/codecs/common/Config.mk

LOCAL_SRC_FILES:=                         \
	ADIFExtractor.cpp                         \
	ADTSExtractor.cpp                         \
	AIFFExtractor.cpp                         \
	DDPExtractor.cpp                         \
	DtshdExtractor.cpp                         \
	LATMExtractor.cpp                         \
	THDExtractor.cpp                         \
	AsfExtractor/ASFExtractor.cpp\
	MediaExtractorPlugin.cpp

LOCAL_SHARED_LIBRARIES := \
        libbinder \
        libcutils \
        libdl \
        libexpat \
        libgui \
        libicui18n \
        libicuuc \
        liblog \
        libmedia \
        libmediautils \
        libnetd_client \
        libopus \
        libsonivox \
        libssl \
        libsync \
        libui \
        libutils \
        libvorbisidec \
        libz \
        libpowermanager

LOCAL_SHARED_LIBRARIES += \
	libstagefright_enc_common \
	libstagefright_avc_common \
	libstagefright_foundation \
	libdl \
	libRScpp \
	libstagefright \

 LOCAL_STATIC_LIBRARIES := \
	 libstagefright_adifdec

LOCAL_C_INCLUDES+= \
	$(TOP)/frameworks/av/media/libstagefright/include  \
	$(TOP)/frameworks/av/media/libmediaplayerservice  \
	$(BOARD_AML_VENDOR_PATH)/external/ffmpeg

LOCAL_CFLAGS += -Wno-multichar -Werror -Wno-error=deprecated-declarations -Wall

LOCAL_CFLAGS += -Werror -Wno-unused-parameter -Wno-unused-variable -Wno-sign-compare \
				-Wno-format -Wno-reorder -Wno-constant-logical-operand -Wno-missing-field-initializers -Wno-sometimes-uninitialized \
				-Wno-writable-strings -Wno-unused-function

# enable experiments only in userdebug and eng builds
ifneq (,$(filter userdebug eng,$(TARGET_BUILD_VARIANT)))
LOCAL_CFLAGS += -DENABLE_STAGEFRIGHT_EXPERIMENTS
endif

LOCAL_CLANG := true



include  $(AMLOGIC_FRAMEWORKS_AV_CONFIG_MK)
LOCAL_SHARED_LIBRARIES += libmedia_amlogic_support

LOCAL_MODULE:= libstagefright_extrator

LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

include $(call all-makefiles-under,$(LOCAL_PATH))
