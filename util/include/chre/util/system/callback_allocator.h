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

#ifndef CHRE_UTIL_SYSTEM_CALLBACK_ALLOCATOR_H_
#define CHRE_UTIL_SYSTEM_CALLBACK_ALLOCATOR_H_

#include <cstddef>
#include <optional>

#include "chre/platform/mutex.h"

#include "pw_allocator/allocator.h"
#include "pw_allocator/capability.h"
#include "pw_allocator/unique_ptr.h"
#include "pw_containers/vector.h"
#include "pw_function/function.h"

namespace chre {

//! An allocator that keeps track of callbacks.
//! The allocator will call the callback when the underlying type is
//! deallocated.
//! @param Metadata The metadata type for the callback function
template <typename Metadata>
class CallbackAllocator : public pw::Allocator {
 public:
  static constexpr Capabilities kCapabilities = 0;

  //! The callback called when the underlying type is deallocated
  using Callback = pw::Function<void(std::byte *message, size_t length,
                                     Metadata &&metadata)>;

  //! A record of a message and its callback
  struct CallbackRecord {
    std::byte *message;
    Metadata metadata;
    size_t messageSize;
  };

  CallbackAllocator(Callback &&callback,
                    pw::Vector<CallbackRecord> &CallbackRecords,
                    bool doEraseRecord = true);

  //! @see pw::Allocator::DoAllocate
  virtual void *DoAllocate(Layout /* layout */) override;

  //! @see pw::Allocator::DoDeallocate
  virtual void DoDeallocate(void *ptr) override;

  //! Creates a pw::UniquePtr with a callback.
  //! The callback will be called when the underlying type is deallocated.
  //! @return A pw::UniquePtr containing data at ptr.
  [[nodiscard]] pw::UniquePtr<std::byte[]> MakeUniqueArrayWithCallback(
      std::byte *ptr, size_t size, Metadata &&metadata);

  //! Gets the callback record for a message. Also removes the record from
  //! the vector.
  //! @param ptr The message pointer
  //! @return The callback record for the message, or std::nullopt if not
  //! found
  std::optional<CallbackRecord> GetAndRemoveCallbackRecord(void *ptr);

 private:
  //! The callback called on deallocation
  Callback mCallback;

  //! The mutex to protect mCallbackRecords
  Mutex mMutex;

  //! The list of callback records
  pw::Vector<CallbackRecord> &mCallbackRecords;

  //! Whether to erase the record from the vector after the data is freed
  const bool mDoEraseRecord;
};

}  // namespace chre

#include "chre/util/system/callback_allocator_impl.h"

#endif  // CHRE_UTIL_SYSTEM_CALLBACK_ALLOCATOR_H_
