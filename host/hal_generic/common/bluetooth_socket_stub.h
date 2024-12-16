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

#include <cstdint>

#include "aidl/android/hardware/bluetooth/socket/BnBluetoothSocket.h"

namespace aidl::android::hardware::bluetooth::socket::impl {

/**
 * The stub implementation of the BT Socket HAL.
 */
class BluetoothSocketStub : public BnBluetoothSocket {
 public:
  // Functions implementing IBluetoothSocket.
  ndk::ScopedAStatus registerCallback(
      const std::shared_ptr<IBluetoothSocketCallback> &) override {
    return ndk::ScopedAStatus::ok();
  }

  ndk::ScopedAStatus getSocketCapabilities(
      SocketCapabilities *result) override {
    result->leCocCapabilities.numberOfSupportedSockets = 0;
    result->leCocCapabilities.mtu = 1000;
    result->rfcommCapabilities.numberOfSupportedSockets = 0;
    result->rfcommCapabilities.maxFrameSize = 23;
    return ndk::ScopedAStatus::ok();
  }

  ndk::ScopedAStatus opened(const SocketContext &) override {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
  }

  ndk::ScopedAStatus closed(int64_t) override {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
  }
};

}  // namespace aidl::android::hardware::bluetooth::socket::impl
