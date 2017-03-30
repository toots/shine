LOCAL_PATH := $(call my-dir)/..

include $(CLEAR_VARS)

LOCAL_ARM_MODE  := arm
LOCAL_MODULE    := shine
LOCAL_SRC_FILES := src/lib/bitstream.c src/lib/huffman.c \
                   src/lib/l3bitstream.c src/lib/l3loop.c src/lib/l3mdct.c \
                   src/lib/l3subband.c src/lib/layer3.c src/lib/reservoir.c \
                   src/lib/tables.c
LOCAL_LDLIBS    := -lm

include $(BUILD_SHARED_LIBRARY)
