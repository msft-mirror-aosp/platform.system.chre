/*
 * Copyright (C) 2017 The Android Open Source Project
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

#ifndef CHRE_UTIL_CONTAINER_SUPPORT_H_
#define CHRE_UTIL_CONTAINER_SUPPORT_H_

/**
 * @file Provides replacements for macros and functions that are normally
 * provided by the CHRE framework implementation. These portable implementations
 * are implemented using the CHRE API rather than private system APIs.
 */

#if defined CHRE_IS_NANOAPP_BUILD

#include "chre/util/nanoapp/assert.h"
#include "chre_api/chre.h"

#ifdef CHRE_STANDALONE_POSIX_ALIGNED_ALLOC
#include <stdlib.h>
#else
#include "chre/util/always_false.h"
#endif  // CHRE_STANDALONE_POSIX_ALIGNED_ALLOC

namespace chre {

/**
 * Provides the memoryAlloc function that is normally provided by the CHRE
 * runtime. It maps into chreHeapAlloc.
 *
 * @param size the size of the allocation to make.
 * @return a pointer to allocated memory or nullptr if allocation failed.
 */
inline void *memoryAlloc(size_t size) {
  return chreHeapAlloc(static_cast<uint32_t>(size));
}

/**
 * Returns memory of suitable alignment to hold an array of the given object
 * type, which may exceed alignment of std::max_align_t and therefore cannot
 * use memoryAlloc().
 *
 * @param count the number of elements to allocate.
 * @return a pointer to allocated memory or nullptr if allocation failed.
 */
template <typename T>
inline T *memoryAlignedAllocArray([[maybe_unused]] size_t count) {
#ifdef CHRE_STANDALONE_POSIX_ALIGNED_ALLOC
  void *ptr;
  int result = posix_memalign(&ptr, alignof(T), sizeof(T) * count);
  if (result != 0) {
    ptr = nullptr;
  }
  return static_cast<T *>(ptr);
#else
  static_assert(AlwaysFalse<T>::value,
                "memoryAlignedAlloc is unsupported on this platform");
  return nullptr;
#endif  // CHRE_STANDALONE_POSIX_ALIGNED_ALLOC
}

/**
 * Returns memory of suitable alignment to hold a given object of the
 * type T, which may exceed alignment of std::max_align_t and therefore cannot
 * use memoryAlloc().
 *
 * @return a pointer to allocated memory or nullptr if allocation failed.
 */
template <typename T>
inline T *memoryAlignedAlloc() {
  return memoryAlignedAllocArray<T>(/* count= */ 1);
}

/**
 * Provides the memoryFree function that is normally provided by the CHRE
 * runtime. It maps into chreHeapFree.
 *
 * @param pointer the allocation to release.
 */
inline void memoryFree(void *pointer) {
  chreHeapFree(pointer);
}

}  // namespace chre

#elif defined CHRE_IS_HOST_BUILD

#include "chre/util/host/assert.h"

namespace chre {

/**
 * Provides the memoryAlloc function that is normally provided by the CHRE
 * runtime. It maps into malloc.
 *
 * @param size the size of the allocation to make.
 * @return a pointer to allocated memory or nullptr if allocation failed.
 */
inline void *memoryAlloc(size_t size) {
  return malloc(size);
}

/**
 * Returns memory of suitable alignment to hold an array of the given object
 * type, which may exceed alignment of std::max_align_t and therefore cannot
 * use memoryAlloc().
 *
 * @param count the number of elements to allocate.
 * @return a pointer to allocated memory or nullptr if allocation failed.
 */
template <typename T>
inline T *memoryAlignedAllocArray(size_t count) {
  void *ptr;
  int result = posix_memalign(&ptr, alignof(T), sizeof(T) * count);
  if (result != 0) {
    ptr = nullptr;
  }
  return static_cast<T *>(ptr);
}

/**
 * Returns memory of suitable alignment to hold a given object of the
 * type T, which may exceed alignment of std::max_align_t and therefore cannot
 * use memoryAlloc().
 *
 * @return a pointer to allocated memory or nullptr if allocation failed.
 */
template <typename T>
inline T *memoryAlignedAlloc() {
  return memoryAlignedAllocArray<T>(/* count= */1);
}

/**
 * Provides the memoryFree function that is normally provided by the CHRE
 * runtime. It maps into free.
 *
 * @param pointer the allocation to release.
 */
inline void memoryFree(void *pointer) {
  free(pointer);
}

}  // namespace chre

#else
#include "chre/platform/assert.h"
#include "chre/platform/memory.h"
#endif  // CHRE_IS_NANOAPP_BUILD

#endif  // CHRE_UTIL_CONTAINER_SUPPORT_H_
