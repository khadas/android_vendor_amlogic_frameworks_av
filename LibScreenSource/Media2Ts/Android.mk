LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
include $(BOARD_AML_MEDIA_HAL_CONFIG)
LOCAL_SRC_FILES:= \
        esconvertor.cpp     \
        tspack.cpp    \

LOCAL_C_INCLUDES:= \
        $(TOP)/frameworks/av/media/libstagefright \
        $(TOP)/frameworks/av/media/libstagefright/media2ts \
        $(TOP)/frameworks/native/include/media/openmax \
        $(TOP)/frameworks/native/services/systemwrite \
        $(BOARD_AML_VENDOR_PATH)/frameworks/av

LOCAL_SHARED_LIBRARIES:= \
        libbinder                       \
        libcutils                       \
        libgui                          \
        libmedia                        \
        libstagefright                  \
        libstagefright_foundation       \
        libui                           \
        libutils                        \
        libiscreenmediasource


LOCAL_MODULE:= libstagefright_mediaconvertor

LOCAL_MODULE_TAGS:= optional

include $(BUILD_SHARED_LIBRARY)
################################################################################

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
        videomediaconvertortest.cpp                 \

LOCAL_C_INCLUDES:= \
        $(TOP)/frameworks/av/media/libstagefright \
        $(TOP)/frameworks/av/media/libstagefright/media2ts \
        $(TOP)/frameworks/native/include/media/openmax \
        $(TOP)/frameworks/native/services/systemwrite \
        $(BOARD_AML_VENDOR_PATH)/frameworks/av

LOCAL_SHARED_LIBRARIES:= \
        libbinder                       \
        libgui                          \
        libmedia                        \
        libstagefright                  \
        libstagefright_foundation       \
        libutils                        \
        libcutils                       \
        libstagefright_mediaconvertor        \
        libiscreenmediasource


LOCAL_MODULE:= videomediaconvertortest

LOCAL_MODULE_TAGS := debug

include $(BUILD_EXECUTABLE)
################################################################################
################################################################################

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
        audiomediaconvertortest.cpp                 \

LOCAL_C_INCLUDES:= \
        $(TOP)/frameworks/av/media/libstagefright \
        $(TOP)/frameworks/av/media/libstagefright/media2ts \
        $(TOP)/frameworks/native/include/media/openmax \
        $(TOP)/frameworks/native/services/systemwrite \
        $(BOARD_AML_VENDOR_PATH)/frameworks/av

LOCAL_SHARED_LIBRARIES:= \
        libbinder                       \
        libgui                          \
        libmedia                        \
        libstagefright                  \
        libstagefright_foundation       \
        libutils                        \
        libcutils                       \
        libstagefright_mediaconvertor        \
        libiscreenmediasource


LOCAL_MODULE:= audiomediaconvertortest

LOCAL_MODULE_TAGS := debug

include $(BUILD_EXECUTABLE)
################################################################################
################################################################################

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
        tspacktest.cpp                 \

LOCAL_C_INCLUDES:= \
        $(TOP)/frameworks/av/media/libstagefright \
        $(TOP)/frameworks/av/media/libstagefright/media2ts \
        $(TOP)/frameworks/native/include/media/openmax \
        $(TOP)/frameworks/native/services/systemwrite \
        $(BOARD_AML_VENDOR_PATH)/frameworks/av

LOCAL_SHARED_LIBRARIES:= \
        libbinder                       \
        libgui                          \
        libmedia                        \
        libstagefright                  \
        libstagefright_foundation       \
        libutils                        \
        libcutils                       \
        libstagefright_mediaconvertor   \
        libiscreenmediasource


LOCAL_MODULE:= tspacktest

LOCAL_MODULE_TAGS := debug

include $(BUILD_EXECUTABLE)
################################################################################