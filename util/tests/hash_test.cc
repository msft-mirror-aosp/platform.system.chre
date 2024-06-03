
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

#include "gtest/gtest.h"

#include "chre/util/hash.h"

using ::chre::fnv1a32Hash;

TEST(Hash, HashRejectsInvalidInput) {
  uint8_t data = 3;
  EXPECT_EQ(fnv1a32Hash(nullptr, 32), UINT32_MAX);
  EXPECT_EQ(fnv1a32Hash(nullptr, 0), UINT32_MAX);
  EXPECT_EQ(fnv1a32Hash(&data, 0), UINT32_MAX);
}

TEST(Hash, HashCalculatesKnownValues) {
  const char* dataStr = "CHRE";
  EXPECT_EQ(fnv1a32Hash(reinterpret_cast<const uint8_t*>(dataStr),
                      strlen(dataStr)),
            0x9ebfaf03);

  dataStr = "ERHC";
  EXPECT_EQ(fnv1a32Hash(reinterpret_cast<const uint8_t*>(dataStr),
                      strlen(dataStr)),
            0x45406abb);

  dataStr = "Android Open Source Project";
  EXPECT_EQ(fnv1a32Hash(reinterpret_cast<const uint8_t*>(dataStr),
                      strlen(dataStr)),
            0x8c9705a8);
}
