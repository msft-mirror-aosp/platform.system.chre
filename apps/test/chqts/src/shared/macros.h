/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _GTS_NANOAPPS_SHARED_MACROS_H_
#define _GTS_NANOAPPS_SHARED_MACROS_H_

#include "send_message.h"

/**
 * A helper macro to perform an assertion in tests that leverage
 * nanoapp_testing::sendFailureToHost.
 *
 * This macro can be used in the following ways:
 * 1. EXPECT_FAIL(const char *message)
 * 2. EXPECT_FAIL(const char *message, uint32_t *value)
 *
 * In usage (2), an integer value will be encoded at the end of the message
 * string, and will be forwarded to the test host through sendFailureToHost.
 *
 * Note that this macro can only be used in functions that return a `void`.
 */
#define EXPECT_FAIL(...)                             \
  do {                                               \
    nanoapp_testing::sendFailureToHost(__VA_ARGS__); \
    nanoapp_testing::logFailureMessage(__VA_ARGS__); \
    return;                                          \
  } while (0)

/**
 * An additional helper macro that can be used to print a uint8 instead of
 * uint32 in usage (2) of EXPECT_FAIL.
 *
 * TODO(b/396134028): Consolidate this with the EXPECT_FAIL macro.
 */
#define EXPECT_FAIL_UINT8(message, value)           \
  static_assert(sizeof(value) <= sizeof(uint32_t)); \
  do {                                              \
    uint32_t valueU32 = value;                      \
    EXPECT_FAIL(message, &valueU32);                \
  } while (0)

/**
 * Asserts the two provided values are equal. If the assertion fails, then a
 * fatal failure occurs.
 */
#define EXPECT_EQ(val1, val2, failureMessage) \
  if ((val1) != (val2)) EXPECT_FAIL(failureMessage)

/**
 * Asserts the two provided values are not equal. If the assertion fails, then
 * a fatal failure occurs.
 */
#define EXPECT_NE(val1, val2, failureMessage) \
  if ((val1) == (val2)) EXPECT_FAIL(failureMessage)

/**
 * Asserts the given value is greater than or equal to value of lower. If the
 * value fails this assertion, then a fatal failure occurs.
 */
#define EXPECT_GE(value, lower, failureMessage) \
  if ((value) < (lower)) EXPECT_FAIL(failureMessage)

/**
 * Asserts the given value is less than or equal to value of upper. If the value
 * fails this assertion, then a fatal failure occurs.
 */
#define EXPECT_LE(value, upper, failureMessage) \
  if ((value) > (upper)) EXPECT_FAIL(failureMessage)

/**
 * Asserts the given value is less than the value of upper. If the value fails
 * this assertion, then a fatal failure occurs.
 */
#define EXPECT_LT(value, upper, failureMessage) \
  if ((value) >= (upper)) EXPECT_FAIL(failureMessage)

/**
 * Asserts the given value is within the range defined by lower and upper
 * (inclusive). If the value is outside the range, then a fatal failure occurs.
 */
#define EXPECT_IN_RANGE(value, lower, upper, failureMessage) \
  EXPECT_GE((value), (lower), failureMessage);               \
  EXPECT_LE((value), (upper), failureMessage)

#endif  // _GTS_NANOAPPS_SHARED_MACROS_H_