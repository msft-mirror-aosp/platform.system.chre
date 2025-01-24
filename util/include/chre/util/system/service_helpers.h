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

#ifndef CHRE_UTIL_SYSTEM_SERVICE_HELPERS_H_
#define CHRE_UTIL_SYSTEM_SERVICE_HELPERS_H_

#include <cstdint>

namespace chre::message {

//! Finds the nanoapp ID and service ID for the given service descriptor if
//! the service descriptor is in the legacy service descriptor format.
//! @see chrePublishRpcServices for the legacy service format
//! @return true if the nanoapp ID and service ID were found and populated,
//! false otherwise
bool extractNanoappIdAndServiceId(const char *serviceDescriptor,
                                  uint64_t &nanoappId, uint64_t &serviceId);

}  // namespace chre::message

#endif  // CHRE_UTIL_SYSTEM_SERVICE_HELPERS_H_
