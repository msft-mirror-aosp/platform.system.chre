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

#ifndef CHRE_UTIL_SYSTEM_MESSAGE_ROUTER_CALLBACK_ALLOCATOR_IMPL_H_
#define CHRE_UTIL_SYSTEM_MESSAGE_ROUTER_CALLBACK_ALLOCATOR_IMPL_H_

#include <pw_allocator/allocator.h>
#include <pw_allocator/capability.h>
#include <pw_allocator/unique_ptr.h>
#include <pw_containers/vector.h>
#include <pw_function/function.h>
#include <cstddef>
#include <optional>

#include "chre/util/lock_guard.h"
#include "chre/util/system/message_common.h"
#include "chre/util/system/message_router_callback_allocator.h"

namespace chre::message {

template <typename Metadata>
MessageRouterCallbackAllocator<Metadata>::MessageRouterCallbackAllocator(
    MessageFreeCallback &&callback,
    pw::Vector<FreeCallbackRecord> &freeCallbackRecords)
        : pw::Allocator(kCapabilities),
          mCallback(std::move(callback)),
          mFreeCallbackRecords(freeCallbackRecords) {}

template <typename Metadata>
void *MessageRouterCallbackAllocator<Metadata>::DoAllocate(
    Layout /* layout */) {
  return nullptr;
}

template <typename Metadata>
void MessageRouterCallbackAllocator<Metadata>::DoDeallocate(void *ptr) {
  std::optional<FreeCallbackRecord> freeCallbackRecord;
  {
    LockGuard<Mutex> lock(mMutex);
    for (FreeCallbackRecord &record : mFreeCallbackRecords) {
      if (record.message == ptr) {
        freeCallbackRecord = std::move(record);
        mFreeCallbackRecords.erase(&record);
        break;
      }
    }
  }

  if (freeCallbackRecord.has_value()) {
    mCallback(freeCallbackRecord->message, freeCallbackRecord->messageSize,
              freeCallbackRecord->metadata);
  }
}

template <typename Metadata>
pw::UniquePtr<std::byte[]>
MessageRouterCallbackAllocator<Metadata>::MakeUniqueArrayWithCallback(
    std::byte *ptr, size_t size, Metadata &&metadata) {
  {
    LockGuard<Mutex> lock(mMutex);
    if (mFreeCallbackRecords.full()) {
      return pw::UniquePtr<std::byte[]>();
    }

    mFreeCallbackRecords.push_back(
        {.message = ptr, .metadata = std::move(metadata), .messageSize = size});
  }

  return WrapUniqueArray(ptr, size);
}

}  // namespace chre::message

#endif  // CHRE_UTIL_SYSTEM_MESSAGE_ROUTER_CALLBACK_ALLOCATOR_IMPL_H_
