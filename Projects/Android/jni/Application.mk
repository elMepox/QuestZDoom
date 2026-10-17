APP_PLATFORM := android-29

APP_CFLAGS += -Wl,--no-undefined, -march=armv8+crc

APPLICATIONMK_PATH = $(call my-dir)
NDK_MODULE_PATH := $(APPLICATIONMK_PATH)/../..

TOP_DIR			:= $(APPLICATIONMK_PATH)
SUPPORT_LIBS	:= $(APPLICATIONMK_PATH)/SupportLibs
GZDOOM_TOP_PATH := $(APPLICATIONMK_PATH)/gzdoom-g3.3mgw_mobile

APP_ALLOW_MISSING_DEPS=true

APP_SHORT_COMMANDS :=true

APP_MODULES := qzdoom
APP_STL := c++_shared



# gzdoom's mobile/Android_src.mk lists this include dir as "src/posix\nosdl" (a Windows
# path separator), which does not resolve on Linux/macOS hosts, so glvideo.h is not found.
# Add the correct path here until that is fixed in the engine submodule.
APP_CPPFLAGS += -I$(GZDOOM_TOP_PATH)/src/posix/nosdl
