LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE    := ShadowMod
LOCAL_SRC_FILES := main.cpp
LOCAL_CFLAGS    += -O2 -DNDEBUG
LOCAL_CPPFLAGS  += -std=c++17 -fexceptions
LOCAL_LDLIBS    += -llog -ldl

include $(BUILD_SHARED_LIBRARY)
