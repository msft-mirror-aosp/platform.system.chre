#
# Copyright 2019 The Android Open Source Project
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
#

LOCAL_PATH := $(call my-dir)

# If the daemon is using USF, but USF isn't enabled, disable the daemon.
# TODO (b/157659611): Remove when USF is available on partner with a prebuilt.
ifeq ($(CHRE_DAEMON_USES_USF),true)
  ifneq ($(USF_ENABLED),true)
    CHRE_DAEMON_ENABLED := false
  endif
endif

# Don't build the daemon for targets that don't contain a vendor image as
# libsdsprpc and libadsprpc are provided by vendor code
ifeq ($(BUILDING_VENDOR_IMAGE),true)

ifeq ($(CHRE_DAEMON_ENABLED),true)

include $(CLEAR_VARS)

# CHRE AP-side daemon
# NOTE: This can't be converted to a blueprint file until libsdsprpc /
# libadsprpc is converted as blueprint targets can't depend on targets exposed
# by makefiles
LOCAL_MODULE := chre
LOCAL_MODULE_OWNER := google
LOCAL_MODULE_TAGS := optional
LOCAL_VENDOR_MODULE := true
LOCAL_INIT_RC := chre_daemon.rc

LOCAL_CPP_EXTENSION := .cc
LOCAL_CFLAGS += -Wall -Werror -Wextra

# Enable the LPMA feature for devices that support audio
ifeq ($(CHRE_DAEMON_LPMA_ENABLED),true)
LOCAL_CFLAGS += -DCHRE_DAEMON_LPMA_ENABLED
endif

ifeq ($(CHRE_DAEMON_LOAD_INTO_SENSORSPD),true)
LOCAL_CFLAGS += -DCHRE_DAEMON_LOAD_INTO_SENSORSPD
endif

# Disable Tokenized Logging
CHRE_USE_TOKENIZED_LOGGING := false

LOCAL_SRC_FILES := \
    host/common/daemon_base.cc \
    host/common/fragmented_load_transaction.cc \
    host/common/host_protocol_host.cc \
    host/common/log_message_parser_base.cc \
    host/common/socket_server.cc \
    host/common/st_hal_lpma_handler.cc \
    platform/shared/host_protocol_common.cc

MSM_DAEMON_SRC_FILES := \
    host/msm/daemon/fastrpc_daemon.cc \
    host/msm/daemon/main.cc \
    host/msm/daemon/generated/chre_slpi_stub.c

USF_DAEMON_SRC_FILES := \
    host/usf_daemon/usf_daemon.cc \
    host/usf_daemon/main.cc

LOCAL_C_INCLUDES := \
    system/chre/external/flatbuffers/include \
    system/chre/host/common/include \
    system/chre/platform/shared/include \
    system/chre/util/include \
    system/core/base/include \
    system/core/libcutils/include \
    system/core/liblog/include \
    system/core/libutils/include

MSM_DAEMON_INCLUDES := \
    external/fastrpc/inc \
    system/chre/platform/slpi/include \
    system/chre/host/msm/daemon

USF_DAEMON_INCLUDES := \
    system/chre/host/usf_daemon \
    vendor/google/sensors/usf/core/include \
    vendor/google/sensors/usf/pal/android/include \
    vendor/google/sensors/usf/pal/include \
    vendor/google/sensors/usf/core/fbs

LOCAL_SHARED_LIBRARIES := \
    libjsoncpp \
    libutils \
    libcutils \
    liblog \
    libhidlbase \
    libbase \
    android.hardware.soundtrigger@2.0 \
    libpower

USF_DAEMON_SHARED_LIBRARIES := libusf

ifeq ($(CHRE_DAEMON_USES_USF),true)
LOCAL_C_INCLUDES += $(USF_DAEMON_INCLUDES)
LOCAL_SRC_FILES += $(USF_DAEMON_SRC_FILES)
LOCAL_SHARED_LIBRARIES += $(USF_DAEMON_SHARED_LIBRARIES)
else
LOCAL_SRC_FILES += $(MSM_DAEMON_SRC_FILES)
LOCAL_C_INCLUDES += $(MSM_DAEMON_INCLUDES)
ifeq ($(CHRE_DAEMON_USE_SDSPRPC),true)
LOCAL_SHARED_LIBRARIES += libsdsprpc
else
LOCAL_SHARED_LIBRARIES += libadsprpc
endif
endif

# Enable tokenized logging
ifeq ($(CHRE_USE_TOKENIZED_LOGGING),true)
LOCAL_CFLAGS += -DCHRE_USE_TOKENIZED_LOGGING
PIGWEED_TOKENIZER_DIR = vendor/google_contexthub/chre/external/pigweed
PIGWEED_TOKENIZER_DIR_RELPATH = ../../$(PIGWEED_TOKENIZER_DIR)
LOCAL_CFLAGS += -I$(PIGWEED_TOKENIZER_DIR)/pw_polyfill/public
LOCAL_CFLAGS += -I$(PIGWEED_TOKENIZER_DIR)/pw_polyfill/public_overrides
LOCAL_CFLAGS += -I$(PIGWEED_TOKENIZER_DIR)/pw_polyfill/standard_library_public
LOCAL_CFLAGS += -I$(PIGWEED_TOKENIZER_DIR)/pw_preprocessor/public
LOCAL_CFLAGS += -I$(PIGWEED_TOKENIZER_DIR)/pw_tokenizer/public
LOCAL_CFLAGS += -I$(PIGWEED_TOKENIZER_DIR)/pw_varint/public
LOCAL_CFLAGS += -I$(PIGWEED_TOKENIZER_DIR)/pw_span/public

LOCAL_SRC_FILES += $(PIGWEED_TOKENIZER_DIR_RELPATH)/pw_tokenizer/detokenize.cc
LOCAL_SRC_FILES += $(PIGWEED_TOKENIZER_DIR_RELPATH)/pw_tokenizer/decode.cc
LOCAL_SRC_FILES += $(PIGWEED_TOKENIZER_DIR_RELPATH)/pw_varint/varint.cc
endif

ifeq ($(CHRE_DAEMON_LPMA_ENABLED),true)
LOCAL_SHARED_LIBRARIES += android.hardware.soundtrigger@2.0
LOCAL_SHARED_LIBRARIES += libpower
endif

include $(BUILD_EXECUTABLE)

endif
endif
