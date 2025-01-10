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
#pragma once

#include <cstddef>
#include <cstdint>

#include "bluetooth_socket_offload_link_callback.h"

namespace aidl::android::hardware::bluetooth::socket::impl {

/**
 * This abstract class defines the interface between the Bluetooth Socket HAL
 * and the offload domain.
 */
class BluetoothSocketOffloadLink {
 public:
  virtual ~BluetoothSocketOffloadLink() = default;
  /**
   * Initializes the link between HAL and the offload domain.
   *
   * @return true if success, otherwise false.
   */
  virtual bool initOffloadLink() = 0;

  /**
   * Sends an encoded message regarding the status of an offloaded Bluetooth
   * Socket to the offload stack.
   *
   * @return true if success, otherwise false.
   */
  virtual bool sendMessageToOffloadStack(void *data, size_t size) = 0;

  virtual void setBluetoothSocketCallback(
      BluetoothSocketOffloadLinkCallback *btSocketCallback) = 0;
};

}  // namespace aidl::android::hardware::bluetooth::socket::impl
