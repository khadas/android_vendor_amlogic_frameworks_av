LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_C_INCLUDES := \
    $(TOP)/frameworks/native/include/media/openmax \
    $(TOP)/frameworks/av/include/media/ \
    $(TOP)/frameworks/av/media/libstagefright \

LOCAL_SRC_FILES:= \
    IScreenMediaSource.cpp \
    IScreenMediaSourceClient.cpp \

LOCAL_SHARED_LIBRARIES := \
                    libui                     \
                    liblog                    \
                    libcutils                 \
                    libutils                  \
                    libbinder                 \
                    libsonivox                \
                    libicuuc                  \
                    libicui18n                \
                    libexpat                  \
                    libstagefright_foundation \
                    libgui                    \
                    libdl                     \

LOCAL_MODULE:= libiscreenmediasource
LOCAL_MODULE_TAGS:= optional
include $(BUILD_SHARED_LIBRARY)
