LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
        ScreenMediaSource.cpp


LOCAL_C_INCLUDES:= \
        $(TOP)/frameworks/av/media/libstagefright \
        $(TOP)/frameworks/native/include/media/openmax \
        $(TOP)/frameworks/av/media/libstagefright/mpeg2ts \
        $(TOP)/frameworks/native/include/media/hardware \
        $(LOCAL_PATH)/../../\

LOCAL_SHARED_LIBRARIES:= \
        libbinder                       \
        libcutils                       \
        liblog                          \
        libgui                          \
        libmedia                        \
        libstagefright                  \
        libstagefright_foundation       \
        libui                           \
        libutils                        \
        libhardware        \
        libiscreenmediasource \


LOCAL_MODULE:= libstagefright_screenmediasource

LOCAL_MODULE_TAGS:= optional

include $(BUILD_SHARED_LIBRARY)