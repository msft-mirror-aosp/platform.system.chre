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

#ifndef CHRE_UTIL_SYSTEM_MESSAGE_ROUTER_CALLBACK_ALLOCATOR_H_
#define CHRE_UTIL_SYSTEM_MESSAGE_ROUTER_CALLBACK_ALLOCATOR_H_

#include <pw_allocator/allocator.h>
#include <pw_allocator/capability.h>
#include <pw_allocator/unique_ptr.h>
#include <pw_containers/vector.h>
#include <pw_function/function.h>
#include <cstddef>
#include <optional>

#include "chre/platform/mutex.h"

namespace chre::message {

//! An allocator for message free callbacks
//! This allocator is used to store message free callbacks in a vector
//! The allocator will call the free callback when the message is deallocated
//! This is used to create pw::UniquePtrs for messages that have a free
//! callback.
//! @param Metadata The metadata type for the callback function
template <typename Metadata>
class MessageRouterCallbackAllocator : public pw::Allocator {
 public:
  static constexpr Capabilities kCapabilities = 0;

  //! The callback used to free a message
  using MessageFreeCallback = pw::Function<void(
      std::byte *message, size_t length, Metadata &&metadata)>;

  //! A record of a message and its free callback
  struct FreeCallbackRecord {
    std::byte *message;
    Metadata metadata;
    size_t messageSize;
  };

  MessageRouterCallbackAllocator(
      MessageFreeCallback &&callback,
      pw::Vector<FreeCallbackRecord> &freeCallbackRecords,
      bool doEraseRecord = true);

  //! @see pw::Allocator::DoAllocate
  virtual void *DoAllocate(Layout /* layout */) override;

  //! @see pw::Allocator::DoDeallocate
  virtual void DoDeallocate(void *ptr) override;

  //! Creates a pw::UniquePtr for a message with a free callback.
  //! The free callback will be called when the message is deallocated.
  //! @return A pw::UniquePtr containing the message
  [[nodiscard]] pw::UniquePtr<std::byte[]> MakeUniqueArrayWithCallback(
      std::byte *ptr, size_t size, Metadata &&metadata);

  //! Gets the free callback record for a message. Also removes the record from
  //! the vector.
  //! @param ptr The message pointer
  //! @return The free callback record for the message, or std::nullopt if not
  //! found
  std::optional<FreeCallbackRecord> GetAndRemoveFreeCallbackRecord(void *ptr);

 private:
  //! The callback used to free a message
  MessageFreeCallback mCallback;

  //! The mutex to protect mFreeCallbackRecords
  Mutex mMutex;

  //! The map of message pointers to free callbacks
  pw::Vector<FreeCallbackRecord> &mFreeCallbackRecords;

  //! Whether to erase the record from the vector after the message is freed
  const bool mDoEraseRecord;
};

}  // namespace chre::message

#include "chre/util/system/message_router_callback_allocator_impl.h"

#endif  // CHRE_UTIL_SYSTEM_MESSAGE_ROUTER_CALLBACK_ALLOCATOR_H_
