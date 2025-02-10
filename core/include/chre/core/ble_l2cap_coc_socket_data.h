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

#include <cstdint>

/**
 * Parameters for a direction of packet flow in an LE L2CAP COC socket.
 */
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

/**
 * Data for the offloaded LE L2CAP COC socket.
 */
struct BleL2capCocSocketData {
  //! Unique identifier for this socket connection. This ID in the offload
  //! domain matches the ID used on the host side. It is valid only while the
  //! socket is connected.
  uint64_t socketId;

  //! The ID of the Hub endpoint for hardware offload data path.
  uint64_t endpointId;

  //! ACL connection handle for the socket.
  uint16_t connectionHandle;

  //! The originating or destination client ID on the host side, used to direct
  //! responses only to the client that sent the request.
  uint16_t hostClientId;

  L2capCocConfig rxConfig;

  L2capCocConfig txConfig;
};
