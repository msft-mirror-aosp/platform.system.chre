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

#include <atomic>
#include <cstdint>
#include <future>

#include "aidl/android/hardware/bluetooth/socket/BnBluetoothSocket.h"
#include "bluetooth_socket_offload_link.h"
#include "bluetooth_socket_offload_link_callback.h"
#include "chre_host/generated/host_messages_generated.h"

namespace aidl::android::hardware::bluetooth::socket::impl {

/**
 * Implementationof the BT Socket HAL using flatbuffer encoding and decoding for
 * offload messages.
 */
class BluetoothSocketFbsHal : public BnBluetoothSocket,
                              public BluetoothSocketOffloadLinkCallback {
 public:
  BluetoothSocketFbsHal(std::shared_ptr<BluetoothSocketOffloadLink> offloadLink)
      : mOffloadLink(offloadLink) {
    mOffloadLink->setBluetoothSocketCallback(this);
  }

  // Functions implementing IBluetoothSocket.
  ndk::ScopedAStatus registerCallback(
      const std::shared_ptr<IBluetoothSocketCallback> &callback) override;
  ndk::ScopedAStatus getSocketCapabilities(SocketCapabilities *result) override;
  ndk::ScopedAStatus opened(const SocketContext &context) override;
  ndk::ScopedAStatus closed(int64_t socketId) override;

  // Functions implementing BluetoothSocketOffloadLinkCallback.
  void onOffloadLinkDisconnected() override {
    mOffloadLinkAvailable = false;
  }
  void onOffloadLinkReconnected() override {
    mOffloadLinkAvailable = true;
  }
  void handleMessageFromOffloadStack(const void *message,
                                     size_t length) override;

 private:
  std::shared_ptr<BluetoothSocketOffloadLink> mOffloadLink;

  std::shared_ptr<IBluetoothSocketCallback> mCallback{};

  // A thread safe flag indicating if the offload link is ready for operations.
  // Outside of the constructor, this boolean flag should only be written by
  // onOffloadLinkDisconnected and onOffloadLinkReconnected, the order of which
  // should be guaranteed by the BT Socket offload link's disconnection handler.
  std::atomic_bool mOffloadLinkAvailable = true;

  // A promise that is set when getSocketCapabilities is called and is fulfilled
  // when a response is received from the offload stack.
  std::promise<SocketCapabilities> mCapabilitiesPromise;

  void sendOpenedCompleteMessage(int64_t socketId, Status status,
                                 std::string reason);

  void handleBtSocketOpenResponse(
      const ::chre::fbs::BtSocketOpenResponseT &response);

  void handleBtSocketClose(const ::chre::fbs::BtSocketCloseT &message);

  void handleBtSocketCapabilitiesResponse(
      const ::chre::fbs::BtSocketCapabilitiesResponseT &response);
};

}  // namespace aidl::android::hardware::bluetooth::socket::impl
