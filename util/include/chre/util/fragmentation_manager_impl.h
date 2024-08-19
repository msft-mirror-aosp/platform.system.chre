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

#ifndef CHRE_UTIL_FRAGMENTATION_MANAGER_IMPL
#define CHRE_UTIL_FRAGMENTATION_MANAGER_IMPL
#include "chre/util/fragmentation_manager.h"
#include "chre/util/optional.h"

namespace chre {

template <typename ObjectType, size_t fragmentSize>
bool FragmentationManager<ObjectType, fragmentSize>::init(
    ObjectType *dataSource, size_t dataSize) {
  if (dataSource == nullptr) {
    return false;
  }
  mData = dataSource;
  mDataSize = dataSize;
  mEmittedFragment = 0;
  return true;
}

template <typename ObjectType, size_t fragmentSize>
void FragmentationManager<ObjectType, fragmentSize>::deinit() {
  mData = nullptr;
  mDataSize = 0;
  mEmittedFragment = 0;
}

template <typename ObjectType, size_t fragmentSize>
Optional<Fragment<ObjectType>>
FragmentationManager<ObjectType, fragmentSize>::getNextFragment() {
  if (hasNoMoreFragment()) {
    return Optional<Fragment<ObjectType>>();
  }
  size_t currentFragmentSize = fragmentSize;
  // Special case to calculate the size of the last fragment.
  if ((mEmittedFragment + 1) * fragmentSize > mDataSize) {
    currentFragmentSize = mDataSize % fragmentSize;
  }
  Fragment<ObjectType> fragment(mData + mEmittedFragment * fragmentSize,
                                currentFragmentSize);
  ++mEmittedFragment;
  return fragment;
}

}  // namespace chre
#endif  // CHRE_UTIL_FRAGMENTATION_MANAGER_IMPL
