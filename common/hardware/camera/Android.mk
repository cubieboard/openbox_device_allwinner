# Copyright (C) 2011 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_CFLAGS += -fno-short-enums -DQEMU_HARDWARE
LOCAL_SHARED_LIBRARIES:= \
    libbinder \
    libutils \
    libcutils \
    libcamera_client \
    libui \

# JPEG conversion libraries and includes.
LOCAL_SHARED_LIBRARIES += \
	libjpeg \
	libskia \
	libandroid_runtime \
	libCedarX \
	libcedarxosal \
	libfacedetection
	
LOCAL_C_INCLUDES += 								\
	external/jpeg 									\
	external/skia/include/core/ 					\
	frameworks/base/core/jni/android/graphics 		\
	frameworks/base/include/media/stagefright/openmax \
	$(TOP)/frameworks/base/media/CedarX-Projects/CedarX/include/include_camera \
	hardware/libhardware/include/hardware \
	external/neven/FaceRecEm/common/src/b_FDSDK     \
	$(TARGET_HARDWARE_INCLUDE)
					

LOCAL_SRC_FILES := \
	CameraHal.cpp \
	HALCameraFactory.cpp \
	CameraHardware.cpp \
	V4L2Camera.cpp \
	CameraHardwareDevice.cpp \
	V4L2CameraDevice.cpp \
	Converters.cpp \
	PreviewWindow.cpp \
	CallbackNotifier.cpp \
	JpegCompressor.cpp \
	CCameraConfig.cpp \
	OSAL_Mutex.c \
	OSAL_Queue.c
	

ifneq ($(filter nuclear%,$(TARGET_DEVICE)),)
LOCAL_CFLAGS += -D__SUN5I__
endif

ifneq ($(filter crane%,$(TARGET_DEVICE)),)
LOCAL_CFLAGS += -D__SUN4I__
endif

LOCAL_MODULE := camera.$(TARGET_BOARD_PLATFORM)

LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)

#-------------------------------------------------------------------------------
# 
#-------------------------------------------------------------------------------
# include $(CLEAR_VARS)
# LOCAL_C_INCLUDES:= \
# 	external/neven/FaceRecEm/common/src/b_FDSDK     \
# 	$(TOP)/frameworks/base/media/CedarX-Projects/CedarX/include/include_camera \
# 	$(TARGET_HARDWARE_INCLUDE)
# 	
# LOCAL_SRC_FILES:= \
# 	FaceDetection.cpp \
# 	
# LOCAL_MODULE := libfacedetection
# 
# LOCAL_SHARED_LIBRARIES:= libcutils libutils libtinyalsa liblog libFFTEm
# 
# LOCAL_MODULE_TAGS := optional
# 
# include $(BUILD_SHARED_LIBRARY)

