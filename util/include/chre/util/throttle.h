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

#ifndef CHRE_UTIL_THROTTLE_H_
#define CHRE_UTIL_THROTTLE_H_

#include "chre/util/optional.h"

using ::chre::Optional;

/**
 * Throttles an action to a given interval and maximum number of times.
 * The action will be called at most maxCount in every interval.
 *
 * @param action The action to throttle
 * @param interval The interval between actions
 * @param maxCount The maximum number of times to call the action
 * @param getTime A function to get the current time
 */
#define CHRE_THROTTLE(action, interval, maxCount, getTime) \
  do {                                                     \
    static uint32_t _count = 0;                            \
    static Optional<Nanoseconds> _lastCallTime;            \
    Nanoseconds _now = getTime;                            \
    if (!_lastCallTime.has_value() ||                      \
        _now - _lastCallTime.value() >= interval) {        \
      _count = 0;                                          \
      _lastCallTime = _now;                                \
    }                                                      \
    if (++_count > maxCount) {                             \
      break;                                               \
    }                                                      \
    action;                                                \
  } while (0)

#endif  // CHRE_UTIL_THROTTLE_H_
