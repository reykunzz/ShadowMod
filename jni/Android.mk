LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE    := ShadowMod
LOCAL_SRC_FILES := main.cpp mod/logger.cpp
LOCAL_CFLAGS += -O2 -DNDEBUG -DAML32
LOCAL_CPPFLAGS  += -std=c++17 -fexceptions
LOCAL_LDLIBS    += -llog -ldl

include $(BUILD_SHARED_LIBRARY)
