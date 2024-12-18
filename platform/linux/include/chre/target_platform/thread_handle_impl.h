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

#ifndef CHRE_PLATFORM_LINUX_THREAD_HANDLE_IMPL_H_
#define CHRE_PLATFORM_LINUX_THREAD_HANDLE_IMPL_H_

#include "chre/platform/thread_handle.h"

#include <pthread.h>

namespace chre {

inline ThreadHandle::ThreadHandle(ThreadHandle::NativeHandle nativeHandle) {
  mHandle = nativeHandle;
}

inline ThreadHandle::ThreadHandle(const ThreadHandle &other) = default;
inline ThreadHandle::ThreadHandle(ThreadHandle &&other) = default;
inline ThreadHandle &ThreadHandle::operator=(const ThreadHandle &other) =
    default;
inline ThreadHandle &ThreadHandle::operator=(ThreadHandle &&other) = default;
inline ThreadHandle::~ThreadHandle() = default;

inline ThreadHandle::NativeHandle ThreadHandle::GetNative() const {
  return mHandle;
}

inline bool ThreadHandle::operator==(const ThreadHandle &other) const {
  return pthread_equal(mHandle, other.mHandle);
}

inline ThreadHandle::ThreadHandle() : ThreadHandle(pthread_self()) {}

}  // namespace chre

#endif  // CHRE_PLATFORM_LINUX_THREAD_HANDLE_IMPL_H_
