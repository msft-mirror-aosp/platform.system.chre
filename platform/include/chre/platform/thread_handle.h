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

#ifndef CHRE_PLATFORM_THREAD_HANDLE_H_
#define CHRE_PLATFORM_THREAD_HANDLE_H_

#include "chre/target_platform/thread_handle_base.h"

namespace chre {

/**
 * Wrapper around a platform-specific thread handle.
 *
 * The user can get a ThreadHandle representing the current thread or convert
 * to/from a platform-specific representation. ThreadHandles can be compared for
 * equality. ThreadHandles are copyable, though the exact behavior is platform
 * specific.
 *
 * ThreadHandleBase is subclassed to allow platforms to inject the storage for
 * their implementation.
 */
class ThreadHandle : public ThreadHandleBase {
 public:
  using ThreadHandleBase::NativeHandle;

  /**
   * Returns the ThreadHandle for the current thread/task.
   */
  static ThreadHandle GetCurrent() {
    return ThreadHandle();
  }

  /**
   * Creates a ThreadHandle from a platform-specific id.
   */
  explicit ThreadHandle(NativeHandle nativeHandle);

  /**
   * ThreadHandle is copyable and movable.
   */
  ThreadHandle(const ThreadHandle &other);
  ThreadHandle(ThreadHandle &&other);
  ThreadHandle &operator=(const ThreadHandle &other);
  ThreadHandle &operator=(ThreadHandle &&other);

  /**
   * Allows the platform to perform any necessary de-initialization.
   */
  ~ThreadHandle();

  /**
   * Returns the platform-specific id.
   */
  NativeHandle GetNative() const;

  /**
   * Compares with another ThreadHandle for equality.
   */
  bool operator==(const ThreadHandle &other) const;

  /**
   * Compares with another ThreadHandle for inequality.
   */
  bool operator!=(const ThreadHandle &other) const {
    return !(*this == other);
  }

 protected:
  /**
   * Allows the platform to perform any necessary initialization.
   */
  ThreadHandle();
};

}  // namespace chre

#include "chre/target_platform/thread_handle_impl.h"

#endif  // CHRE_PLATFORM_THREAD_HANDLE_H_
