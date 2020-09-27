#
# Std library overrides Makefile
#

ifeq ($(CHRE_STD_OVERRIDES_ALLOWED),true)
# Common Compiler Flags ########################################################
COMMON_CFLAGS += -I$(CHRE_PREFIX)/std_overrides/include

# Common Source Files ##########################################################
COMMON_SRCS += $(CHRE_PREFIX)/std_overrides/stdlib_wrapper.cc

# Platform specific flags ######################################################
AOC_CFLAGS += -I$(CHRE_PREFIX)/platform/aoc/include/std_overrides/include
endif
