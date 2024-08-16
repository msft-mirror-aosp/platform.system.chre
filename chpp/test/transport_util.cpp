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

#include "transport_util.h"

#include <gtest/gtest.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <chrono>
#include <thread>

#include "chpp/app.h"
#include "chpp/common/discovery.h"
#include "chpp/common/gnss.h"
#include "chpp/common/gnss_types.h"
#include "chpp/common/standard_uuids.h"
#include "chpp/common/wifi.h"
#include "chpp/common/wifi_types.h"
#include "chpp/common/wwan.h"
#include "chpp/crc.h"
#include "chpp/macros.h"
#include "chpp/memory.h"
#include "chpp/platform/platform_link.h"
#include "chpp/platform/utils.h"
#include "chpp/services/discovery.h"
#include "chpp/services/loopback.h"
#include "chpp/transport.h"
#include "chre/pal/wwan.h"

namespace chpp::test {

/**
 * Wait for chppTransportDoWork() to finish after it is notified by
 * chppEnqueueTxPacket to run.
 */
void WaitForTransport(struct ChppTransportState *transportContext) {
  // Start sending data out.
  cycleSendThread();
  // Wait for data to be received and processed.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Should have reset loc and length for next packet / datagram
  EXPECT_EQ(transportContext->rxStatus.locInDatagram, 0);
  EXPECT_EQ(transportContext->rxDatagram.length, 0);
}

/**
 * Validates a ChppTestResponse. Since the error field within the
 * ChppAppHeader struct is optional (and not used for common services), this
 * function returns the error field to be checked if desired, depending on the
 * service.
 *
 * @param buf Buffer containing response.
 * @param ackSeq Ack sequence to be verified.
 * @param handle Handle number to be verified
 * @param transactionID Transaction ID to be verified.
 *
 * @return The error field within the ChppAppHeader struct that is used by some
 * but not all services.
 */
uint8_t validateChppTestResponse(void *buf, uint8_t ackSeq, uint8_t handle,
                                 uint8_t transactionID) {
  struct ChppTestResponse *response = (ChppTestResponse *)buf;

  // Check preamble
  EXPECT_EQ(response->preamble0, kChppPreamble0);
  EXPECT_EQ(response->preamble1, kChppPreamble1);

  // Check response transport headers
  EXPECT_EQ(response->transportHeader.packetCode, CHPP_TRANSPORT_ERROR_NONE);
  EXPECT_EQ(response->transportHeader.ackSeq, ackSeq);

  // Check response app headers
  EXPECT_EQ(response->appHeader.handle, handle);
  EXPECT_EQ(response->appHeader.type, CHPP_MESSAGE_TYPE_SERVICE_RESPONSE);
  EXPECT_EQ(response->appHeader.transaction, transactionID);

  // Return optional response error to be checked if desired
  return response->appHeader.error;
}

/**
 * Aborts a packet and validates state.
 *
 * @param transportcontext Maintains status for each transport layer instance.
 */
void endAndValidatePacket(struct ChppTransportState *transportContext) {
  chppRxPacketCompleteCb(transportContext);
  EXPECT_EQ(transportContext->rxStatus.state, CHPP_STATE_PREAMBLE);
  EXPECT_EQ(transportContext->rxStatus.locInDatagram, 0);
  EXPECT_EQ(transportContext->rxDatagram.length, 0);
}

/**
 * Adds a preamble to a certain location in a buffer, and increases the location
 * accordingly, to account for the length of the added preamble.
 *
 * @param buf Buffer.
 * @param location Location to add the preamble, which its value will be
 * increased accordingly.
 */
void addPreambleToBuf(uint8_t *buf, size_t *location) {
  buf[(*location)++] = kChppPreamble0;
  buf[(*location)++] = kChppPreamble1;
}

/**
 * Adds a transport header (with default values) to a certain location in a
 * buffer, and increases the location accordingly, to account for the length of
 * the added transport header.
 *
 * @param buf Buffer.
 * @param location Location to add the transport header, which its value will be
 * increased accordingly.
 *
 * @return Pointer to the added transport header (e.g. to modify its fields).
 */
ChppTransportHeader *addTransportHeaderToBuf(uint8_t *buf, size_t *location) {
  size_t oldLoc = *location;

  // Default values for initial, minimum size request packet
  ChppTransportHeader transHeader = {};
  transHeader.flags = CHPP_TRANSPORT_FLAG_FINISHED_DATAGRAM;
  transHeader.packetCode = CHPP_TRANSPORT_ERROR_NONE;
  transHeader.ackSeq = 1;
  transHeader.seq = 0;
  transHeader.length = sizeof(ChppAppHeader);
  transHeader.reserved = 0;

  memcpy(&buf[*location], &transHeader, sizeof(transHeader));
  *location += sizeof(transHeader);

  return (ChppTransportHeader *)&buf[oldLoc];
}

/**
 * Adds an app header (with default values) to a certain location in a buffer,
 * and increases the location accordingly, to account for the length of the
 * added app header.
 *
 * @param buf Buffer.
 * @param location Location to add the app header, which its value will be
 * increased accordingly.
 *
 * @return Pointer to the added app header (e.g. to modify its fields).
 */
ChppAppHeader *addAppHeaderToBuf(uint8_t *buf, size_t *location) {
  size_t oldLoc = *location;

  // Default values - to be updated later as necessary
  ChppAppHeader appHeader = {};
  appHeader.handle = CHPP_HANDLE_NEGOTIATED_RANGE_START;
  appHeader.type = CHPP_MESSAGE_TYPE_CLIENT_REQUEST;
  appHeader.transaction = 0;
  appHeader.error = CHPP_APP_ERROR_NONE;
  appHeader.command = 0;

  memcpy(&buf[*location], &appHeader, sizeof(appHeader));
  *location += sizeof(appHeader);

  return (ChppAppHeader *)&buf[oldLoc];
}

/**
 * Adds a transport footer to a certain location in a buffer, and increases the
 * location accordingly, to account for the length of the added preamble.
 *
 * @param buf Buffer.
 * @param location Location to add the footer. The value of location will be
 * increased accordingly.
 *
 */
void addTransportFooterToBuf(uint8_t *buf, size_t *location) {
  uint32_t *checksum = (uint32_t *)&buf[*location];

  *checksum = chppCrc32(0, &buf[CHPP_PREAMBLE_LEN_BYTES],
                        *location - CHPP_PREAMBLE_LEN_BYTES);

  *location += sizeof(ChppTransportFooter);
}

/**
 * Opens a service and checks to make sure it was opened correctly.
 *
 * @param transportContext Transport layer context.
 * @param buf Buffer.
 * @param ackSeq Ack sequence of the packet to be sent out
 * @param seq Sequence number of the packet to be sent out.
 * @param handle Handle of the service to be opened.
 * @param transactionID Transaction ID for the open request.
 * @param command Open command.
 */
void openService(ChppTransportState *transportContext, uint8_t *buf,
                 uint8_t ackSeq, uint8_t seq, uint8_t handle,
                 uint8_t transactionID, uint16_t command,
                 struct ChppLinuxLinkState &chppLinuxLinkContext) {
  size_t len = 0;

