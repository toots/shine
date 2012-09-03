LOCAL_PATH := $(call my-dir)/..

include $(CLEAR_VARS)

LOCAL_CFLAGS    := -Iandroid/include
LOCAL_LDLIBS    := -Landroid/lib -lshine

LOCAL_MODULE    := shineenc
LOCAL_SRC_FILES := src/bin/main.c src/bin/wave.c

include $(BUILD_EXECUTABLE)

