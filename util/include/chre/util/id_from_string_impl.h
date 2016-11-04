/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "chre/util/id_from_string.h"

#ifndef CHRE_UTIL_ID_FROM_STRING_IMPL_H_
#define CHRE_UTIL_ID_FROM_STRING_IMPL_H_

constexpr uint64_t createIdFromString(const char str[5]) {
  return (static_cast<uint64_t>(str[0]) << 56)
      | (static_cast<uint64_t>(str[1]) << 48)
      | (static_cast<uint64_t>(str[2]) << 40)
      | (static_cast<uint64_t>(str[3]) << 32)
      | (static_cast<uint64_t>(str[4]) << 24);
}

#endif  // CHRE_UTIL_ID_FROM_STRING_IMPL_H_