  addPreambleToBuf(buf, &len);

  ChppTransportHeader *transHeader = addTransportHeaderToBuf(buf, &len);
  transHeader->ackSeq = ackSeq;
  transHeader->seq = seq;

  ChppAppHeader *appHeader = addAppHeaderToBuf(buf, &len);
  appHeader->handle = handle;
  appHeader->transaction = transactionID;
  appHeader->command = command;

  addTransportFooterToBuf(buf, &len);

  // Send header + payload (if any) + footer
  EXPECT_TRUE(chppRxDataCb(transportContext, buf, len));

  // Check for correct state
  uint8_t nextSeq = transHeader->seq + 1;
  EXPECT_EQ(transportContext->rxStatus.expectedSeq, nextSeq);
  EXPECT_EQ(transportContext->rxStatus.state, CHPP_STATE_PREAMBLE);

  // Wait for response
  WaitForTransport(transportContext);

  // Validate common response fields
  EXPECT_EQ(validateChppTestResponse(chppLinuxLinkContext.buf, nextSeq, handle,
                                     transactionID),
            CHPP_APP_ERROR_NONE);

  // Check response length
  EXPECT_EQ(sizeof(ChppTestResponse), CHPP_PREAMBLE_LEN_BYTES +
                                          sizeof(ChppTransportHeader) +
                                          sizeof(ChppAppHeader));
  EXPECT_EQ(transportContext->linkBufferSize,
            sizeof(ChppTestResponse) + sizeof(ChppTransportFooter));
}

/**
 * Sends a command to a service and checks for errors.
 *
 * @param transportContext Transport layer context.
 * @param buf Buffer.
 * @param ackSeq Ack sequence of the packet to be sent out
 * @param seq Sequence number of the packet to be sent out.
 * @param handle Handle of the service to be opened.
 * @param transactionID Transaction ID for the open request.
 * @param command Command to be sent.
 */
void sendCommandToService(ChppTransportState *transportContext, uint8_t *buf,
                          uint8_t ackSeq, uint8_t seq, uint8_t handle,
                          uint8_t transactionID, uint16_t command,
                          struct ChppLinuxLinkState &chppLinuxLinkContext) {
  size_t len = 0;

  addPreambleToBuf(buf, &len);

  ChppTransportHeader *transHeader = addTransportHeaderToBuf(buf, &len);
  transHeader->ackSeq = ackSeq;
  transHeader->seq = seq;

  ChppAppHeader *appHeader = addAppHeaderToBuf(buf, &len);
  appHeader->handle = handle;
  appHeader->transaction = transactionID;
  appHeader->command = command;

  addTransportFooterToBuf(buf, &len);

  // Send header + payload (if any) + footer
  EXPECT_TRUE(chppRxDataCb(transportContext, buf, len));

  // Check for correct state
  uint8_t nextSeq = transHeader->seq + 1;
  EXPECT_EQ(transportContext->rxStatus.expectedSeq, nextSeq);
  EXPECT_EQ(transportContext->rxStatus.state, CHPP_STATE_PREAMBLE);

  // Wait for response
  WaitForTransport(transportContext);

  // Validate common response fields
  EXPECT_EQ(validateChppTestResponse(chppLinuxLinkContext.buf, nextSeq, handle,
                                     transactionID),
            CHPP_APP_ERROR_NONE);
}

/**
 * Find service handle by name.
 *
 * @param appContext App context.
 * @param name Service name.
 * @param handle Output service handle if found.
 *
 * @return True if service found, false otherwise.
 */
bool findServiceHandle(ChppAppState *appContext, const char *name,
                       uint8_t *handle) {
  for (uint8_t i = 0; i < appContext->registeredServiceCount; i++) {
    if (0 == strcmp(appContext->registeredServices[i]->descriptor.name, name)) {
      *handle = appContext->registeredServiceStates[i]->handle;
      return true;
    }
  }
  return false;
}

}  // namespace chpp::test
