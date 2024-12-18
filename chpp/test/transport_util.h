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

#ifndef CHPP_TRANSPORT_UTIL_H_
#define CHPP_TRANSPORT_UTIL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "chpp/app.h"
#include "chpp/macros.h"
#include "chpp/platform/platform_link.h"
#include "chpp/transport.h"

#ifdef __cplusplus
extern "C" {
#endif

CHPP_PACKED_START
struct ChppTestResponse {
  char preamble0;
  char preamble1;
  struct ChppTransportHeader transportHeader;
  struct ChppAppHeader appHeader;
} CHPP_PACKED_ATTR;
CHPP_PACKED_END

#ifdef __cplusplus
}
#endif

namespace chpp::test {

namespace {

// Preamble as separate bytes for testing
constexpr uint8_t kChppPreamble0 = 0x68;
constexpr uint8_t kChppPreamble1 = 0x43;

}  // namespace

/************************************************
 *  Helper functions available for other tests
 ***********************************************/

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
                                 uint8_t transactionID);

/**
 * Aborts a packet and validates state.
 *
 * @param transportcontext Maintains status for each transport layer instance.
 */
void endAndValidatePacket(struct ChppTransportState *transportContext);

/**
 * Adds a preamble to a certain location in a buffer, and increases the location
 * accordingly, to account for the length of the added preamble.
 *
 * @param buf Buffer.
 * @param location Location to add the preamble, which its value will be
 * increased accordingly.
 */
void addPreambleToBuf(uint8_t *buf, size_t *location);

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
ChppTransportHeader *addTransportHeaderToBuf(uint8_t *buf, size_t *location);

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
ChppAppHeader *addAppHeaderToBuf(uint8_t *buf, size_t *location);

/**
 * Adds a transport footer to a certain location in a buffer, and increases the
 * location accordingly, to account for the length of the added preamble.
 *
 * @param buf Buffer.
 * @param location Location to add the footer. The value of location will be
 * increased accordingly.
 *
 */
void addTransportFooterToBuf(uint8_t *buf, size_t *location);

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
                 struct ChppLinuxLinkState &chppLinuxLinkContext);

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
                          struct ChppLinuxLinkState &chppLinuxLinkContext);

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
                       uint8_t *handle);

}  // namespace chpp::test

#endif  // CHPP_TRANSPORT_UTIL_H_
