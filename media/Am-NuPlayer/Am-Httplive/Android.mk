LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:=               \
        AmLiveDataSource.cpp      \
        AmLiveSession.cpp         \
        AmM3UParser.cpp           \
        AmPlaylistFetcher.cpp     \
        StreamSniffer.cpp

LOCAL_C_INCLUDES:= \
	$(TOP)/frameworks/av/media/libstagefright \
	$(TOP)/frameworks/native/include/media/openmax \
	$(TOP)/external/boringssl/src/include \
    $(TOP)/external/curl/include \
    $(TOP)/vendor/amlogic/frameworks/av/LibPlayer/third_parts/libcurl-ffmpeg/include \
    $(TOP)/vendor/amlogic/frameworks/av/media/Am-NuPlayer/Am-mpeg2ts \

LOCAL_CFLAGS += -Werror

LOCAL_MODULE:= libamhttplive

ifeq ($(TARGET_ARCH),arm)
    LOCAL_CFLAGS += -Wno-psabi
endif

include $(BUILD_STATIC_LIBRARY)
