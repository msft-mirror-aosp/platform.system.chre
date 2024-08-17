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

#include "chre/util/fragmentation_manager.h"
#include <stdio.h>
#include <type_traits>
#include "chre/util/memory.h"
#include "gtest/gtest.h"

namespace chre::test {

TEST(FragmentationTest, CanRetrieveByteDataTest) {
  constexpr size_t dataSize = 9;
  constexpr size_t fragmentSize = 3;
  uint8_t testData[dataSize];
  Optional<Fragment<uint8_t>> fragment;
  FragmentationManager<uint8_t, fragmentSize> testManager;

  for (size_t i = 0; i < dataSize; i++) {
    testData[i] = i;
  }
  testManager.init(testData, dataSize);
  for (size_t iteration = 0; iteration < dataSize / fragmentSize; ++iteration) {
    std::cout << iteration << std::endl;
    fragment = testManager.getNextFragment();
    EXPECT_TRUE(fragment.has_value());
    EXPECT_EQ(fragment.value().size, fragmentSize);
    for (size_t j = 0; j < fragmentSize; j++) {
      EXPECT_EQ(fragment.value().data[j], j + iteration * fragmentSize);
    }
  }

  fragment = testManager.getNextFragment();
  EXPECT_FALSE(fragment.has_value());

  testManager.deinit();
}

TEST(FragmentationTest, CanRetrieveLongDataTest) {
  constexpr size_t dataSize = 10;
  constexpr size_t fragmentSize = 3;
  uint32_t testData[dataSize];
  Optional<Fragment<uint32_t>> fragment;
  FragmentationManager<uint32_t, fragmentSize> testManager;

  for (size_t i = 0; i < dataSize; i++) {
    testData[i] = i;
  }
  testManager.init(testData, dataSize);
  for (size_t iteration = 0; iteration < dataSize / fragmentSize; ++iteration) {
    fragment = testManager.getNextFragment();
    EXPECT_TRUE(fragment.has_value());
    EXPECT_EQ(fragment.value().size, fragmentSize);
    for (size_t j = 0; j < fragmentSize; j++) {
      EXPECT_EQ(fragment.value().data[j], j + iteration * fragmentSize);
    }
  }

  // Special case for the last element.
  fragment = testManager.getNextFragment();
  EXPECT_TRUE(fragment.has_value());
  EXPECT_EQ(fragment.value().size, 1);
  EXPECT_EQ(fragment.value().data[0], testData[dataSize - 1]);

  fragment = testManager.getNextFragment();
  EXPECT_FALSE(fragment.has_value());

  testManager.deinit();
}

TEST(FragmentationTest, FailWhenInitializingWithNullptr) {
  constexpr size_t dataSize = 10;
  constexpr size_t fragmentSize = 3;
  FragmentationManager<uint64_t, fragmentSize> testManager;
  EXPECT_FALSE(testManager.init(nullptr, dataSize));
}

TEST(FragmentationTest, CanRetrieveLongComplexDataTest) {
  struct Foo {
    uint8_t byteData;
    uint32_t longData;
    uint64_t doubleData;
  };

  constexpr size_t dataSize = 10;
  constexpr size_t fragmentSize = 3;
  Foo testData[dataSize];
  Optional<Fragment<Foo>> fragment;
  FragmentationManager<Foo, fragmentSize> testManager;

  for (size_t i = 0; i < dataSize; i++) {
    testData[i].byteData = i;
    testData[i].longData = static_cast<uint64_t>(i) << 16 | i;
    testData[i].doubleData = static_cast<uint64_t>(i) << 32 | i;
  }

  EXPECT_TRUE(testManager.init(testData, dataSize));
  for (size_t iteration = 0; iteration < dataSize / fragmentSize; ++iteration) {
    fragment = testManager.getNextFragment();
    EXPECT_TRUE(fragment.has_value());
    EXPECT_EQ(fragment.value().size, fragmentSize);
    for (size_t j = 0; j < fragmentSize; j++) {
      uint8_t arrayIndex = j + iteration * fragmentSize;
      EXPECT_EQ(
          memcmp(&fragment.value().data[j], &testData[arrayIndex], sizeof(Foo)),
          0);
    }
    EXPECT_EQ(fragment.value().data, &testData[iteration * fragmentSize]);
  }

  // Special case for the last element.
  fragment = testManager.getNextFragment();
  EXPECT_TRUE(fragment.has_value());
  EXPECT_EQ(fragment.value().size, 1);
  EXPECT_EQ(
      memcmp(&fragment.value().data[0], &testData[dataSize - 1], sizeof(Foo)),
      0);

  fragment = testManager.getNextFragment();
  EXPECT_FALSE(fragment.has_value());

  testManager.deinit();
}

TEST(FragmentationTest, CanReuseAfterDeinitInitTest) {
  constexpr size_t dataSize = 10;
  constexpr size_t fragmentSize = 3;
  uint32_t testData[dataSize];
  for (size_t i = 0; i < dataSize; i++) {
    testData[i] = i;
  }

  constexpr size_t realDataSize = 13;
  uint32_t realTestData[realDataSize];
  for (size_t i = 0; i < realDataSize; i++) {
    realTestData[i] = UINT32_MAX - i;
  }

  Optional<Fragment<uint32_t>> fragment;
  FragmentationManager<uint32_t, fragmentSize> testManager;

  testManager.init(testData, dataSize);
  for (size_t iteration = 0; iteration < dataSize / fragmentSize; ++iteration) {
    testManager.getNextFragment();
  }
  testManager.deinit();

  testManager.init(realTestData, realDataSize);
  for (size_t iteration = 0; iteration < realDataSize / fragmentSize;
       ++iteration) {
    fragment = testManager.getNextFragment();
    EXPECT_TRUE(fragment.has_value());
    EXPECT_EQ(fragment.value().size, fragmentSize);
    for (size_t j = 0; j < fragmentSize; j++) {
      EXPECT_EQ(fragment.value().data[j],
                realTestData[iteration * fragmentSize + j]);
    }
  }

  // Special case for the last element.
  fragment = testManager.getNextFragment();
  EXPECT_TRUE(fragment.has_value());
  EXPECT_EQ(fragment.value().size, 1);
  EXPECT_EQ(fragment.value().data[0], realTestData[realDataSize - 1]);

  fragment = testManager.getNextFragment();
  EXPECT_FALSE(fragment.has_value());

  testManager.deinit();
}

}  // namespace chre::test
