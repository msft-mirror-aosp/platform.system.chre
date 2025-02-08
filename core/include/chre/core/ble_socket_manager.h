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

#include "chre/core/ble_l2cap_coc_socket_data.h"
#include "chre_api/chre.h"

namespace chre {

/**
 * Manages offloaded BLE sockets. Handles sending packets between nanoapps and
 * BLE sockets.
 */
class BleSocketManager : public NonCopyable {
 public:
  chreError socketConnected(const BleL2capCocSocketData & /* socketData */) {
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
