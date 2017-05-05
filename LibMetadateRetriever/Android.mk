LOCAL_PATH:= $(call my-dir)

#
# libamlogic_metadata_retriever.so
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES +=                          \
        AmlPlayerMetadataRetriever.cpp \
        AmlogicMetadataRetrieverFactory.cpp \


LOCAL_SHARED_LIBRARIES :=       \
    libcutils                   \
    libutils \
    liblog                      \
    libmedia \
    libmediaplayerservice       \
    libamffmpeg \
    libamffmpegadapter \
    libstagefright \

LOCAL_STATIC_LIBRARIES :=       \
    libamthumbnail  \

LOCAL_C_INCLUDES :=                                                 \
    $(TOP)/frameworks/av/              \
    $(TOP)/frameworks/av/include/media \
    $(TOP)/vendor/amlogic/external/ffmpeg \
	$(TOP)/vendor/amlogic/frameworks/av/AmFFmpegAdapter/include/ \
	$(TOP)/frameworks/native/include/media/openmax \
    $(TOP)/external/icu/icu4c/source/common				\
    $(TOP)/external/icu/icu4c/source/i18n					\

LOCAL_MODULE:= libamlogic_metadata_retriever

LOCAL_32_BIT_ONLY := true

include $(BUILD_SHARED_LIBRARY)

include $(call all-makefiles-under,$(LOCAL_PATH))


