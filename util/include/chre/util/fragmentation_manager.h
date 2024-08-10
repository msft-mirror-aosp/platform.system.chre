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

#ifndef CHRE_UTIL_FRAGMENTATION_MANAGER
#define CHRE_UTIL_FRAGMENTATION_MANAGER
#include <stddef.h>
#include <type_traits>
#include "chre/util/optional.h"
namespace chre {

/**
 * Structure representing a fragment of continuous data.
 *
 * @tparam ObjectType the type of the data.
 *
 * @param data pointer to the start of the data.
 * @param size number of count of the data.
 */
template <typename ObjectType>
struct Fragment {
  ObjectType *data;
  size_t size;
  Fragment(ObjectType *data_, size_t size_) : data(data_), size(size_) {}
};

/**
 * A data structure designed to partition continuous sequences of data into
 * manageable fragments.
 *
 * This class is particularly useful when dealing with large datasets, allowing
 * for efficient processing. Each fragment represents a contiguous subset of the
 * original data, with the size of each fragment determined by the
 * @c fragmentSize parameter besides the last fragment. The last fragment might
 * have less data then @c fragmentSize if there is no enough data left.
 * It is also the creator's responsibility to make sure that the data is alive
 * during the usage of FragmentationManager since FragmentationManager does not
 * keep a copy of the data.
 *
 * @tparam ObjectType The specific type of data element (object) to be stored
 * within this structure.
 * @tparam fragmentSize The number of @c ObjectType elements that constitute a
 * single fragment.
 */
template <typename ObjectType, size_t fragmentSize>
class FragmentationManager {
 public:
  /**
   * Initializes the fragmentation manager, partitioning a continuous block of
   * data into fragments.
   *
   * @param dataSource A raw pointer to pointing to the beginning of the
   * continuous data block to be fragmented.
   * @param dataSize The total number of bytes in the dataSource.
   *
   * @return false if dataSource is initialized with a nullptr; otherwise true.
   */
  bool init(ObjectType *dataSource, size_t dataSize);

  /**
   * Deinitializes the fragmentation manager.
   */
  void deinit();

  /**
   * Retrieves the next available data fragment.
   *
   * @return a @c Fragment with @c data pointing to the address of the
   * fragment's data; @c size with the size of the fragment. If there is no more
   * fragments, return an empty optional.
   */
  Optional<Fragment<ObjectType>> getNextFragment();

  /**
   * @return the number of fragments that have been emitted so far.
   */
  size_t getEmittedFragmentedCount() {
    return mEmittedFragment;
  }
  /**
   * @return True if all fragments have been emitted.

   */
  bool hasNoMoreFragment() {
    return mEmittedFragment * fragmentSize >= mDataSize;
  }

 private:
  // A pointer to the beginning of the continuous block of data being
  // fragmented.
  ObjectType *mData;
  // The number of bytes in the 'mData' block.
  size_t mDataSize;
  // The number of fragments that have been emitted.
  size_t mEmittedFragment;
};
}  // namespace chre

#include "chre/util/fragmentation_manager_impl.h"
#endif  // CHRE_UTIL_FRAGMENTATION_MANAGER
