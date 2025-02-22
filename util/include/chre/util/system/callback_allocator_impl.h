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

#ifndef CHRE_UTIL_SYSTEM_CALLBACK_ALLOCATOR_IMPL_H_
#define CHRE_UTIL_SYSTEM_CALLBACK_ALLOCATOR_IMPL_H_

#include <cstddef>
#include <optional>

#include "chre/util/lock_guard.h"
#include "chre/util/system/callback_allocator.h"

#include "pw_allocator/allocator.h"
#include "pw_allocator/capability.h"
#include "pw_allocator/unique_ptr.h"
#include "pw_containers/vector.h"
#include "pw_function/function.h"

namespace chre {

template <typename Metadata>
CallbackAllocator<Metadata>::CallbackAllocator(
    Callback &&callback, pw::Vector<CallbackRecord> &callbackRecords,
    bool doEraseRecord)
    : pw::Allocator(kCapabilities),
      mCallback(std::move(callback)),
      mCallbackRecords(callbackRecords),
      mDoEraseRecord(doEraseRecord) {}

template <typename Metadata>
void *CallbackAllocator<Metadata>::DoAllocate(Layout /* layout */) {
  // Do not allow usage of this allocator without providing a callback
  // function. This allocator does not manage the memory, only guarantees
  // that the callback will be called. Use MakeUniqueArrayWithCallback.
  return nullptr;
}

template <typename Metadata>
void CallbackAllocator<Metadata>::DoDeallocate(void *ptr) {
  std::optional<CallbackRecord> callbackRecord;
  {
    LockGuard<Mutex> lock(mMutex);
    for (CallbackRecord &record : mCallbackRecords) {
      if (record.message == ptr) {
        if (mDoEraseRecord) {
          callbackRecord = std::move(record);
          mCallbackRecords.erase(&record);
        } else {
          callbackRecord = record;
        }
        break;
      }
    }
  }

  if (callbackRecord.has_value()) {
    mCallback(callbackRecord->message, callbackRecord->messageSize,
              std::move(callbackRecord->metadata));
  }
}

template <typename Metadata>
pw::UniquePtr<std::byte[]>
CallbackAllocator<Metadata>::MakeUniqueArrayWithCallback(std::byte *ptr,
                                                         size_t size,
                                                         Metadata &&metadata) {
  {
    LockGuard<Mutex> lock(mMutex);
    if (mCallbackRecords.full()) {
      return pw::UniquePtr<std::byte[]>();
    }

    mCallbackRecords.push_back(
        {.message = ptr, .metadata = std::move(metadata), .messageSize = size});
  }

  return WrapUniqueArray(ptr, size);
}

template <typename Metadata>
std::optional<typename CallbackAllocator<Metadata>::CallbackRecord>
CallbackAllocator<Metadata>::GetAndRemoveCallbackRecord(void *ptr) {
  LockGuard<Mutex> lock(mMutex);
  std::optional<CallbackRecord> foundRecord;
  for (CallbackRecord &record : mCallbackRecords) {
    if (record.message == ptr) {
      foundRecord = std::move(record);
      mCallbackRecords.erase(&record);
      break;
    }
  }
  return foundRecord;
}

}  // namespace chre

#endif  // CHRE_UTIL_SYSTEM_CALLBACK_ALLOCATOR_IMPL_H_
