LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
include $(BOARD_AML_MEDIA_HAL_CONFIG)
LOCAL_SRC_FILES:= \
		ScreenCatch.cpp

LOCAL_C_INCLUDES:= \
        $(TOP)/frameworks/av/media/libstagefright \
        $(TOP)/frameworks/native/include/media/openmax \
        $(TOP)/frameworks/av/media/libstagefright/mpeg2ts \
        $(TOP)/frameworks/native/services/systemwrite \
        $(TOP)/frameworks/native/include/media/hardware \
        $(BOARD_AML_VENDOR_PATH)/frameworks/av

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
        libstagefright_screenmediasource \
        libiscreenmediasource

LOCAL_MODULE:= libstagefright_screencatch

LOCAL_MODULE_TAGS:= optional

include $(BUILD_SHARED_LIBRARY)

############################################

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
		ScreenCatchTest.cpp

LOCAL_C_INCLUDES:= \
        $(TOP)/frameworks/av/media/libstagefright/screencatch \
        $(TOP)/frameworks/native/include/media/openmax \

LOCAL_C_INCLUDES:= \
        $(TOP)/frameworks/av/media/libstagefright \
        $(TOP)/frameworks/av/media/libstagefright/media2ts \
        $(TOP)/frameworks/native/include/media/openmax \
        $(TOP)/frameworks/native/services/systemwrite \
        $(BOARD_AML_VENDOR_PATH)/frameworks/av

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
				libstagefright_screencatch \
				libiscreenmediasource

LOCAL_MODULE:= screencatchtest

LOCAL_MODULE_TAGS:= debug

include $(BUILD_EXECUTABLE)