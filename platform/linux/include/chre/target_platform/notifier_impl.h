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

#ifndef CHRE_PLATFORM_LINUX_NOTIFIER_IMPL_H_
#define CHRE_PLATFORM_LINUX_NOTIFIER_IMPL_H_

#include "chre/platform/notifier.h"

#include <pthread.h>

#include <cinttypes>
#include <mutex>

#include "chre/platform/log.h"
#include "chre/platform/thread_handle.h"

namespace chre {

inline Notifier::Notifier() = default;
inline Notifier::~Notifier() = default;

inline void Notifier::Bind(ThreadHandle threadHandle) {
  mTarget = threadHandle.GetNative();
}

inline void Notifier::Wait() {
  CHRE_ASSERT_LOG(mTarget, "Notifier is not bound.");
  CHRE_ASSERT_LOG(pthread_equal(pthread_self(), *mTarget),
                  "Wrong thread calling Notifier::Wait(). Expected %" PRIu64
                  ", got %" PRIu64,
                  *mTarget, pthread_self());
  std::unique_lock lock(mLock);
  mCondVar.wait(lock, [this] { return mNotified; });
  mNotified = false;
}

inline void Notifier::Notify() {
  {
    std::lock_guard lock(mLock);
    mNotified = true;
  }
  mCondVar.notify_one();
}

inline void Notifier::Clear() {
  CHRE_ASSERT_LOG(mTarget, "Notifier is not bound.");
  CHRE_ASSERT_LOG(pthread_equal(pthread_self(), *mTarget),
                  "Wrong thread calling Notifier::Wait(). Expected %" PRIu64
                  ", got %" PRIu64,
                  *mTarget, pthread_self());
  std::lock_guard lock(mLock);
  mNotified = false;
}

}  // namespace chre

#endif  // CHRE_PLATFORM_LINUX_NOTIFIER_IMPL_H_
