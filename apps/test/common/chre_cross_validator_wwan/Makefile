#
# CHRE Cross Validator Nanoapp Makefile
#

# Environment Checks ###########################################################
ifeq ($(CHRE_PREFIX),)
  ifneq ($(ANDROID_BUILD_TOP),)
    CHRE_PREFIX = $(ANDROID_BUILD_TOP)/system/chre
  else
    $(error "You must run 'lunch' to setup ANDROID_BUILD_TOP, or explicitly \
    define the CHRE_PREFIX environment variable to point to the CHRE root \
    directory.")
  endif
endif

# Nanoapp Configuration ########################################################

NANOAPP_NAME = chre_cross_validator_wwan
NANOAPP_ID = 0x476f6f6754000011
NANOAPP_NAME_STRING = \"Chre\ Cross\ Validator\ WWAN\"
NANOAPP_VERSION = 0x00000001

NANOAPP_PATH = $(CHRE_PREFIX)/apps/test/common/chre_cross_validator_wwan
TEST_SHARED_PATH = $(CHRE_PREFIX)/apps/test/common/shared


# Protobuf Sources #############################################################

NANOPB_EXTENSION = nanopb

NANOPB_SRCS += $(NANOAPP_PATH)/../proto/chre_cross_validation_wwan.proto
NANOPB_SRCS += $(NANOAPP_PATH)/../proto/chre_test_common.proto
NANOPB_INCLUDES = $(NANOAPP_PATH)/../proto

# Source Code ##################################################################

COMMON_SRCS += $(NANOAPP_PATH)/src/chre_cross_validator_wwan.cc
COMMON_SRCS += $(NANOAPP_PATH)/src/chre_cross_validator_wwan_manager.cc
COMMON_SRCS += $(TEST_SHARED_PATH)/src/send_message.cc
COMMON_SRCS += $(CHRE_PREFIX)/util/nanoapp/callbacks.cc

# Compiler Flags ###############################################################

# Defines
COMMON_CFLAGS += -DNANOAPP_MINIMUM_LOG_LEVEL=CHRE_LOG_LEVEL_INFO
COMMON_CFLAGS += -DLOG_TAG=\"[ChreCrossValidatorWwan]\"
COMMON_CFLAGS += -DCHRE_ASSERTIONS_ENABLED

# Includes
COMMON_CFLAGS += -I$(NANOAPP_PATH)/inc
COMMON_CFLAGS += -I$(TEST_SHARED_PATH)/inc

# Permission declarations ######################################################

CHRE_NANOAPP_USES_WWAN = true

# Makefile Includes ############################################################

include $(CHRE_PREFIX)/build/nanoapp/app.mk
