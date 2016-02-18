#####################################################
# amlogic soft decoder based on ffmpeg
####################################################

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_PREBUILT_LIBS := libstagefright_soft_amsoftadec.so

LOCAL_MODULE_TAGS := optional

include $(BUILD_MULTI_PREBUILT)
