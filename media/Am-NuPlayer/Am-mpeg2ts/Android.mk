LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:=                 \
        AmAnotherPacketSource.cpp   \
        AmATSParser.cpp             \
        AmESQueue.cpp               \
		AmMPEG2TSExtractor.cpp		\

LOCAL_C_INCLUDES:= \
	$(TOP)/frameworks/av/media/libstagefright \
	$(TOP)/frameworks/native/include/media/openmax\
	$(LOCAL_PATH)/../hevc_utils \

LOCAL_CFLAGS += -Werror -Wall
LOCAL_CLANG := true
LOCAL_SANITIZE := unsigned-integer-overflow signed-integer-overflow
include  $(TOP)/frameworks/av/amlogic/config.mk
LOCAL_SHARED_LIBRARIES += libmedia_amlogic_support
LOCAL_MODULE:= libammpeg2ts

ifeq ($(TARGET_ARCH),arm)
    LOCAL_CFLAGS += -Wno-psabi
endif
LOCAL_CFLAGS += -DDOLBY_UDC -DDOLBY_UDC_STREAMING_HLS

include $(BUILD_STATIC_LIBRARY)
