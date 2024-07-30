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

#include "chre_api/chre.h"
#include "gtest/gtest.h"

#include "chre/platform/linux/system_time.h"
#include "chre/util/duplicate_message_detector.h"

using chre::platform_linux::SystemTimeOverride;

namespace chre {

constexpr Nanoseconds kTimeout = Nanoseconds(100);
constexpr uint32_t kNumMessages = 100;

TEST(DuplicateMessageDetectorTest, AddMessageCanBeFound) {
  DuplicateMessageDetector duplicateMessageDetector(kTimeout);
  uint32_t messageSequenceNumber = 1;
  uint16_t hostEndpoint = 2;

  EXPECT_FALSE(duplicateMessageDetector.findOrAdd(messageSequenceNumber,
                                                  hostEndpoint).has_value());
}

TEST(DuplicateMessageDetectorTest, AddMultipleCanBeFound) {
  DuplicateMessageDetector duplicateMessageDetector(kTimeout);
  for (size_t i = 0; i < kNumMessages; ++i) {
    EXPECT_FALSE(duplicateMessageDetector.findOrAdd(i, i).has_value());
  }
}

TEST(DuplicateMessageDetectorTest, RemoveOldEntries) {
  DuplicateMessageDetector duplicateMessageDetector(kTimeout);

  for (size_t i = 0; i < kNumMessages; ++i) {
    SystemTimeOverride override(i);
    EXPECT_FALSE(duplicateMessageDetector.findOrAdd(i, kNumMessages - i)
                 .has_value());
  }

  SystemTimeOverride override(kTimeout * 10);
  duplicateMessageDetector.removeOldEntries();

  for (size_t i = 0; i < kNumMessages; ++i) {
    EXPECT_FALSE(duplicateMessageDetector.findAndSetError(i,
                                                          kNumMessages - i,
                                                          CHRE_ERROR_NONE));
  }
}

TEST(DuplicateMessageDetectorTest, RemoveOldEntriesDoesNotRemoveRecentEntries) {
  DuplicateMessageDetector duplicateMessageDetector(kTimeout);

  for (size_t i = 0; i < kNumMessages; ++i) {
    SystemTimeOverride override(i);
    EXPECT_FALSE(duplicateMessageDetector.findOrAdd(i, i).has_value());
  }

  {
    constexpr uint32_t kNumMessagesToRemove = kNumMessages / 2;
    SystemTimeOverride override(kNumMessagesToRemove +
                                kTimeout.toRawNanoseconds());
    duplicateMessageDetector.removeOldEntries();

    for (size_t i = 0; i <= kNumMessagesToRemove; ++i) {
      EXPECT_FALSE(duplicateMessageDetector.findAndSetError(i, i,
                                                            CHRE_ERROR_NONE));
    }
    for (size_t i = kNumMessagesToRemove + 1; i < kNumMessages; ++i) {
      bool isDuplicate = false;
      EXPECT_FALSE(
          duplicateMessageDetector.findOrAdd(i, i, &isDuplicate).has_value());
      EXPECT_TRUE(isDuplicate);
      EXPECT_TRUE(duplicateMessageDetector.findAndSetError(i, i,
                                                           CHRE_ERROR_NONE));

      isDuplicate = false;
      Optional<chreError> error =
          duplicateMessageDetector.findOrAdd(i, i, &isDuplicate);
      EXPECT_TRUE(error.has_value());
      EXPECT_EQ(error.value(), CHRE_ERROR_NONE);
      EXPECT_TRUE(isDuplicate);
    }
  }
}

}  // namespace chre
