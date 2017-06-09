LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_SRC_FILES:=               \
        AmLiveDataSource.cpp      \
        AmHLSDataSource.cpp       \
        AmLiveSession.cpp         \
        AmM3UParser.cpp           \
        AmPlaylistFetcher.cpp     \
        StreamSniffer.cpp		\
	    AmHTTPDownloader.cpp
LOCAL_C_INCLUDES:= \
	$(TOP)/frameworks/av/media/libstagefright \
	$(TOP)/frameworks/native/include/media/openmax \
	$(TOP)/external/boringssl/src/include \
    $(TOP)/external/curl/include \
    $(LOCAL_PATH)/../../../LibPlayer/third_parts/libcurl-ffmpeg/include \
    $(LOCAL_PATH)/../Am-mpeg2ts \
	$(LOCAL_PATH)/../hevc_utils \

LOCAL_CFLAGS += -Werror

LOCAL_MODULE:= libamhttplive

ifeq ($(TARGET_ARCH),arm)
    LOCAL_CFLAGS += -Wno-psabi
endif

include $(BUILD_STATIC_LIBRARY)
