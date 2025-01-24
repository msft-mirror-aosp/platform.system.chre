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

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "chre/util/system/service_helpers.h"

namespace chre::message {

namespace {

//! Legacy format is: chre.nanoapp_0x<nanoappId>.service_0x<serviceId>
//! All IDs are in hexadecimal
constexpr char kPrefix[] = "chre.nanoapp_0x";
constexpr size_t kPrefixLength = sizeof(kPrefix) - 1;
constexpr char kSeparator[] = ".service_0x";
constexpr size_t kSeparatorLength = sizeof(kSeparator) - 1;
constexpr size_t kEncodingLength = 16;
constexpr size_t kBase = 16;
constexpr size_t kServiceDescriptorLength =
    kPrefixLength + kEncodingLength + kSeparatorLength + kEncodingLength;

//! Converts a string containing a 16-character hexadecimal encoding to a
//! uint64_t. We are using this instead of directly using strtoull because
//! some of our platforms do not support this function.
//! @return the converted uint64_t
uint64_t convertEncodedIdToUint64(const char *str) {
  constexpr size_t kHalfEncodingLength = kEncodingLength / 2;
  char buffer[kHalfEncodingLength + 1];
  buffer[kHalfEncodingLength] = '\0';

  // Convert the first half (upper 32 bits) of the encoding to a uint64_t
  memcpy(buffer, str, kHalfEncodingLength);
  uint64_t resultFirst = strtoul(buffer, nullptr, kBase);

  // Convert the second half (lower 32 bits) of the encoding to a uint64_t
  memcpy(buffer, str + kHalfEncodingLength, kHalfEncodingLength);
  uint64_t resultSecond = strtoul(buffer, nullptr, kBase);

  // Combine the two halves into a single uint64_t
  return (resultFirst << 32) | resultSecond;
}

}  // anonymous namespace

bool extractNanoappIdAndServiceId(const char *serviceDescriptor,
                                  uint64_t &nanoappId, uint64_t &serviceId) {
  // Reject null service descriptors
  if (serviceDescriptor == nullptr) {
    return false;
  }

  // Check the service descriptor length
  if (strlen(serviceDescriptor) != kServiceDescriptorLength) {
    return false;
  }

  // Check if the service descriptor starts with the legacy prefix
  if (strstr(serviceDescriptor, kPrefix) != serviceDescriptor) {
    return false;
  }

  // Check if the service descriptor contains the separator in the correct
  // location
  const char *separatorIndex =
      strstr(serviceDescriptor + kPrefixLength, kSeparator);
  if (separatorIndex == nullptr ||
      (separatorIndex - serviceDescriptor) * sizeof(char) !=
          kPrefixLength + kEncodingLength) {
    return false;
  }

  // Convert the encoded strings for the IDs to uint64_t
  nanoappId = convertEncodedIdToUint64(serviceDescriptor + kPrefixLength);
  serviceId = convertEncodedIdToUint64(separatorIndex + kSeparatorLength);
  return true;
}

}  // namespace chre::message
