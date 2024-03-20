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

#ifndef CHRE_NANOAPP_LOAD_LISTENER_H_
#define CHRE_NANOAPP_LOAD_LISTENER_H_

namespace android {
namespace chre {

class INanoappLoadListener {
 public:
  virtual ~INanoappLoadListener() = default;

  /**
   * Called before we send any nanoapp data to CHRE.
   *
   * @param appId The app ID associated with the nanoapp binary.
   */
  virtual void onNanoappLoadStarted(
      uint64_t appId,
      std::shared_ptr<const std::vector<uint8_t>> nanoappBinary) = 0;

  /**
   * Called after a nanoapp load failed.
   *
   * @param appId The app ID associated with the nanoapp binary.
   * @param success True if the nanoapp is successfully loaded to CHRE.
   */
  virtual void onNanoappLoadFailed(uint64_t appId) = 0;

  /**
   * Called after receiving a nanoapp unload response.
   *
   * @param appId The app ID associated with the nanoapp binary.
   */
  virtual void onNanoappUnloaded(uint64_t appId) = 0;
};

}  // namespace chre
}  // namespace android

#endif  // CHRE_NANOAPP_LOAD_LISTENER_H_