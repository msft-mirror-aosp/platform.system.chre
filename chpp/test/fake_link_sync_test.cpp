/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include <gtest/gtest.h>

#include <string.h>
#include <cstdint>
#include <future>
#include <iostream>
#include <thread>
#include <type_traits>
#include <vector>

#include "chpp/app.h"
#include "chpp/clients/wifi.h"
#include "chpp/common/wifi.h"
#include "chpp/link.h"
#include "chpp/log.h"
#include "chpp/platform/platform_link.h"
#include "chpp/transport.h"
#include "chre/pal/wifi.h"
#include "chre/platform/shared/pal_system_api.h"
#include "fake_link.h"
#include "packet_util.h"

using chpp::test::FakeLink;

namespace {

static void init(void *linkContext,
                 struct ChppTransportState *transportContext) {
  auto context = static_cast<struct ChppTestLinkState *>(linkContext);
  context->fake = new FakeLink();
  context->transportContext = transportContext;
}

static void deinit(void *linkContext) {
  auto context = static_cast<struct ChppTestLinkState *>(linkContext);
  auto *fake = reinterpret_cast<FakeLink *>(context->fake);
  delete fake;
}

static enum ChppLinkErrorCode send(void *linkContext, size_t len) {
  auto context = static_cast<struct ChppTestLinkState *>(linkContext);
  auto *fake = reinterpret_cast<FakeLink *>(context->fake);
  fake->appendTxPacket(&context->txBuffer[0], len);
  return CHPP_LINK_ERROR_NONE_SENT;
}

static void doWork(void * /*linkContext*/, uint32_t /*signal*/) {}

static void reset(void * /*linkContext*/) {}

struct ChppLinkConfiguration getConfig(void * /*linkContext*/) {
  return ChppLinkConfiguration{
      .txBufferLen = CHPP_TEST_LINK_TX_MTU_BYTES,
      .rxBufferLen = CHPP_TEST_LINK_RX_MTU_BYTES,
  };
}

uint8_t *getTxBuffer(void *linkContext) {
  auto context = static_cast<struct ChppTestLinkState *>(linkContext);
  return &context->txBuffer[0];
}

}  // namespace

const struct ChppLinkApi gLinkApi = {
    .init = &init,
    .deinit = &deinit,
    .send = &send,
    .doWork = &doWork,
    .reset = &reset,
    .getConfig = &getConfig,
    .getTxBuffer = &getTxBuffer,
};

namespace chpp::test {

class FakeLinkSyncTests : public testing::Test {
 protected:
  void SetUp() override {
    pthread_setname_np(pthread_self(), "test");
    memset(&mLinkContext, 0, sizeof(mLinkContext));
    chppTransportInit(&mTransportContext, &mAppContext, &mLinkContext,
                      &gLinkApi);
    initChppAppLayer();
    mFakeLink = reinterpret_cast<FakeLink *>(mLinkContext.fake);

    // Note that while the tests tend to primarily execute in the main thread,
    // some behaviors rely on the work thread, which can create some flakiness,
    // e.g. if the thread doesn't get scheduled within the timeout. It would be
    // possible to "pause" the work thread by sending a link signal that blocks
    // indefinitely, so we can execute any pending operations synchronously in
    // waitForTxPacket(), but it would be best to combine this approach with
    // simulated timestamps/delays so we can guarantee no unexpected timeouts
    // and so we can force timeout behavior without having to delay test
    // execution (as seen in CHRE's TransactionManagerTest).
    mWorkThread = std::thread([this] {
      pthread_setname_np(pthread_self(), "worker");
      chppWorkThreadStart(&mTransportContext);
    });
    performHandshake();
  }

  virtual void initChppAppLayer() {
    chppAppInitWithClientServiceSet(&mAppContext, &mTransportContext,
                                    /*clientServiceSet=*/{});
    mAppContext.isDiscoveryComplete = true;  // Skip discovery
  }

  void performHandshake() {
    // Proceed to the initialized state by performing the CHPP 3-way handshake
    CHPP_LOGI("Send a RESET packet");
    ASSERT_TRUE(mFakeLink->waitForTxPacket());
    std::vector<uint8_t> resetPkt = mFakeLink->popTxPacket();
    ASSERT_TRUE(comparePacket(resetPkt, generateResetPacket()))
        << "Full packet: " << asResetPacket(resetPkt);

    CHPP_LOGI("Receive a RESET ACK packet");
    ChppResetPacket resetAck = generateResetAckPacket();
    chppRxDataCb(&mTransportContext, reinterpret_cast<uint8_t *>(&resetAck),
                 sizeof(resetAck));

    // Handling of the ACK to RESET-ACK depends on configuration
    handleFirstPacket();
  }

