#####################################################
# h265 soft decoder based on openHEVC
####################################################

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_PREBUILT_LIBS := libstagefright_soft_amh265dec.so \
              libopenHEVC.so

LOCAL_MODULE_TAGS := optional

include $(BUILD_MULTI_PREBUILT) 
