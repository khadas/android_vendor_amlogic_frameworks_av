LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

  LOCAL_SRC_FILES := \
			libfaad/bits.c \
			libfaad/cfft.c \
			libfaad/decoder.c \
			libfaad/drc.c \
		        libfaad/drm_dec.c \
			libfaad/error.c \
			libfaad/filtbank.c \
		  	libfaad/ic_predict.c \
			libfaad/is.c \
			libfaad/lt_predict.c \
			libfaad/mdct.c \
			libfaad/mp4.c \
			libfaad/ms.c \
			libfaad/output.c \
			libfaad/pns.c \
		  	libfaad/ps_dec.c \
			libfaad/ps_syntax.c \
		  	libfaad/pulse.c \
			libfaad/specrec.c \
			libfaad/syntax.c \
			libfaad/tns.c \
			libfaad/hcr.c \
			libfaad/huffman.c \
		  	libfaad/rvlc.c \
			libfaad/ssr.c \
			libfaad/ssr_fb.c \
			libfaad/ssr_ipqf.c \
			libfaad/common.c \
		  	libfaad/sbr_dct.c \
			libfaad/sbr_e_nf.c \
			libfaad/sbr_fbt.c \
			libfaad/sbr_hfadj.c \
			libfaad/sbr_hfgen.c \
		  	libfaad/sbr_huff.c \
			libfaad/sbr_qmf.c \
			libfaad/sbr_syntax.c \
			libfaad/sbr_tf_grid.c \
			libfaad/sbr_dec.c 

  LOCAL_C_INCLUDES := \
	 $(LOCAL_PATH)/libfaad \
  	 $(LOCAL_PATH)/libfaad/codebook \
  	 $(LOCAL_PATH)/include

  LOCAL_CFLAGS := \
        -DOSCL_UNUSED_ARG=

  LOCAL_MODULE := libstagefright_adifdec

  LOCAL_ARM_MODE := arm

  include $(BUILD_STATIC_LIBRARY)

  ################################################################################

  include $(CLEAR_VARS)

  LOCAL_SRC_FILES := \
        SoftADIF.cpp

  LOCAL_C_INCLUDES := \
        frameworks/av/media/libstagefright/include \
        frameworks/native/include/media/openmax \
	$(LOCAL_PATH)/libfaad \
  	$(LOCAL_PATH)/libfaad/codebook \
  	$(LOCAL_PATH)/include

  LOCAL_STATIC_LIBRARIES := \
	 libstagefright_adifdec

  LOCAL_SHARED_LIBRARIES := \
        libstagefright libstagefright_omx libstagefright_foundation libutils liblog

  LOCAL_MODULE := libstagefright_soft_adifdec
  LOCAL_MODULE_TAGS := optional

  include $(BUILD_SHARED_LIBRARY)

  ################################################################################

  include $(CLEAR_VARS)

  LOCAL_SRC_FILES := \
        SoftLATM.cpp

  LOCAL_C_INCLUDES := \
        frameworks/av/media/libstagefright/include \
        frameworks/native/include/media/openmax \
	$(LOCAL_PATH)/libfaad \
  	$(LOCAL_PATH)/libfaad/codebook \
  	$(LOCAL_PATH)/include

  LOCAL_STATIC_LIBRARIES := \
	 libstagefright_adifdec

  LOCAL_SHARED_LIBRARIES := \
        libstagefright libstagefright_omx libstagefright_foundation libutils liblog

  LOCAL_MODULE := libstagefright_soft_latmdec
  LOCAL_MODULE_TAGS := optional

  include $(BUILD_SHARED_LIBRARY)

  ################################################################################

  include $(CLEAR_VARS)

  LOCAL_SRC_FILES := \
        SoftADTS.cpp

  LOCAL_C_INCLUDES := \
        frameworks/av/media/libstagefright/include \
        frameworks/native/include/media/openmax \
	$(LOCAL_PATH)/libfaad \
  	$(LOCAL_PATH)/libfaad/codebook \
  	$(LOCAL_PATH)/include

  LOCAL_STATIC_LIBRARIES := \
	 libstagefright_adifdec

  LOCAL_SHARED_LIBRARIES := \
        libstagefright libstagefright_omx libstagefright_foundation libutils liblog

  LOCAL_MODULE := libstagefright_soft_adtsdec
  LOCAL_MODULE_TAGS := optional

  include $(BUILD_SHARED_LIBRARY)
