LOCAL_PATH:= $(call my-dir)

#
# libamlogic_metadata_retriever.so
#

include $(CLEAR_VARS)
include $(BOARD_AML_MEDIA_HAL_CONFIG)
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
    $(BOARD_AML_VENDOR_PATH)/external/ffmpeg \
	$(LOCAL_PATH)/../AmFFmpegAdapter/include/ \
	$(TOP)/frameworks/native/include/media/openmax \
    $(TOP)/external/icu/icu4c/source/common				\
    $(TOP)/external/icu/icu4c/source/i18n					\

LOCAL_MODULE:= libamlogic_metadata_retriever

LOCAL_32_BIT_ONLY := true

include $(BUILD_SHARED_LIBRARY)

include $(call all-makefiles-under,$(LOCAL_PATH))


