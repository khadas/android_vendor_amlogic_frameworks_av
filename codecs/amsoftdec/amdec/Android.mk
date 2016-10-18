#####################################################
# amlogic soft decoder based on ffmpeg
####################################################

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := libstagefright_soft_amsoftdec
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_SUFFIX := .so
LOCAL_MULTILIB := 32
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_PATH_32 := $(TARGET_OUT)/lib/
LOCAL_SRC_FILES_arm := $(LOCAL_MODULE)$(LOCAL_MODULE_SUFFIX)

include $(BUILD_PREBUILT)
