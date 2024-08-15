/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include <cmath>
#include <cstdint>

#include "gtest/gtest.h"

#include "chre/platform/linux/system_time.h"
#include "chre/platform/system_time.h"
#include "chre/util/throttle.h"
#include "chre/util/time.h"

using ::chre::Milliseconds;
using ::chre::Nanoseconds;
using ::chre::Seconds;
using ::chre::SystemTime;
using ::chre::platform_linux::SystemTimeOverride;

TEST(Throttle, ThrottlesActionLessThanOneInterval) {
  uint32_t count = 0;
  constexpr uint32_t kMaxCount = 10;
  constexpr uint64_t kCallCount = 1000;
  constexpr Seconds kInterval(1);
  static_assert(kCallCount < kInterval.toRawNanoseconds());

  for (uint64_t i = 0; i < kCallCount; ++i) {
    SystemTimeOverride override(i);
    CHRE_THROTTLE(++count, kInterval, kMaxCount,
                  SystemTime::getMonotonicTime());
  }

  EXPECT_EQ(count, kMaxCount);
}

TEST(Throttle, ThrottlesActionMoreThanOneInterval) {
  uint32_t count = 0;
  constexpr uint32_t kMaxCount = 10;
  constexpr uint64_t kCallCount = 1000;
  constexpr Nanoseconds kInterval(100);
  static_assert(kCallCount > kInterval.toRawNanoseconds());

  for (uint64_t i = 0; i < kCallCount; ++i) {
    SystemTimeOverride override(i);
    CHRE_THROTTLE(++count, kInterval, kMaxCount,
                  SystemTime::getMonotonicTime());
  }

  EXPECT_EQ(count, (kCallCount / kInterval.toRawNanoseconds()) * kMaxCount);
}
