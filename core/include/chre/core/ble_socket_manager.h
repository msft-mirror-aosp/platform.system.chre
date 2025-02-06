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

#ifdef CHRE_BLE_SOCKET_SUPPORT_ENABLED

#include "chre_api/chre.h"

namespace chre {

/**
 * Manages offloaded BLE sockets. Handles sending packets between nanoapps and
 * BLE sockets.
 */
class BleSocketManager : public NonCopyable {
 public:
  struct L2capCocConfig {
    //! Channel identifier of the endpoint.
    uint16_t cid;

    //! Maximum Transmission Unit.
    uint16_t mtu;

    //! Maximum PDU payload Size.
    uint16_t mps;

    //! Currently available credits for sending or receiving K-frames in LE
    //! Credit Based Flow Control mode.
    uint16_t credits;
  };

  chreError socketConnected(uint16_t /*hostClientId*/, uint64_t /*socketId*/,
                            uint64_t /*endpointId*/,
                            uint16_t /*connectionHandle*/,
                            L2capCocConfig /*rxConfig*/,
                            L2capCocConfig /*txConfig*/) {
    return CHRE_ERROR_NOT_SUPPORTED;
  }

  bool acceptBleSocket(uint64_t /*socketId*/) {
    return false;
  }

  int32_t sendBleSocketPacket(
      uint64_t /*socketId*/, const void * /*data*/, uint16_t /*length*/,
      chreBleSocketPacketFreeFunction * /*freeCallback*/) {
    return CHRE_ERROR_NOT_SUPPORTED;
  }
};

}  // namespace chre

#endif  // CHRE_BLE_SOCKET_SUPPORT_ENABLED
