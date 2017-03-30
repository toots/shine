LOCAL_PATH := $(call my-dir)/..

include $(CLEAR_VARS)

LOCAL_ARM_MODE  := arm

LOCAL_CFLAGS    := -I$(LOCAL_PATH)/src/lib

LOCAL_MODULE    := shineenc
LOCAL_SRC_FILES := src/bin/main.c src/bin/wave.c \
                   src/lib/bitstream.c src/lib/huffman.c \
                   src/lib/l3bitstream.c src/lib/l3loop.c src/lib/l3mdct.c \
                   src/lib/l3subband.c src/lib/layer3.c src/lib/reservoir.c \
                   src/lib/tables.c

include $(BUILD_EXECUTABLE)

