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
#include "bluetooth_socket_connection_callback.h"
#include "chre_host/generated/host_messages_generated.h"
#include "hal_chre_socket_connection.h"

namespace aidl::android::hardware::bluetooth::socket::impl {

/**
 * The base class of BT Socket HAL.
 *
 * A subclass should initiate mConnection.
 */
class BluetoothSocketBase
    : public BnBluetoothSocket,
      public ::android::hardware::bluetooth::socket::common::implementation::
          BluetoothSocketConnectionCallback {
 public:
  // Functions implementing IBluetoothSocket.
  ndk::ScopedAStatus registerCallback(
      const std::shared_ptr<IBluetoothSocketCallback> &callback) override;
  ndk::ScopedAStatus getSocketCapabilities(SocketCapabilities *result) override;
  ndk::ScopedAStatus opened(const SocketContext &context) override;
  ndk::ScopedAStatus closed(int64_t socketId) override;

  // Functions implementing BluetoothSocketConnectionCallback.
  void handleBtSocketOpenResponse(
      const ::chre::fbs::BtSocketOpenResponseT &response) override;
  void handleBtSocketClose(const ::chre::fbs::BtSocketCloseT &message) override;

 protected:
  std::shared_ptr<::android::hardware::contexthub::common::implementation::
                      HalChreSocketConnection>
      mConnection;
  std::shared_ptr<IBluetoothSocketCallback> mCallback{};

 private:
  void sendOpenedCompleteMessage(int64_t socketId, Status status,
                                 std::string reason);
};

}  // namespace aidl::android::hardware::bluetooth::socket::impl
