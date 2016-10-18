#####################################################
# h265 soft decoder based on openHEVC
####################################################

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := libstagefright_soft_amh265dec
LOCAL_MODULE_SUFFIX := .so
LOCAL_MULTILIB := 32
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_PATH_32 := $(TARGET_OUT)/lib/
LOCAL_SRC_FILES_arm := $(LOCAL_MODULE)$(LOCAL_MODULE_SUFFIX)
LOCAL_MODULE_TAGS := optional
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := libopenHEVC
LOCAL_MODULE_SUFFIX := .so
LOCAL_MULTILIB := 32
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_PATH_32 := $(TARGET_OUT)/lib/
LOCAL_SRC_FILES_arm := $(LOCAL_MODULE)$(LOCAL_MODULE_SUFFIX)
LOCAL_MODULE_TAGS := optional
include $(BUILD_PREBUILT)
