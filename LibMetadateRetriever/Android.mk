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

LOCAL_STATIC_LIBRARIES :=       \
    libamthumbnail  \

LOCAL_C_INCLUDES :=                                                 \
    $(TOP)/frameworks/av/              \
    $(TOP)/frameworks/av/include/media \
    $(TOP)/external/ffmpeg \
    $(TOP)/external/icu/icu4c/source/common				\
    $(TOP)/external/icu/icu4c/source/i18n					\

LOCAL_MODULE:= libamlogic_metadata_retriever

LOCAL_32_BIT_ONLY := true

include $(BUILD_SHARED_LIBRARY)

include $(call all-makefiles-under,$(LOCAL_PATH))


