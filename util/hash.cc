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

#include <cstddef>
#include <cstdint>

#include "chre/util/hash.h"

namespace chre {

uint32_t fnv1a32Hash(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return UINT32_MAX;
  }

  constexpr uint32_t kFnvPrime = 0x01000193;
  constexpr uint32_t kFnvOffset = 0x811c9dc5;

  uint32_t hash = kFnvOffset;
  for (size_t i = 0; i < size; ++i) {
    hash ^= data[i];
    hash *= kFnvPrime;
  }
  return hash;
}

}  // namespace chre