  virtual void handleFirstPacket() {
    // chppProcessResetAck() results in sending a no error packet, with no
    // payload when discovery is disabled
    CHPP_LOGI("Send CHPP_TRANSPORT_ERROR_NONE packet");
    ASSERT_TRUE(mFakeLink->waitForTxPacket());
    std::vector<uint8_t> ackPkt = mFakeLink->popTxPacket();
    ASSERT_TRUE(comparePacket(ackPkt, generateEmptyPacket()))
        << "Full packet: " << asChpp(ackPkt);
    CHPP_LOGI("CHPP handshake complete");
  }

  void discardTxPacket() {
    ASSERT_TRUE(mFakeLink->waitForTxPacket());
    EXPECT_EQ(mFakeLink->getTxPacketCount(), 1);
    (void)mFakeLink->popTxPacket();
  }

  std::vector<uint8_t> getNextPacket() {
    if (!mFakeLink->waitForTxPacket()) {
      CHPP_LOGE("Didn't get expected packet");
      return std::vector<uint8_t>();
    }
    EXPECT_EQ(mFakeLink->getTxPacketCount(), 1);
    return mFakeLink->popTxPacket();
  }

  template <typename PacketType>
  bool deliverRxPacket(const PacketType &packet) {
    CHPP_LOGW("Debug dump of RX packet:");
    std::vector<uint8_t> vec;
    vec.resize(sizeof(packet));
    memcpy(vec.data(), &packet, sizeof(packet));
    std::cout << asChpp(vec);
    return chppRxDataCb(&mTransportContext,
                        reinterpret_cast<const uint8_t *>(&packet),
                        sizeof(packet));
  }

  void TearDown() override {
    chppWorkThreadStop(&mTransportContext);
    mWorkThread.join();
    EXPECT_EQ(mFakeLink->getTxPacketCount(), 0);
    std::cout << "Unexpected extra packet(s):" << std::endl;
    while (mFakeLink->getTxPacketCount() > 0) {
      mFakeLink->popTxPacket();
    }
  }

  void txPacket() {
    uint32_t *payload = static_cast<uint32_t *>(chppMalloc(sizeof(uint32_t)));
    *payload = 0xdeadbeef;
    bool enqueued = chppEnqueueTxDatagramOrFail(&mTransportContext, payload,
                                                sizeof(uint32_t));
    EXPECT_TRUE(enqueued);
  }

  ChppTransportState mTransportContext = {};
  ChppAppState mAppContext = {};
  ChppTestLinkState mLinkContext;
  FakeLink *mFakeLink;
  std::thread mWorkThread;
};

class FakeLinkWithClientSyncTests : public FakeLinkSyncTests {
 public:
  void initChppAppLayer() override {
    // We use the WiFi client to simulate real-world integrations, but any
    // service (including a dedicated test client/service) would work
    ChppClientServiceSet set = {
        .wifiClient = 1,
    };
    chppAppInitWithClientServiceSet(&mAppContext, &mTransportContext, set);
    mAppContext.isDiscoveryComplete = true;  // Bypass initial discovery
  }

  virtual void handleFirstPacket() override {
    ASSERT_TRUE(mFakeLink->waitForTxPacket());
    std::vector<uint8_t> ackPkt = mFakeLink->popTxPacket();
    ASSERT_TRUE(comparePacket(ackPkt, generateEmptyPacket()))
        << "Full packet: " << asChpp(ackPkt);
    CHPP_LOGI("CHPP handshake complete");

    mAppContext.matchedClientCount = mAppContext.discoveredServiceCount = 1;
    // Initialize the client similar to how discovery would
    EXPECT_TRUE(mAppContext.registeredClients[0]->initFunctionPtr(
        mAppContext.registeredClientStates[0]->context,
        CHPP_SERVICE_HANDLE_OF_INDEX(0), /*version=*/{1, 0, 0}));
  }

