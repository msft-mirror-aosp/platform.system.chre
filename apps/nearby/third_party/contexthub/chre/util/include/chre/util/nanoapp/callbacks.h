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

#ifndef CHRE_UTIL_CALLBACKS_H_
#define CHRE_UTIL_CALLBACKS_H_

#include <cstddef>

namespace chre {

/**
 * Convenience wrapper that only calls chreHeapFree() on the given message. For
 * use with CHRE APIs that take a chreMessageFreeFunction, and when the message
 * was allocated through chreHeapAlloc().
 *
 * @see chreMessageFreeFunction
 */
void heapFreeMessageCallback(void *message, size_t messageSize);

}  // namespace chre

#endif  // CHRE_UTIL_CALLBACKS_H_
