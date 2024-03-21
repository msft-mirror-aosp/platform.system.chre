#
# Makefile for Pigweed's function module
#

# Environment Checks
ifeq ($(ANDROID_BUILD_TOP),)
$(error "You should supply an ANDROID_BUILD_TOP environment variable \
         containing a path to the Android source tree. This is typically \
         provided by initializing the Android build environment.")
endif

# Location of Pigweed
PIGWEED_DIR = $(ANDROID_BUILD_TOP)/external/pigweed
PIGWEED_CHRE_DIR = $(ANDROID_BUILD_TOP)/system/chre/external/pigweed

# Pigweed include paths
COMMON_CFLAGS += -I$(PIGWEED_CHRE_DIR)/pw_log_nanoapp/public_overrides
COMMON_CFLAGS += -I$(PIGWEED_CHRE_DIR)/pw_assert_nanoapp/public_overrides
COMMON_CFLAGS += -I$(PIGWEED_DIR)/pw_assert/public
COMMON_CFLAGS += -I$(PIGWEED_DIR)/pw_log/public
COMMON_CFLAGS += -I$(PIGWEED_DIR)/pw_function/public
COMMON_CFLAGS += -I$(PIGWEED_DIR)/pw_polyfill/public
COMMON_CFLAGS += -I$(PIGWEED_DIR)/pw_preprocessor/public
COMMON_CFLAGS += -I$(PIGWEED_DIR)/third_party/fuchsia/repo/sdk/lib/fit/include
COMMON_CFLAGS += -I$(PIGWEED_DIR)/third_party/fuchsia/repo/sdk/lib/stdcompat/include
