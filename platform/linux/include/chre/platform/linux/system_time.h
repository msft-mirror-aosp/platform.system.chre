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

#pragma once

#include "chre/util/time.h"

namespace chre::platform_linux {

/**
 * Override the value returned by SystemTime::getMonotonicTime(). Useful for
 * testing.
 *
 * @param ns The value that should be returned by SystemTime::getMonotonicTime()
 */
void overrideMonotonicTime(Nanoseconds ns);

/**
 * Reset SystemTime::getMonotonicTime() to its default behavior of returning a
 * real time reference.
 */
void clearMonotonicTimeOverride(void);

//! RAII way to override the time and then reset
class SystemTimeOverride {
 public:
  explicit SystemTimeOverride(Nanoseconds ns) {
    update(ns);
  }
  explicit SystemTimeOverride(uint64_t ns) {
    update(ns);
  }
  ~SystemTimeOverride() {
    clearMonotonicTimeOverride();
  }

  void update(Nanoseconds ns) {
    overrideMonotonicTime(ns);
  }
  void update(uint64_t ns) {
    update(Nanoseconds(ns));
  }
};

}  // namespace chre::platform_linux
