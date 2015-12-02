LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:=                 \
        AmAnotherPacketSource.cpp   \
        AmATSParser.cpp             \
        AmESQueue.cpp               \

LOCAL_C_INCLUDES:= \
	$(TOP)/frameworks/av/media/libstagefright \
	$(TOP)/frameworks/native/include/media/openmax

LOCAL_CFLAGS += -Werror

LOCAL_MODULE:= libammpeg2ts

ifdef DOLBY_UDC
#ifdef DOLBY_UDC_STREAMING_HLS
  LOCAL_CFLAGS += -DDOLBY_UDC
  LOCAL_CFLAGS += -DDOLBY_UDC_STREAMING_HLS
#endif
endif #DOLBY_UDC
ifeq ($(TARGET_ARCH),arm)
    LOCAL_CFLAGS += -Wno-psabi
endif

include $(BUILD_STATIC_LIBRARY)
