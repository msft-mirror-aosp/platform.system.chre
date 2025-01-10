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

#pragma once

#include <cstddef>
#include "chre_host/generated/host_messages_generated.h"

namespace aidl::android::hardware::bluetooth::socket::impl {

/**
 * The callback interface for a BluetoothSocketOffloadLink
 *
 * A Bluetooth Socket HAL should implement this interface so that the
 * BluetoothSocketOffloadLink has an API to pass messages to.
 */
class BluetoothSocketOffloadLinkCallback {
 public:
  virtual ~BluetoothSocketOffloadLinkCallback() = default;

  /**
   * This method should be called when the offload link is disconnected from
   * the HAL.
   */
  virtual void onOffloadLinkDisconnected() {}

  /**
   * This method should be called when the offload link is reconnected to the
   * HAL.
   */
  virtual void onOffloadLinkReconnected() {}

  /**
   * Handles an encoded message related to an offloaded Bluetooth Socket from
   * the offload stack.
   */
  virtual void handleMessageFromOffloadStack(const void *message,
                                             size_t length) = 0;
};

}  // namespace aidl::android::hardware::bluetooth::socket::impl
