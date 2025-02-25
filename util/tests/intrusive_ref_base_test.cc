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

#include <memory>

#include "gtest/gtest.h"

#include "chre/util/memory.h"
#include "chre/util/system/intrusive_ref_base.h"

#include "pw_intrusive_ptr/intrusive_ptr.h"
#include "pw_intrusive_ptr/recyclable.h"

namespace {

class TestBase : public chre::IntrusiveRefBase,
                 public pw::Recyclable<TestBase> {
 public:
  ~TestBase() {
    destructorCount++;
  }

  void pw_recycle() {
    chre::memoryFreeAndDestroy(this);
  }

  static int destructorCount;
};
int TestBase::destructorCount = 0;

class IntrusiveRefBaseTest : public testing::Test {
 public:
  void SetUp() override {
    TestBase::destructorCount = 0;
  }
};

}  // namespace

TEST_F(IntrusiveRefBaseTest, ObjectIsDestroyed) {
  TestBase *object =
      static_cast<TestBase *>(chre::memoryAlloc(sizeof(TestBase)));
  ASSERT_NE(object, nullptr);
  std::construct_at(object);

  {
    pw::IntrusivePtr<TestBase> objectPtr(object);
    EXPECT_EQ(0, TestBase::destructorCount);

    {
      pw::IntrusivePtr<TestBase> objectPtr2(object);
      EXPECT_EQ(0, TestBase::destructorCount);
    }
    EXPECT_EQ(0, TestBase::destructorCount);
  }
  EXPECT_EQ(1, TestBase::destructorCount);
}
