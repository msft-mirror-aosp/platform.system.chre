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

namespace android::hardware::bluetooth::socket::common::implementation {

/**
 * The callback interface for handling BT Socket messages from a ChreConnection.
 *
 * A BT Socket HAL should implement this interface so that the ChreConnection
 * has an API to pass messages to.
 */
class BluetoothSocketConnectionCallback {
 public:
  virtual ~BluetoothSocketConnectionCallback() = default;

  virtual void handleBtSocketOpenResponse(
      const ::chre::fbs::BtSocketOpenResponseT &response) = 0;

  virtual void handleBtSocketClose(
      const ::chre::fbs::BtSocketCloseT &message) = 0;
};

}  // namespace android::hardware::bluetooth::socket::common::implementation
