/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "transport_test.h"

#include <gtest/gtest.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <thread>

#include "chpp/app.h"
#include "chpp/common/discovery.h"
#include "chpp/common/gnss.h"
#include "chpp/common/standard_uuids.h"
#include "chpp/common/wifi.h"
#include "chpp/common/wwan.h"
#include "chpp/macros.h"
#include "chpp/memory.h"
#include "chpp/services/discovery.h"
#include "chpp/services/gnss_types.h"
#include "chpp/services/wifi_types.h"
#include "chpp/transport.h"
#include "chre/pal/wwan.h"

namespace {

// Preamble as separate bytes for testing
constexpr uint8_t kChppPreamble0 = 0x68;
constexpr uint8_t kChppPreamble1 = 0x43;

// Max size of payload sent to chppRxDataCb (bytes)
constexpr size_t kMaxChunkSize = 20000;

constexpr size_t kMaxPacketSize = kMaxChunkSize + CHPP_PREAMBLE_LEN_BYTES +
                                  sizeof(ChppTransportHeader) +
                                  sizeof(ChppTransportFooter);

// Input sizes to test the entire range of sizes with a few tests
constexpr int kChunkSizes[] = {0,  1,   2,   3,    4,     5,    6,
                               7,  8,   10,  16,   20,    30,   40,
                               51, 100, 201, 1000, 10001, 20000};

// Number of services
constexpr int kServiceCount = 3;

// Basic response minimum packet length
constexpr int kMinResponsePacketLength =
    CHPP_PREAMBLE_LEN_BYTES + sizeof(ChppTransportHeader) +
    sizeof(ChppAppHeader) + sizeof(ChppTransportFooter);

/*
 * Test suite for the CHPP Transport Layer
 */
class TransportTests : public testing::TestWithParam<int> {
 protected:
  void SetUp() override {
    memset(&mTransportContext.linkParams, 0,
           sizeof(mTransportContext.linkParams));
    mTransportContext.linkParams.linkEstablished = true;
    chppTransportInit(&mTransportContext, &mAppContext);
    chppAppInit(&mAppContext, &mTransportContext);

    mTransportContext.resetState = CHPP_RESET_STATE_NONE;

    // Make sure CHPP has a correct count of the number of registered services
    // on this platform, (in this case, 1,) as registered in the function
    // chppRegisterCommonServices().
    ASSERT_EQ(mAppContext.registeredServiceCount, kServiceCount);
  }

  void TearDown() override {
    chppAppDeinit(&mAppContext);
    chppTransportDeinit(&mTransportContext);
  }

