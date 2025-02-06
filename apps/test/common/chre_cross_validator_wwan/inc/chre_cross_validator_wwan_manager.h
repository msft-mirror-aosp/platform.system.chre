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

#ifndef CHRE_CROSS_VALIDATOR_WWAN_MANAGER_H_
#define CHRE_CROSS_VALIDATOR_WWAN_MANAGER_H_

#include <pb_common.h>
#include <pb_decode.h>
#include <pb_encode.h>

#include <cinttypes>
#include <cstdint>

#include "chre/util/dynamic_vector.h"
#include "chre/util/singleton.h"
#include "chre_api/chre.h"
#include "chre_api/chre/wwan.h"
#include "chre_cross_validation_wwan.nanopb.h"
#include "chre_test_common.nanopb.h"

namespace chre::cross_validator_wwan {

/**
 * Class to manage a CHRE cross validator wwan nanoapp.
 */
class Manager {
 public:
  /**
   * Handle a CHRE event.
   *
   * @param senderInstanceId The instand ID that sent the event.
   * @param eventType The type of the event.
   * @param eventData The data for the event.
   */
  void handleEvent(uint32_t senderInstanceId, uint16_t eventType,
                   const void *eventData);

 private:
  /**
   * Handle a message from the host.
   * @param senderInstanceId The instance id of the sender.
   * @param data The message from the host's data.
   */
  void handleMessageFromHost(uint32_t senderInstanceId,
                             const chreMessageFromHostData *data);

  /*
   * Send the capabilities of the wwan to the host.
   */
  void sendCapabilitiesToHost();

  /**
   * @param capabilitiesFromChre The number with flags that represent the
   *        different wwan capabilities.
   * @return The wwan capabilities proto message for the host.
   */
  chre_cross_validation_wwan_WwanCapabilities makeWwanCapabilitiesMessage(
      uint32_t capabilitiesFromChre);

  /**
   * Handle a wwan cell info result event from a CHRE event.
   *
   * @param event the wwan cell info event from CHRE api.
   */
  void handleWwanCellInfoResult(const chreWwanCellInfoResult *event);

  // Helper functions to convert CHRE results to proto
  chre_cross_validation_wwan_WwanCellInfoResult toWwanCellInfoResultProto(
      const chreWwanCellInfoResult &cell_info_result);

  // Host endpoint for sending responses. Updated on message receipt.
  uint16_t mHostEndpoint = 0;
};

// The chre cross validator manager singleton.
typedef chre::Singleton<Manager> ManagerSingleton;

}  // namespace chre::cross_validator_wwan

#endif  // CHRE_CROSS_VALIDATOR_WWAN_MANAGER_H_
