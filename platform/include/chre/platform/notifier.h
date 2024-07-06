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

#ifndef CHRE_PLATFORM_NOTIFIER_H_
#define CHRE_PLATFORM_NOTIFIER_H_

#include "chre/platform/thread_handle.h"
#include "chre/target_platform/notifier_base.h"
#include "chre/util/non_copyable.h"

namespace chre {

/**
 * Provides the ability to notify a fixed thread/task. A thread must be bound to
 * the instance, after which any thread (including itself) may notify it via
 * Notify(). The target thread receives notifications through the Wait() which
 * returns immediately if there are pending notifications or otherwise blocks.
 * Pending notifications are cleared before the target thread returns from
 * Wait().
 *
 * An example control flow between two threads:
 * [Event]  [T1]          [T2]      [Description]
 * 1.       Bind()        ...       T1 binds itself to the notifier
 * 2.       ...           Notify()  T2 notifies.
 * 3.       ...           Notify()  T2 notifies again before T1 calls Wait().
 * 2.       Wait()        ...       T1 waits for notifications. Returns
 *                                  immediately.
 * 3.       Wait()        ...       T1 waits again. The previous Wait() cleared
 *                                  both pending notifications so it blocks.
 * 4.       <blocked>     Notify()  T2 notifies. T1 is scheduled again.
 * 5.       ...           Notify()  T2 notifies.
 * 6.       Clear()       ...       T1 clears pending notifications.
 * 7.       Wait()        ...       T1 waits for notifications. Blocks as all
 *                                  pending notifications were cleared.
 *
 * NotifierBase is subclassed to allow platforms to inject the storage for their
 * implementation.
 */
class Notifier : public NotifierBase, public NonCopyable {
 public:
  /**
   * Allows the platform to perform any necessary initialization.
   */
  Notifier();

  /**
   * Allows the platform to perform any necessary de-initialization.
   */
  ~Notifier();

  /**
   * Binds a thread to this instance.
   *
   * By default, binds the instance to the current thread.
   *
   * This is not thread-safe w.r.t. Wait() and Notify(). It must be called
   * before either Wait() or Notify() are called.
   */
  void Bind(ThreadHandle threadHandle = ThreadHandle::GetCurrent());

  /**
   * Blocks the caller until/unless notified.
   *
   * Clears any pending notifications before returning. The user must be
   * prepared for spurious wake-ups.
   *
   * Must be called by the last thread bound to this instance.
   */
  void Wait();

  /**
   * Sets notification state, and if necessary, wakes the thread in Wait().
   *
   * Depending on the platform it may be valid to invoke this from an interrupt
   * context.
   */
  void Notify();

  /**
   * Clears any pending notifications.
   *
   * Must be called from the last thread bound to this instance.
   */
  void Clear();
};

}  // namespace chre

#include "chre/target_platform/notifier_impl.h"

#endif  // CHRE_PLATFORM_NOTIFIER_H_
