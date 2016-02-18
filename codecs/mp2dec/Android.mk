LOCAL_PATH := $(call my-dir)
ifeq ($(TARGET_ARCH),arm)
include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES :=  \
                    src/libmpg123/synth_neon_s32.S \
                    src/libmpg123/compat.c \
                    src/libmpg123/icy.c \
                    src/libmpg123/synth_8bit.c\
                    src/libmpg123/synth_real.c \
                    src/libmpg123/dither.c  \
                    src/libmpg123/id3.c   \
                    src/libmpg123/synth_s32.c \
                    src/libmpg123/index.c \
                    src/libmpg123/synth_arm_accurate.S  \
                    src/libmpg123/equalizer.c \
                    src/libmpg123/synth_arm.S     \
                    src/libmpg123/feature.c \
                    src/libmpg123/synth.c    \
                    src/libmpg123/dct64.c   \
                    src/libmpg123/format.c \
                    src/libmpg123/ntom.c   \
                    src/libmpg123/tabinit.c \
                    src/libmpg123/frame.c  \
                    src/libmpg123/optimize.c \
                    src/libmpg123/synth_stereo_neon_accurate.S \
                    src/libmpg123/layer1.c \
                    src/libmpg123/parse.c  \
                    src/libmpg123/synth_stereo_neon_float.S     \
                    src/libmpg123/dct64_neon_float.S \
                    src/libmpg123/layer2.c   \
                    src/libmpg123/synth_stereo_neon.S \
                    src/libmpg123/dct64_neon.S \
                    src/libmpg123/layer3.c \
                    src/libmpg123/synth_stereo_neon_s32.S\
                    src/libmpg123/lfs_alias.c   \
                    src/libmpg123/readers.c     \
                    src/libmpg123/synth_neon_accurate.S \
                    src/libmpg123/lfs_wrap.c \
                    src/libmpg123/synth_neon_float.S     \
                    src/libmpg123/icy2utf8.c  \
                    src/libmpg123/libmpg123.c \
                    src/libmpg123/stringbuf.c         \
                    src/libmpg123/synth_neon.S

# for logging
LOCAL_LDLIBS += -llog
# for native asset manager

LOCAL_CFLAGS := -DHAVE_NEON=1 -DHAVE_CONFIG -DOPT_NEON -DREAL_IS_FLOAT -mfloat-abi=softfp -mfpu=neon

LOCAL_C_INCLUDES += \
         $(LOCAL_PATH)/src \
         $(LOCAL_PATH)/src/libmpg123

LOCAL_MODULE := libstagefright_mp2dec

LOCAL_ARM_MODE := arm

include $(BUILD_STATIC_LIBRARY)

#####################################################################################################

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
        SoftMP2.cpp

LOCAL_C_INCLUDES := \
        frameworks/av/media/libstagefright/include \
        frameworks/native/include/media/openmax \
        $(LOCAL_PATH)/src

LOCAL_C_INCLUDES += \
        $(LOCAL_PATH)/src/libmpg123 \
        $(LOCAL_PATH)/include


LOCAL_SHARED_LIBRARIES := \
        libstagefright libstagefright_omx libstagefright_foundation libutils liblog

LOCAL_STATIC_LIBRARIES := \
        libstagefright_mp2dec

LOCAL_MODULE := libstagefright_soft_mp2dec
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
endif
