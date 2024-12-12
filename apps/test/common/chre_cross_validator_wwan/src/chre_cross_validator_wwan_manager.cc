/* * Copyright (C) 2024 The Android Open Source Project
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

#include "chre_cross_validator_wwan_manager.h"

#include <stdio.h>
#include <algorithm>
#include <cinttypes>
#include <cstring>

#include "chre/util/nanoapp/assert.h"
#include "chre/util/nanoapp/callbacks.h"
#include "chre/util/nanoapp/log.h"
#include "chre_api/chre.h"
#include "chre_api/chre/wwan.h"
#include "chre_cross_validation_wwan.nanopb.h"
#include "chre_test_common.nanopb.h"
#include "send_message.h"

namespace chre {

namespace cross_validator_wwan {

void Manager::handleEvent(uint32_t senderInstanceId, uint16_t eventType,
                          const void *eventData) {
  switch (eventType) {
    case CHRE_EVENT_MESSAGE_FROM_HOST:
      handleMessageFromHost(
          senderInstanceId,
          static_cast<const chreMessageFromHostData *>(eventData));
      break;
    case CHRE_EVENT_WWAN_CELL_INFO_RESULT:
      handleWwanCellInfoResult(
          static_cast<const chreWwanCellInfoResult *>(eventData));
      break;
    default:
      LOGE("Unknown message type %" PRIu16 "received when handling event",
           eventType);
  }
}

void Manager::handleMessageFromHost(uint32_t senderInstanceId,
                                    const chreMessageFromHostData *hostData) {
  LOGI("Received message from host");
  if (senderInstanceId != CHRE_INSTANCE_ID) {
    LOGE("Incorrect sender instance id: %" PRIu32, senderInstanceId);
    return;
  }

  switch (hostData->messageType) {
    case chre_cross_validation_wwan_MessageType_WWAN_CAPABILITIES_REQUEST:
      mHostEndpoint = hostData->hostEndpoint;
      sendCapabilitiesToHost();
      break;
    case chre_cross_validation_wwan_MessageType_WWAN_CELL_INFO_REQUEST:
      LOGI("Received WWAN_CELL_INFO_REQUEST, calling chreWwanGetCellInfoAsync");
      if (!chreWwanGetCellInfoAsync(/*cookie=*/nullptr)) {
        LOGE("chreWwanGetCellInfoAsync() failed");
        test_shared::sendTestResultWithMsgToHost(
            mHostEndpoint,
            chre_cross_validation_wwan_MessageType_WWAN_NANOAPP_ERROR,
            /*success=*/false,
            /*errMessage=*/"chreWwanGetCellInfoAsync failed",
            /*abortOnFailure=*/false);
      }
      break;
    default:
      LOGE("Unknown message type %" PRIu32 " for host message",
           hostData->messageType);
      break;
  }
}

void Manager::sendCapabilitiesToHost() {
  LOGI("Sending capabilites to host");
  chre_cross_validation_wwan_WwanCapabilities wwanCapabilities =
      makeWwanCapabilitiesMessage(chreWwanGetCapabilities());
  test_shared::sendMessageToHost(
      mHostEndpoint, &wwanCapabilities,
      chre_cross_validation_wwan_WwanCapabilities_fields,
      chre_cross_validation_wwan_MessageType_WWAN_CAPABILITIES);
}

void Manager::handleWwanCellInfoResult(const chreWwanCellInfoResult *event) {
  chre_cross_validation_wwan_WwanCellInfoResult result =
      chre_cross_validation_wwan_WwanCellInfoResult_init_default;

  if (event->errorCode != CHRE_ERROR_NONE) {
    LOGE("chreWwanCellInfoResult received with errorCode: 0x%" PRIu8,
         event->errorCode);
  }

  result.has_errorCode = true;
  result.errorCode = event->errorCode;
  result.has_cellInfoCount = true;
  result.cellInfoCount = event->cellInfoCount;

  LOGI("Sending wwan scan results to host");
  test_shared::sendMessageToHostWithPermissions(
      mHostEndpoint, &result,
      chre_cross_validation_wwan_WwanCellInfoResult_fields,
      chre_cross_validation_wwan_MessageType_WWAN_CELL_INFO_RESULTS,
      NanoappPermissions::CHRE_PERMS_WWAN);
}

chre_cross_validation_wwan_WwanCapabilities
Manager::makeWwanCapabilitiesMessage(uint32_t capabilitiesFromChre) {
  chre_cross_validation_wwan_WwanCapabilities capabilities;
  capabilities.has_wwanCapabilities = true;
  capabilities.wwanCapabilities = capabilitiesFromChre;
  return capabilities;
}

}  // namespace cross_validator_wwan

}  // namespace chre
