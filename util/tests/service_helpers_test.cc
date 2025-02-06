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

#include "chre/util/system/service_helpers.h"
#include "gtest/gtest.h"

namespace chre::message {
namespace {

TEST(ServiceHelpersTest, ExtractNanoappIdAndServiceId_NullServiceDescriptor) {
  uint64_t nanoappId;
  uint64_t serviceId;
  EXPECT_FALSE(extractNanoappIdAndServiceId(nullptr, nanoappId, serviceId));
}

TEST(ServiceHelpersTest, ExtractNanoappIdAndServiceId_InvalidPrefix) {
  uint64_t nanoappId;
  uint64_t serviceId;
  EXPECT_FALSE(
      extractNanoappIdAndServiceId("invalid_prefix", nanoappId, serviceId));
}

TEST(ServiceHelpersTest, ExtractNanoappIdAndServiceId_MissingSeparator) {
  uint64_t nanoappId;
  uint64_t serviceId;
  EXPECT_FALSE(extractNanoappIdAndServiceId("chre.nanoapp_0x1234567890ABCDEF",
                                            nanoappId, serviceId));
}

TEST(ServiceHelpersTest, ExtractNanoappIdAndServiceId_InvalidEncodingLength) {
  uint64_t nanoappId;
  uint64_t serviceId;
  EXPECT_FALSE(extractNanoappIdAndServiceId(
      "chre.nanoapp_0x1234567890ABCDEF.service_0x1234567890ABCDE", nanoappId,
      serviceId));

  EXPECT_FALSE(extractNanoappIdAndServiceId(
      "chre.nanoapp_0x1234567890ABCDE.service_0x1234567890ABCDEF", nanoappId,
      serviceId));

  EXPECT_FALSE(extractNanoappIdAndServiceId("chre.nanoapp_0x0.service_0x1",
                                            nanoappId, serviceId));

  EXPECT_FALSE(extractNanoappIdAndServiceId("chre.nanoapp_0x.service_0x",
                                            nanoappId, serviceId));

  EXPECT_FALSE(extractNanoappIdAndServiceId(
      "chre.nanoapp_0x1234567890ABCDEF.service_0x", nanoappId, serviceId));

  EXPECT_FALSE(extractNanoappIdAndServiceId(
      "chre.nanoapp_0x.service_0x1234567890ABCDEF", nanoappId, serviceId));
}

TEST(ServiceHelpersTest, ExtractNanoappIdAndServiceId_Success) {
  uint64_t nanoappId;
  uint64_t serviceId;
  EXPECT_TRUE(extractNanoappIdAndServiceId(
      "chre.nanoapp_0x1234567890ABCDEF.service_0x1234567890ABCDEF", nanoappId,
      serviceId));
  EXPECT_EQ(nanoappId, 0x1234567890ABCDEF);
  EXPECT_EQ(serviceId, 0x1234567890ABCDEF);

  EXPECT_TRUE(extractNanoappIdAndServiceId(
      "chre.nanoapp_0xDEADBEEFCAFECAFE.service_0xCAFECAFECAFECAFE", nanoappId,
      serviceId));
  EXPECT_EQ(nanoappId, 0xDEADBEEFCAFECAFE);
  EXPECT_EQ(serviceId, 0xCAFECAFECAFECAFE);
}

}  // namespace
}  // namespace chre::message
