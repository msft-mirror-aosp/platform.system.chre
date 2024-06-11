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

#ifndef CHRE_UTIL_HASH_H_
#define CHRE_UTIL_HASH_H_

#include <cstddef>
#include <cstdint>

namespace chre {

/**
 * Computes the 32-bit FNV-1a hash of the data with size: size.
 * @param data The data to hash. Must be non-nullptr.
 * @param size The size of the data to hash. Must be greater than 0.
 * @return The FNV-1a hash or UINT32_MAX if the size is 0 or data is nullptr.
 */
uint32_t fnv1a32Hash(const uint8_t* data, size_t size);

}  // namespace chre

#endif  // CHRE_UTIL_HASH_H_