  void sendOpenResp(const ChppPacketWithAppHeader &openReq) {
    ChppAppHeader appHdr = {
        .handle = openReq.appHeader.handle,
        .type = CHPP_MESSAGE_TYPE_SERVICE_RESPONSE,
        .transaction = openReq.appHeader.transaction,
        .error = CHPP_APP_ERROR_NONE,
        .command = openReq.appHeader.command,
    };
    std::span<uint8_t, sizeof(appHdr)> payload(
        reinterpret_cast<uint8_t *>(&appHdr), sizeof(appHdr));
    auto rsp = generatePacketWithPayload<sizeof(appHdr)>(
        openReq.transportHeader.seq + 1, openReq.transportHeader.ackSeq,
        &payload);
    deliverRxPacket(rsp);
  }
};

TEST_F(FakeLinkSyncTests, CheckRetryOnTimeout) {
  txPacket();
  ASSERT_TRUE(mFakeLink->waitForTxPacket());
  EXPECT_EQ(mFakeLink->getTxPacketCount(), 1);

  std::vector<uint8_t> pkt1 = mFakeLink->popTxPacket();

  // Not calling chppRxDataCb() will result in a timeout.
  // Ideally, to speed up the test, we'd have a mechanism to trigger
  // chppNotifierWait() to return immediately, to simulate timeout
  ASSERT_TRUE(mFakeLink->waitForTxPacket());
  EXPECT_EQ(mFakeLink->getTxPacketCount(), 1);
  std::vector<uint8_t> pkt2 = mFakeLink->popTxPacket();

  // The retry packet should be an exact match of the first one
  EXPECT_EQ(pkt1, pkt2);
}

TEST_F(FakeLinkSyncTests, NoRetryAfterAck) {
  txPacket();
  ASSERT_TRUE(mFakeLink->waitForTxPacket());
  EXPECT_EQ(mFakeLink->getTxPacketCount(), 1);

  // Generate and reply back with an ACK
  std::vector<uint8_t> pkt = mFakeLink->popTxPacket();
  ChppEmptyPacket ack = generateAck(pkt);
  chppRxDataCb(&mTransportContext, reinterpret_cast<uint8_t *>(&ack),
               sizeof(ack));

  // We shouldn't get that packet again
  EXPECT_FALSE(mFakeLink->waitForTxPacket());
}

TEST_F(FakeLinkSyncTests, MultipleNotifications) {
  constexpr int kNumPackets = 5;
  for (int i = 0; i < kNumPackets; i++) {
    txPacket();
  }

  for (int i = 0; i < kNumPackets; i++) {
    ASSERT_TRUE(mFakeLink->waitForTxPacket());

    // Generate and reply back with an ACK
    std::vector<uint8_t> pkt = mFakeLink->popTxPacket();
    ChppEmptyPacket ack = generateAck(pkt);
    chppRxDataCb(&mTransportContext, reinterpret_cast<uint8_t *>(&ack),
                 sizeof(ack));
  }

  EXPECT_FALSE(mFakeLink->waitForTxPacket());
}

// This test is essentially CheckRetryOnTimeout but with a twist: we send a
// packet, then don't send an ACK in the expected time so it gets retried, then
// after the retry, we send two equivalent ACKs back-to-back
TEST_F(FakeLinkSyncTests, DelayedThenDupeAck) {
  // Post the TX packet, discard the first ACK
  txPacket();
  discardTxPacket();

  // Second wait should yield timeout + retry
  ASSERT_TRUE(mFakeLink->waitForTxPacket());
  ASSERT_EQ(mFakeLink->getTxPacketCount(), 1);

  // Now deliver duplicate ACKs
  ChppEmptyPacket ack = generateAck(mFakeLink->popTxPacket());
  chppRxDataCb(&mTransportContext, reinterpret_cast<uint8_t *>(&ack),
               sizeof(ack));
  chppRxDataCb(&mTransportContext, reinterpret_cast<uint8_t *>(&ack),
               sizeof(ack));

  // We shouldn't get another packet (e.g. NAK)
  EXPECT_FALSE(mFakeLink->waitForTxPacket())
      << "Got unexpected packet: " << asChpp(mFakeLink->popTxPacket());

  // The next outbound packet should carry the next sequence number
  txPacket();
  ASSERT_TRUE(mFakeLink->waitForTxPacket());
  EXPECT_EQ(asChpp(mFakeLink->popTxPacket()).header.seq, ack.header.ackSeq);
}

// This tests the opposite side of DelayedThenDuplicateAck: confirms that if we
// receive a packet, then send an ACK, then we receive a duplicate, we send the
// ACK again
TEST_F(FakeLinkSyncTests, ResendAckOnDupe) {
  // Note that seq and ackSeq should both be 1, since RESET/RESET_ACK will use 0
  constexpr uint8_t kSeq = 1;
  constexpr uint8_t kAckSeq = 1;
  auto rxPkt = generatePacketWithPayload<1>(kAckSeq, kSeq);
  EXPECT_TRUE(chppRxDataCb(&mTransportContext,
                           reinterpret_cast<const uint8_t *>(&rxPkt),
                           sizeof(rxPkt)));

  ASSERT_TRUE(mFakeLink->waitForTxPacket());
  ASSERT_EQ(mFakeLink->getTxPacketCount(), 1);
  std::vector<uint8_t> pkt = mFakeLink->popTxPacket();
  // We should get an ACK in response
  EXPECT_TRUE(comparePacket(pkt, generateEmptyPacket(kSeq + 1)))
      << "Expected first ACK for seq 1 but got: " << asEmptyPacket(pkt);

  // Pretend that we lost that ACK, so resend the same packet
  EXPECT_TRUE(chppRxDataCb(&mTransportContext,
                           reinterpret_cast<const uint8_t *>(&rxPkt),
                           sizeof(rxPkt)));

  // We should get another ACK that matches the first
  ASSERT_TRUE(mFakeLink->waitForTxPacket());
  ASSERT_EQ(mFakeLink->getTxPacketCount(), 1);
  pkt = mFakeLink->popTxPacket();
  EXPECT_TRUE(comparePacket(pkt, generateEmptyPacket(kSeq + 1)))
      << "Expected second ACK for seq 1 but got: " << asEmptyPacket(pkt);

  // Sending another packet should succeed
  auto secondRxPkt = generatePacketWithPayload<2>(kAckSeq, kSeq + 1);
  EXPECT_TRUE(chppRxDataCb(&mTransportContext,
                           reinterpret_cast<const uint8_t *>(&secondRxPkt),
                           sizeof(secondRxPkt)));

  ASSERT_TRUE(mFakeLink->waitForTxPacket());
  ASSERT_EQ(mFakeLink->getTxPacketCount(), 1);
  pkt = mFakeLink->popTxPacket();
  EXPECT_TRUE(comparePacket(pkt, generateEmptyPacket(kSeq + 2)))
      << "Expected ACK for seq 2 but got: " << asEmptyPacket(pkt);
}

TEST_F(FakeLinkWithClientSyncTests, RecoverFromAbortedOpen) {
  // Setting all callbacks as null here since none should be invoked
  const struct chrePalWifiCallbacks kCallbacks = {};
  const struct chrePalWifiApi *api =
      chppPalWifiGetApi(CHPP_PAL_WIFI_API_VERSION);
  ASSERT_NE(api, nullptr);

  // Calling open() blocks until the open response is received, so spin off
  // another thread to wait on the open request and post the response.
  // This puts us in the opened state - we are mainly interested in testing
  auto result = std::async(std::launch::async, [this] {
    if (mFakeLink->waitForTxPacket()) {
      std::vector<uint8_t> rawPkt = mFakeLink->popTxPacket();
      const ChppPacketWithAppHeader &pkt = asApp(rawPkt);
      ASSERT_EQ(pkt.appHeader.command, CHPP_WIFI_OPEN);
      sendOpenResp(pkt);
    }
  });
  ASSERT_TRUE(api->open(&chre::gChrePalSystemApi, &kCallbacks));

  // Confirm our open response was ACKed
  ASSERT_TRUE(comparePacket(getNextPacket(), generateEmptyPacket(2)));

  // Now we're in the opened state and can trigger the test condition: feed in a
  // RESET, discard the RESET_ACK, confirm we got OPEN_REQ, but instead of
  // OPEN_RESP, send another RESET, then confirm we can open successfully
  CHPP_LOGI("Triggering RESET after successful open");
  auto resetPkt = generateResetPacket();
  deliverRxPacket(resetPkt);
  auto rawPkt = getNextPacket();
  ASSERT_TRUE(comparePacket(rawPkt, generateResetAckPacket()));
  // It shouldn't send anything until we ack RESET-ACK, which we aren't going to
  // do here
  ASSERT_EQ(mFakeLink->getTxPacketCount(), 0);

  CHPP_LOGI("Triggering abort of open request via another RESET");
  deliverRxPacket(resetPkt);
  rawPkt = getNextPacket();
  ASSERT_TRUE(comparePacket(rawPkt, generateResetAckPacket()));
  auto ackForResetAck = generateEmptyPacket();
  deliverRxPacket(ackForResetAck);

  // Confirm we get OPEN request, send OPEN response
  ASSERT_TRUE(mFakeLink->waitForTxPacket());
  rawPkt = getNextPacket();
  const ChppPacketWithAppHeader &pkt = asApp(rawPkt);
  ASSERT_EQ(pkt.appHeader.command, CHPP_WIFI_OPEN);
  sendOpenResp(pkt);

  // Confirm we got an ACK to our OPEN_RESP
  ASSERT_TRUE(mFakeLink->waitForTxPacket());
  rawPkt = getNextPacket();
  EXPECT_TRUE(comparePacket(rawPkt, generateEmptyPacket(2)))
      << "Full packet: " << asChpp(rawPkt);
}

}  // namespace chpp::test
