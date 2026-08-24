# Project Name
TARGET = ZenTouchPod

# Sources
CPP_SOURCES = \
src/main.cpp \
src/zen_voice.cpp \
src/zen_fx.cpp \
src/zen_ui.cpp \
src/zen_looper.cpp

C_INCLUDES += -Isrc

# Library Locations
LIBDAISY_DIR = libDaisy
DAISYSP_DIR = DaisySP

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