  ChppTransportState mTransportContext = {};
  ChppAppState mAppContext = {};
  uint8_t mBuf[kMaxPacketSize] = {};
};

/**
 * Wait for chppTransportDoWork() to finish after it is notified by
 * chppEnqueueTxPacket to run.
 *
 * TODO: Explore better ways to synchronize test with transport
 */
void WaitForTransport(struct ChppTransportState *transportContext) {
  volatile uint16_t k = 1;
  while (transportContext->txStatus.hasPacketsToSend || k > 0) {
    k++;
  }
  ASSERT_FALSE(transportContext->txStatus.hasPacketsToSend);  // timeout

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

  static const ChppTransportHeader transHeader = {
      // Default values for initial, minimum size request packet
      .flags = CHPP_TRANSPORT_FLAG_FINISHED_DATAGRAM,
      .packetCode = CHPP_TRANSPORT_ERROR_NONE,
      .ackSeq = 1,
      .seq = 0,
      .length = sizeof(ChppAppHeader),
  };

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

  static const ChppAppHeader appHeader = {
      // Default values - to be updated later as necessary
      .handle = CHPP_HANDLE_NEGOTIATED_RANGE_START,
      .type = CHPP_MESSAGE_TYPE_CLIENT_REQUEST,
      .transaction = 0,
      .command = 0,
      .error = CHPP_APP_ERROR_NONE,
  };

  memcpy(&buf[*location], &appHeader, sizeof(appHeader));
  *location += sizeof(appHeader);

  return (ChppAppHeader *)&buf[oldLoc];
}

/**
 * Adds a transport footer to a certain location in a buffer, and increases the
 * location accordingly, to account for the length of the added preamble.
 *
 * TODO: calculate checksum for the footer.
 *
 * @param buf Buffer.
 * @param location Location to add the preamble, which its value will be
 * increased accordingly.
 */
void addTransportFooterToBuf(uint8_t *buf, size_t *location) {
  // TODO: add checksum
  UNUSED_VAR(buf);

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
                 uint8_t transactionID, uint16_t command) {
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
  EXPECT_EQ(validateChppTestResponse(transportContext->pendingTxPacket.payload,
                                     nextSeq, handle, transactionID),
            CHPP_APP_ERROR_NONE);

  // Check response length
  EXPECT_EQ(sizeof(ChppTestResponse), CHPP_PREAMBLE_LEN_BYTES +
                                          sizeof(ChppTransportHeader) +
                                          sizeof(ChppAppHeader));
  EXPECT_EQ(transportContext->pendingTxPacket.length,
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
                          uint8_t transactionID, uint16_t command) {
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
  EXPECT_EQ(validateChppTestResponse(transportContext->pendingTxPacket.payload,
                                     nextSeq, handle, transactionID),
            CHPP_APP_ERROR_NONE);
}

/**
 * A series of zeros shouldn't change state from CHPP_STATE_PREAMBLE
 */
TEST_P(TransportTests, ZeroNoPreambleInput) {
  size_t len = static_cast<size_t>(GetParam());
  if (len <= kMaxChunkSize) {
    EXPECT_TRUE(chppRxDataCb(&mTransportContext, mBuf, len));
    EXPECT_EQ(mTransportContext.rxStatus.state, CHPP_STATE_PREAMBLE);
  }
}

/**
 * A preamble after a series of zeros input should change state from
 * CHPP_STATE_PREAMBLE to CHPP_STATE_HEADER
 */
TEST_P(TransportTests, ZeroThenPreambleInput) {
  size_t len = static_cast<size_t>(GetParam());

  if (len <= kMaxChunkSize) {
    // Add preamble at the end of mBuf, as individual bytes instead of using
    // chppAddPreamble(&mBuf[preambleLoc])
    size_t preambleLoc = MAX(0, len - CHPP_PREAMBLE_LEN_BYTES);
    mBuf[preambleLoc] = kChppPreamble0;
    mBuf[preambleLoc + 1] = kChppPreamble1;

    if (len >= CHPP_PREAMBLE_LEN_BYTES) {
      EXPECT_FALSE(chppRxDataCb(&mTransportContext, mBuf, len));
      EXPECT_EQ(mTransportContext.rxStatus.state, CHPP_STATE_HEADER);
    } else {
      EXPECT_TRUE(chppRxDataCb(&mTransportContext, mBuf, len));
      EXPECT_EQ(mTransportContext.rxStatus.state, CHPP_STATE_PREAMBLE);
    }
  }
}

/**
 * Rx Testing with various length payloads of zeros
 */
TEST_P(TransportTests, RxPayloadOfZeros) {
  mTransportContext.rxStatus.state = CHPP_STATE_HEADER;
  size_t len = static_cast<size_t>(GetParam());

  mTransportContext.txStatus.hasPacketsToSend = true;
  std::thread t1(chppWorkThreadStart, &mTransportContext);
  WaitForTransport(&mTransportContext);

  if (len <= kMaxChunkSize) {
    size_t loc = 0;
    ChppTransportHeader *transHeader = addTransportHeaderToBuf(mBuf, &loc);
    transHeader->length = len;

    // Send header and check for correct state
    EXPECT_FALSE(
        chppRxDataCb(&mTransportContext, mBuf, sizeof(ChppTransportHeader)));
    if (len > 0) {
      EXPECT_EQ(mTransportContext.rxStatus.state, CHPP_STATE_PAYLOAD);
    } else {
      EXPECT_EQ(mTransportContext.rxStatus.state, CHPP_STATE_FOOTER);
    }

    // Correct decoding of packet length
    EXPECT_EQ(mTransportContext.rxHeader.length, len);
    EXPECT_EQ(mTransportContext.rxStatus.locInDatagram, 0);
    EXPECT_EQ(mTransportContext.rxDatagram.length, len);

    // Send payload if any and check for correct state
    if (len > 0) {
      EXPECT_FALSE(chppRxDataCb(&mTransportContext,
                                &mBuf[sizeof(ChppTransportHeader)], len));
      EXPECT_EQ(mTransportContext.rxStatus.state, CHPP_STATE_FOOTER);
    }

    // Should have complete packet payload by now
    EXPECT_EQ(mTransportContext.rxStatus.locInDatagram, len);

    // But no ACK yet
    EXPECT_EQ(mTransportContext.rxStatus.expectedSeq, transHeader->seq);

    // Send footer
    EXPECT_TRUE(chppRxDataCb(&mTransportContext,
                             &mBuf[sizeof(ChppTransportHeader) + len],
                             sizeof(ChppTransportFooter)));

    // The next expected packet sequence # should incremented only if the
    // received packet is payload-bearing.
    uint8_t nextSeq = transHeader->seq + ((len > 0) ? 1 : 0);
    EXPECT_EQ(mTransportContext.rxStatus.expectedSeq, nextSeq);

    // Check for correct ACK crafting if applicable (i.e. if the received packet
    // is payload-bearing).
    if (len > 0) {
      // TODO: Remove later as can cause flaky tests
      // These are expected to change shortly afterwards, as chppTransportDoWork
      // is run
      // EXPECT_TRUE(mTransportContext.txStatus.hasPacketsToSend);
      EXPECT_EQ(mTransportContext.txStatus.packetCodeToSend,
                CHPP_TRANSPORT_ERROR_NONE);
      EXPECT_EQ(mTransportContext.txDatagramQueue.pending, 0);

      WaitForTransport(&mTransportContext);

      // Check response packet fields
      struct ChppTransportHeader *txHeader =
          (struct ChppTransportHeader *)&mTransportContext.pendingTxPacket
              .payload[CHPP_PREAMBLE_LEN_BYTES];
      EXPECT_EQ(txHeader->flags, CHPP_TRANSPORT_FLAG_FINISHED_DATAGRAM);
      EXPECT_EQ(txHeader->packetCode, CHPP_TRANSPORT_ERROR_NONE);
      EXPECT_EQ(txHeader->ackSeq, nextSeq);
      EXPECT_EQ(txHeader->length, 0);

      // Check outgoing packet length
      EXPECT_EQ(mTransportContext.pendingTxPacket.length,
                CHPP_PREAMBLE_LEN_BYTES + sizeof(struct ChppTransportHeader) +
                    sizeof(struct ChppTransportFooter));
    }

    // Check for correct state
    EXPECT_EQ(mTransportContext.rxStatus.state, CHPP_STATE_PREAMBLE);

    // Should have reset loc and length for next packet / datagram
    EXPECT_EQ(mTransportContext.rxStatus.locInDatagram, 0);
    EXPECT_EQ(mTransportContext.rxDatagram.length, 0);
  }

  chppWorkThreadStop(&mTransportContext);
  t1.join();
}

TEST_P(TransportTests, EnqueueDatagrams) {
  size_t len = static_cast<size_t>(GetParam());

  if (len <= CHPP_TX_DATAGRAM_QUEUE_LEN) {
    // Add (len) datagrams of various length to queue

    int fr = 0;

    for (int j = 0; j == CHPP_TX_DATAGRAM_QUEUE_LEN; j++) {
      for (size_t i = 1; i <= len; i++) {
        uint8_t *mBuf = (uint8_t *)chppMalloc(i + 100);
        EXPECT_TRUE(
            chppEnqueueTxDatagramOrFail(&mTransportContext, mBuf, i + 100));

        EXPECT_EQ(mTransportContext.txDatagramQueue.pending, i);
        EXPECT_EQ(mTransportContext.txDatagramQueue.front, fr);
        EXPECT_EQ(mTransportContext.txDatagramQueue
                      .datagram[(i - 1 + fr) % CHPP_TX_DATAGRAM_QUEUE_LEN]
                      .length,
                  i + 100);
      }

      if (mTransportContext.txDatagramQueue.pending ==
          CHPP_TX_DATAGRAM_QUEUE_LEN) {
        uint8_t *mBuf = (uint8_t *)chppMalloc(100);
        EXPECT_FALSE(
            chppEnqueueTxDatagramOrFail(&mTransportContext, mBuf, 100));
        CHPP_FREE_AND_NULLIFY(mBuf);
      }

      for (size_t i = len; i > 0; i--) {
        fr++;
        fr %= CHPP_TX_DATAGRAM_QUEUE_LEN;

        EXPECT_TRUE(chppDequeueTxDatagram(&mTransportContext));

        EXPECT_EQ(mTransportContext.txDatagramQueue.front, fr);
        EXPECT_EQ(mTransportContext.txDatagramQueue.pending, i - 1);
      }

      EXPECT_FALSE(chppDequeueTxDatagram(&mTransportContext));

      EXPECT_EQ(mTransportContext.txDatagramQueue.front, fr);
      EXPECT_EQ(mTransportContext.txDatagramQueue.pending, 0);
    }
  }
}

/**
 * Loopback testing with various length payloads of zeros
 */
TEST_P(TransportTests, LoopbackPayloadOfZeros) {
  mTransportContext.rxStatus.state = CHPP_STATE_HEADER;
  size_t len = static_cast<size_t>(GetParam());

  mTransportContext.txStatus.hasPacketsToSend = true;
  std::thread t1(chppWorkThreadStart, &mTransportContext);
  WaitForTransport(&mTransportContext);

  if (len <= kMaxChunkSize) {
    size_t loc = 0;
    ChppTransportHeader *transHeader = addTransportHeaderToBuf(mBuf, &loc);
    transHeader->length = len;

    // Loopback App header (only 2 fields required)
    mBuf[sizeof(ChppTransportHeader)] = CHPP_HANDLE_LOOPBACK;
    mBuf[sizeof(ChppTransportHeader) + 1] = CHPP_MESSAGE_TYPE_CLIENT_REQUEST;

    // TODO: Add checksum

    // Send header + payload (if any) + footer
    EXPECT_TRUE(chppRxDataCb(
        &mTransportContext, mBuf,
        sizeof(ChppTransportHeader) + len + sizeof(ChppTransportFooter)));

    // Check for correct state
    EXPECT_EQ(mTransportContext.rxStatus.state, CHPP_STATE_PREAMBLE);

    // The next expected packet sequence # should incremented only if the
    // received packet is payload-bearing.
    uint8_t nextSeq = transHeader->seq + ((len > 0) ? 1 : 0);
    EXPECT_EQ(mTransportContext.rxStatus.expectedSeq, nextSeq);

    WaitForTransport(&mTransportContext);

    // Check for correct response packet crafting if applicable
    if (len > 0) {
      // Check response packet fields
      struct ChppTransportHeader *txHeader =
          (struct ChppTransportHeader *)&mTransportContext.pendingTxPacket
              .payload[CHPP_PREAMBLE_LEN_BYTES];

      // If datagram is larger than Tx MTU, the response packet should be the
      // first fragment
      size_t mtu_len = MIN(len, CHPP_TRANSPORT_TX_MTU_BYTES);
      uint8_t flags = (mtu_len == len)
                          ? CHPP_TRANSPORT_FLAG_FINISHED_DATAGRAM
                          : CHPP_TRANSPORT_FLAG_UNFINISHED_DATAGRAM;

      // Correct loopback command requires min of 2 bytes payload
      if (len < 2) {
        mtu_len = 0;
      }

      // Check response packet parameters
      EXPECT_EQ(txHeader->flags, flags);
      EXPECT_EQ(txHeader->packetCode, CHPP_TRANSPORT_ERROR_NONE);
      EXPECT_EQ(txHeader->ackSeq, nextSeq);
      EXPECT_EQ(txHeader->length, mtu_len);

      // Check response packet length
      EXPECT_EQ(mTransportContext.pendingTxPacket.length,
                CHPP_PREAMBLE_LEN_BYTES + sizeof(struct ChppTransportHeader) +
                    mtu_len + sizeof(struct ChppTransportFooter));

      // Check response packet payload
      if (len >= 2) {
        EXPECT_EQ(mTransportContext.pendingTxPacket
                      .payload[CHPP_PREAMBLE_LEN_BYTES +
                               sizeof(struct ChppTransportHeader)],
                  CHPP_HANDLE_LOOPBACK);
        EXPECT_EQ(mTransportContext.pendingTxPacket
                      .payload[CHPP_PREAMBLE_LEN_BYTES +
                               sizeof(struct ChppTransportHeader) + 1],
                  CHPP_MESSAGE_TYPE_SERVICE_RESPONSE);
      }
    }

    // Should have reset loc and length for next packet / datagram
    EXPECT_EQ(mTransportContext.rxStatus.locInDatagram, 0);
    EXPECT_EQ(mTransportContext.rxDatagram.length, 0);
  }

  chppWorkThreadStop(&mTransportContext);
  t1.join();
}

/**
 * Discovery service + Transaction ID
 */
TEST_P(TransportTests, DiscoveryService) {
  uint8_t transactionID = static_cast<size_t>(GetParam());
  size_t len = 0;

  mTransportContext.txStatus.hasPacketsToSend = true;
  std::thread t1(chppWorkThreadStart, &mTransportContext);
  WaitForTransport(&mTransportContext);

  addPreambleToBuf(mBuf, &len);

  ChppTransportHeader *transHeader = addTransportHeaderToBuf(mBuf, &len);

  ChppAppHeader *appHeader = addAppHeaderToBuf(mBuf, &len);
  appHeader->handle = CHPP_HANDLE_DISCOVERY;
  appHeader->transaction = transactionID;
  appHeader->command = CHPP_DISCOVERY_COMMAND_DISCOVER_ALL;

  addTransportFooterToBuf(mBuf, &len);

  // Send header + payload (if any) + footer
  EXPECT_TRUE(chppRxDataCb(&mTransportContext, mBuf, len));

  // Check for correct state
  uint8_t nextSeq = transHeader->seq + 1;
  EXPECT_EQ(mTransportContext.rxStatus.expectedSeq, nextSeq);
  EXPECT_EQ(mTransportContext.rxStatus.state, CHPP_STATE_PREAMBLE);

  // Wait for response
  WaitForTransport(&mTransportContext);

  // Validate response
  validateChppTestResponse(mTransportContext.pendingTxPacket.payload, nextSeq,
                           CHPP_HANDLE_DISCOVERY, transactionID);
  size_t responseLoc = sizeof(ChppTestResponse);

  // Decode discovery response
  ChppServiceDescriptor *services = (ChppServiceDescriptor *)&mTransportContext
                                        .pendingTxPacket.payload[responseLoc];
  responseLoc += kServiceCount * sizeof(ChppServiceDescriptor);

  // TODO: Verify checksum
  responseLoc += sizeof(ChppTransportFooter);

  // Check total length (and implicit service count)
  EXPECT_EQ(responseLoc, kMinResponsePacketLength +
                             kServiceCount * sizeof(ChppServiceDescriptor));
  EXPECT_EQ(mTransportContext.pendingTxPacket.length, responseLoc);

  // Check service configuration response
  static const ChppServiceDescriptor wwanServiceDescriptor = {
      .uuid = CHPP_UUID_WWAN_STANDARD,

      // Human-readable name
      .name = "WWAN",

      // Version
      .version.major = 1,
      .version.minor = 0,
      .version.patch = 0,
  };
  EXPECT_EQ(std::memcmp(services[0].uuid, wwanServiceDescriptor.uuid,
                        sizeof(wwanServiceDescriptor.uuid)),
            0);
  EXPECT_EQ(std::memcmp(services[0].name, wwanServiceDescriptor.name,
                        sizeof(wwanServiceDescriptor.name)),
            0);
  EXPECT_EQ(services[0].version.major, wwanServiceDescriptor.version.major);
  EXPECT_EQ(services[0].version.minor, wwanServiceDescriptor.version.minor);
  EXPECT_EQ(services[0].version.patch, wwanServiceDescriptor.version.patch);

  // Cleanup
  chppWorkThreadStop(&mTransportContext);
  t1.join();
}

/**
 * WWAN service Open and GetCapabilities.
 */
TEST_F(TransportTests, WwanOpen) {
  mTransportContext.txStatus.hasPacketsToSend = true;
  std::thread t1(chppWorkThreadStart, &mTransportContext);
  WaitForTransport(&mTransportContext);

  uint8_t ackSeq = 1;
  uint8_t seq = 0;
  uint8_t handle = CHPP_HANDLE_NEGOTIATED_RANGE_START;
  uint8_t transactionID = 0;
  size_t len = 0;

  openService(&mTransportContext, mBuf, ackSeq++, seq++, handle,
              transactionID++, CHPP_WWAN_OPEN);

  addPreambleToBuf(mBuf, &len);

  uint16_t command = CHPP_WWAN_GET_CAPABILITIES;
  sendCommandToService(&mTransportContext, mBuf, ackSeq++, seq++, handle,
                       transactionID++, command);

  size_t responseLoc = sizeof(ChppTestResponse);

  // Validate capabilities
  uint32_t *capabilities =
      (uint32_t *)&mTransportContext.pendingTxPacket.payload[responseLoc];
  responseLoc += sizeof(uint32_t);

  EXPECT_EQ(*capabilities, CHRE_WWAN_GET_CELL_INFO);

  // Check total length
  EXPECT_EQ(responseLoc, CHPP_PREAMBLE_LEN_BYTES + sizeof(ChppTransportHeader) +
                             sizeof(ChppWwanGetCapabilitiesResponse));

  // Cleanup
  chppWorkThreadStop(&mTransportContext);
  t1.join();
}

/**
 * WiFi service Open and GetCapabilities.
 */
TEST_F(TransportTests, WifiOpen) {
  mTransportContext.txStatus.hasPacketsToSend = true;
  std::thread t1(chppWorkThreadStart, &mTransportContext);
  WaitForTransport(&mTransportContext);

  uint8_t ackSeq = 1;
  uint8_t seq = 0;
  uint8_t handle = CHPP_HANDLE_NEGOTIATED_RANGE_START + 1;
  uint8_t transactionID = 0;

  openService(&mTransportContext, mBuf, ackSeq++, seq++, handle,
              transactionID++, CHPP_WIFI_OPEN);

  uint16_t command = CHPP_WIFI_GET_CAPABILITIES;
  sendCommandToService(&mTransportContext, mBuf, ackSeq++, seq++, handle,
                       transactionID++, command);

  size_t responseLoc = sizeof(ChppTestResponse);

  // Validate capabilities
  uint32_t *capabilities =
      (uint32_t *)&mTransportContext.pendingTxPacket.payload[responseLoc];
  responseLoc += sizeof(uint32_t);

  EXPECT_EQ(*capabilities, CHRE_WIFI_CAPABILITIES_SCAN_MONITORING |
                               CHRE_WIFI_CAPABILITIES_ON_DEMAND_SCAN);

  // Check total length
  EXPECT_EQ(responseLoc, CHPP_PREAMBLE_LEN_BYTES + sizeof(ChppTransportHeader) +
                             sizeof(ChppWwanGetCapabilitiesResponse));

  // Cleanup
  chppWorkThreadStop(&mTransportContext);
  t1.join();
}

/**
 * GNSS service Open and GetCapabilities.
 */
TEST_F(TransportTests, GnssOpen) {
  mTransportContext.txStatus.hasPacketsToSend = true;
  std::thread t1(chppWorkThreadStart, &mTransportContext);
  WaitForTransport(&mTransportContext);

  uint8_t ackSeq = 1;
  uint8_t seq = 0;
  uint8_t handle = CHPP_HANDLE_NEGOTIATED_RANGE_START + 2;
  uint8_t transactionID = 0;
  size_t len = 0;

  openService(&mTransportContext, mBuf, ackSeq++, seq++, handle,
              transactionID++, CHPP_GNSS_OPEN);

  addPreambleToBuf(mBuf, &len);

  uint16_t command = CHPP_GNSS_GET_CAPABILITIES;
  sendCommandToService(&mTransportContext, mBuf, ackSeq++, seq++, handle,
                       transactionID++, command);

  size_t responseLoc = sizeof(ChppTestResponse);

  // Validate capabilities
  uint32_t *capabilities =
      (uint32_t *)&mTransportContext.pendingTxPacket.payload[responseLoc];
  responseLoc += sizeof(uint32_t);

  EXPECT_EQ(*capabilities,
            CHRE_GNSS_CAPABILITIES_LOCATION |
                CHRE_GNSS_CAPABILITIES_MEASUREMENTS |
                CHRE_GNSS_CAPABILITIES_GNSS_ENGINE_BASED_PASSIVE_LISTENER);

  // Check total length
  EXPECT_EQ(responseLoc, CHPP_PREAMBLE_LEN_BYTES + sizeof(ChppTransportHeader) +
                             sizeof(ChppGnssGetCapabilitiesResponse));

  // Cleanup
  chppWorkThreadStop(&mTransportContext);
  t1.join();
}

/**
 * Discovery client.
 */
TEST_F(TransportTests, Discovery) {
  size_t len = 0;

  mTransportContext.txStatus.hasPacketsToSend = true;
  std::thread t1(chppWorkThreadStart, &mTransportContext);
  WaitForTransport(&mTransportContext);

  addPreambleToBuf(mBuf, &len);

  ChppTransportHeader *transHeader = addTransportHeaderToBuf(mBuf, &len);

  ChppAppHeader *appHeader = addAppHeaderToBuf(mBuf, &len);
  appHeader->handle = CHPP_HANDLE_DISCOVERY;
  appHeader->command = CHPP_DISCOVERY_COMMAND_DISCOVER_ALL;
  appHeader->type = CHPP_MESSAGE_TYPE_SERVICE_RESPONSE;

  static const ChppServiceDescriptor wwanServiceDescriptor = {
      .uuid = CHPP_UUID_WWAN_STANDARD,

      // Human-readable name
      .name = "WWAN",

      // Version
      .version.major = 1,
      .version.minor = 0,
      .version.patch = 0,
  };
  memcpy(&mBuf[len], &wwanServiceDescriptor, sizeof(ChppServiceDescriptor));
  len += sizeof(ChppServiceDescriptor);

  transHeader->length =
      len - sizeof(ChppTransportHeader) - CHPP_PREAMBLE_LEN_BYTES;

  addTransportFooterToBuf(mBuf, &len);

  // Initialize clientIndexOfServiceIndex[0] to see if it correctly updated upon
  // discovery
  mAppContext.clientIndexOfServiceIndex[0] = CHPP_CLIENT_INDEX_NONE;

  // Send header + payload (if any) + footer
  EXPECT_TRUE(chppRxDataCb(&mTransportContext, mBuf, len));

  EXPECT_EQ(mAppContext.clientIndexOfServiceIndex[0], 0);

  // Check for correct state
  uint8_t nextSeq = transHeader->seq + 1;
  EXPECT_EQ(mTransportContext.rxStatus.expectedSeq, nextSeq);
  EXPECT_EQ(mTransportContext.rxStatus.state, CHPP_STATE_PREAMBLE);

  // Cleanup
  chppWorkThreadStop(&mTransportContext);
  t1.join();
}  // namespace

INSTANTIATE_TEST_SUITE_P(TransportTestRange, TransportTests,
                         testing::ValuesIn(kChunkSizes));

}  // namespace
