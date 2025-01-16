#
# Util Makefile
#

# Location of various Pigweed modules  #########################################

PIGWEED_DIR = $(ANDROID_BUILD_TOP)/external/pigweed
PIGWEED_CHRE_DIR = $(ANDROID_BUILD_TOP)/system/chre/external/pigweed

# Common Compiler Flags ########################################################

# Include paths.
COMMON_CFLAGS += -I$(CHRE_PREFIX)/util/include

# Pigweed ######################################################################

COMMON_CFLAGS += -I$(PIGWEED_CHRE_DIR)/pw_log_nanoapp/public_overrides
COMMON_CFLAGS += -I$(PIGWEED_CHRE_DIR)/pw_assert_nanoapp/public_overrides
COMMON_CFLAGS += -I$(PIGWEED_DIR)/pw_allocator/public
COMMON_CFLAGS += -I$(PIGWEED_DIR)/pw_assert/public
COMMON_CFLAGS += -I$(PIGWEED_DIR)/pw_bytes/public
COMMON_CFLAGS += -I$(PIGWEED_DIR)/pw_containers/public
COMMON_CFLAGS += -I$(PIGWEED_DIR)/pw_function/public
COMMON_CFLAGS += -I$(PIGWEED_DIR)/pw_log/public
COMMON_CFLAGS += -I$(PIGWEED_DIR)/pw_polyfill/public
COMMON_CFLAGS += -I$(PIGWEED_DIR)/pw_preprocessor/public
COMMON_CFLAGS += -I$(PIGWEED_DIR)/pw_result/public
COMMON_CFLAGS += -I$(PIGWEED_DIR)/pw_span/public
COMMON_CFLAGS += -I$(PIGWEED_DIR)/pw_status/public
COMMON_CFLAGS += -I$(PIGWEED_DIR)/third_party/fuchsia/repo/sdk/lib/fit/include
COMMON_CFLAGS += -I$(PIGWEED_DIR)/third_party/fuchsia/repo/sdk/lib/stdcompat/include

COMMON_SRCS += $(PIGWEED_DIR)/pw_allocator/allocator.cc
COMMON_SRCS += $(PIGWEED_DIR)/pw_allocator/unique_ptr.cc
COMMON_SRCS += $(PIGWEED_DIR)/pw_containers/intrusive_item.cc

# Common Source Files ##########################################################

COMMON_SRCS += $(CHRE_PREFIX)/util/buffer_base.cc
COMMON_SRCS += $(CHRE_PREFIX)/util/duplicate_message_detector.cc
COMMON_SRCS += $(CHRE_PREFIX)/util/dynamic_vector_base.cc
COMMON_SRCS += $(CHRE_PREFIX)/util/hash.cc
COMMON_SRCS += $(CHRE_PREFIX)/util/intrusive_list_base.cc
COMMON_SRCS += $(CHRE_PREFIX)/util/nanoapp/audio.cc
COMMON_SRCS += $(CHRE_PREFIX)/util/nanoapp/ble.cc
COMMON_SRCS += $(CHRE_PREFIX)/util/nanoapp/callbacks.cc
COMMON_SRCS += $(CHRE_PREFIX)/util/nanoapp/debug.cc
COMMON_SRCS += $(CHRE_PREFIX)/util/nanoapp/string.cc
COMMON_SRCS += $(CHRE_PREFIX)/util/nanoapp/wifi.cc
COMMON_SRCS += $(CHRE_PREFIX)/util/system/ble_util.cc
COMMON_SRCS += $(CHRE_PREFIX)/util/system/event_callbacks.cc
COMMON_SRCS += $(CHRE_PREFIX)/util/system/debug_dump.cc
COMMON_SRCS += $(CHRE_PREFIX)/util/system/message_router.cc
COMMON_SRCS += $(CHRE_PREFIX)/util/system/service_helpers.cc

# GoogleTest Source Files ######################################################

GOOGLETEST_SRCS += $(CHRE_PREFIX)/util/tests/array_queue_test.cc
GOOGLETEST_SRCS += $(CHRE_PREFIX)/util/tests/atomic_spsc_queue_test.cc
GOOGLETEST_SRCS += $(CHRE_PREFIX)/util/tests/blocking_queue_test.cc
GOOGLETEST_SRCS += $(CHRE_PREFIX)/util/tests/buffer_test.cc
GOOGLETEST_SRCS += $(CHRE_PREFIX)/util/tests/copyable_fixed_size_vector_test.cc
GOOGLETEST_SRCS += $(CHRE_PREFIX)/util/tests/debug_dump_test.cc
GOOGLETEST_SRCS += $(CHRE_PREFIX)/util/tests/duplicate_message_detector_test.cc
GOOGLETEST_SRCS += $(CHRE_PREFIX)/util/tests/dynamic_vector_test.cc
GOOGLETEST_SRCS += $(CHRE_PREFIX)/util/tests/fragmentation_manager_test.cc
GOOGLETEST_SRCS += $(CHRE_PREFIX)/util/tests/fixed_size_vector_test.cc
GOOGLETEST_SRCS += $(CHRE_PREFIX)/util/tests/heap_test.cc
GOOGLETEST_SRCS += $(CHRE_PREFIX)/util/tests/intrusive_list_test.cc
GOOGLETEST_SRCS += $(CHRE_PREFIX)/util/tests/lock_guard_test.cc
GOOGLETEST_SRCS += $(CHRE_PREFIX)/util/tests/memory_pool_test.cc
GOOGLETEST_SRCS += $(CHRE_PREFIX)/util/tests/optional_test.cc
GOOGLETEST_SRCS += $(CHRE_PREFIX)/util/tests/priority_queue_test.cc
GOOGLETEST_SRCS += $(CHRE_PREFIX)/util/tests/raw_storage_test.cc
GOOGLETEST_SRCS += $(CHRE_PREFIX)/util/tests/ref_base_test.cc
GOOGLETEST_SRCS += $(CHRE_PREFIX)/util/tests/segmented_queue_test.cc
GOOGLETEST_SRCS += $(CHRE_PREFIX)/util/tests/shared_ptr_test.cc
GOOGLETEST_SRCS += $(CHRE_PREFIX)/util/tests/singleton_test.cc
GOOGLETEST_SRCS += $(CHRE_PREFIX)/util/tests/stats_container_test.cc
GOOGLETEST_SRCS += $(CHRE_PREFIX)/util/tests/synchronized_expandable_memory_pool_test.cc
GOOGLETEST_SRCS += $(CHRE_PREFIX)/util/tests/synchronized_memory_pool_test.cc
GOOGLETEST_SRCS += $(CHRE_PREFIX)/util/tests/time_test.cc
GOOGLETEST_SRCS += $(CHRE_PREFIX)/util/tests/unique_ptr_test.cc

# Pigweed Source Files #########################################################

PIGWEED_UTIL_SRCS += $(CHRE_PREFIX)/util/pigweed/chre_channel_output.cc
