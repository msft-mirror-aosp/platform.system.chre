/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef CHRE_UTIL_SYSTEM_INTRUSIVE_REF_BASE_H_
#define CHRE_UTIL_SYSTEM_INTRUSIVE_REF_BASE_H_

#include "chre/platform/atomic.h"
#include "pw_intrusive_ptr/intrusive_ptr.h"

namespace chre {

//! Base class for any type used with pw::IntrusivePtr that needs to support
//! reference counting.
class IntrusiveRefBase {
 public:
  //! Increments reference counter.
  void AddRef() const { mRefCount.fetch_increment(); }

  //! Decrements reference count and returns true if the object should be
  //! deleted.
  [[nodiscard]] bool ReleaseRef() const {
    return mRefCount.fetch_decrement() == 1;
  }

  //! Reference count
  [[nodiscard]] int32_t ref_count() const {
    return mRefCount.load();
  }

 protected:
  constexpr IntrusiveRefBase() = default;
  IntrusiveRefBase(const IntrusiveRefBase &) = delete;
  IntrusiveRefBase &operator=(const IntrusiveRefBase &) = delete;
  IntrusiveRefBase(IntrusiveRefBase &&) = delete;
  IntrusiveRefBase &operator=(IntrusiveRefBase &&) = delete;
  virtual ~IntrusiveRefBase() = default;

 private:
  template <typename T>
  friend class pw::IntrusivePtr;

  //! Reference count
  mutable AtomicUint32 mRefCount{0};
};

}  // namespace chre

#endif  // CHRE_UTIL_SYSTEM_INTRUSIVE_REF_BASE_H_
