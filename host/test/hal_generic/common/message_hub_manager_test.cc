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

#include "message_hub_manager.h"

#include <cstddef>
#include <memory>
#include <random>
#include <unordered_map>

#include <aidl/android/hardware/contexthub/IContextHub.h>

#include "chre/platform/log.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace android::hardware::contexthub::common::implementation {
namespace {

using ::aidl::android::hardware::contexthub::EndpointId;
using ::aidl::android::hardware::contexthub::EndpointInfo;
using ::aidl::android::hardware::contexthub::ErrorCode;
using ::aidl::android::hardware::contexthub::HubInfo;
using ::aidl::android::hardware::contexthub::IEndpointCallback;
using ::aidl::android::hardware::contexthub::Message;
using ::aidl::android::hardware::contexthub::MessageDeliveryStatus;
using ::aidl::android::hardware::contexthub::Reason;
using ::ndk::ScopedAStatus;
using ::ndk::SharedRefBase;
using ::ndk::SpAIBinder;
using ::testing::_;
using ::testing::AllOf;
using ::testing::AnyNumber;
using ::testing::Ge;
using ::testing::IsEmpty;
using ::testing::IsNull;
using ::testing::Le;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::UnorderedElementsAreArray;

class MockEndpointCallback : public IEndpointCallback {
 public:
  MOCK_METHOD(ScopedAStatus, onEndpointStarted,
              (const std::vector<EndpointInfo> &), (override));
  MOCK_METHOD(ScopedAStatus, onEndpointStopped,
              (const std::vector<EndpointId> &, Reason), (override));
  MOCK_METHOD(ScopedAStatus, onMessageReceived, (int32_t, const Message &),
              (override));
  MOCK_METHOD(ScopedAStatus, onMessageDeliveryStatusReceived,
              (int32_t, const MessageDeliveryStatus &), (override));
  MOCK_METHOD(ScopedAStatus, onEndpointSessionOpenRequest,
              (int32_t, const EndpointId &, const EndpointId &,
               const std::optional<std::string> &),
              (override));
  MOCK_METHOD(ScopedAStatus, onCloseEndpointSession, (int32_t, Reason),
              (override));
  MOCK_METHOD(ScopedAStatus, onEndpointSessionOpenComplete, (int32_t),
              (override));
  MOCK_METHOD(SpAIBinder, asBinder, (), (override));
  MOCK_METHOD(bool, isRemote, (), (override));
  MOCK_METHOD(ScopedAStatus, getInterfaceVersion, (int32_t *), (override));
  MOCK_METHOD(ScopedAStatus, getInterfaceHash, (std::string *), (override));

  MockEndpointCallback() {
    ON_CALL(*this, onEndpointStarted)
        .WillByDefault([](const std::vector<EndpointInfo> &) {
          return ScopedAStatus::ok();
        });
    ON_CALL(*this, onEndpointStopped)
        .WillByDefault([](const std::vector<EndpointId> &, Reason) {
          return ScopedAStatus::ok();
        });
    ON_CALL(*this, onMessageReceived)
        .WillByDefault(
            [](int32_t, const Message &) { return ScopedAStatus::ok(); });
    ON_CALL(*this, onMessageDeliveryStatusReceived)
        .WillByDefault([](int32_t, const MessageDeliveryStatus &) {
          return ScopedAStatus::ok();
        });
    ON_CALL(*this, onEndpointSessionOpenRequest)
        .WillByDefault([](int32_t, const EndpointId &, const EndpointId &,
                          const std::optional<std::string> &) {
          return ScopedAStatus::ok();
        });
    ON_CALL(*this, onCloseEndpointSession).WillByDefault([](int32_t, Reason) {
      return ScopedAStatus::ok();
    });
    ON_CALL(*this, onEndpointSessionOpenComplete).WillByDefault([](int32_t) {
      return ScopedAStatus::ok();
    });
  }
};

constexpr int64_t kHub1Id = 0x1, kHub2Id = 0x2;
constexpr int64_t kEndpoint1Id = 0x1, kEndpoint2Id = 0x2;
const std::string kTestServiceDescriptor = "test_service";
const HubInfo kHub1Info{.hubId = kHub1Id};
const HubInfo kHub2Info{.hubId = kHub2Id};
const Service kTestService{.serviceDescriptor = kTestServiceDescriptor};
const EndpointInfo kEndpoint1_1Info{
    .id = {.id = kEndpoint1Id, .hubId = kHub1Id}};
const EndpointInfo kEndpoint1_2Info{
    .id = {.id = kEndpoint2Id, .hubId = kHub1Id}, .services = {kTestService}};
const EndpointInfo kEndpoint2_1Info{
    .id = {.id = kEndpoint1Id, .hubId = kHub2Id}};
const EndpointInfo kEndpoint2_2Info{
    .id = {.id = kEndpoint2Id, .hubId = kHub2Id}, .services = {kTestService}};

}  // namespace

class MessageHubManagerTest : public ::testing::Test {
 public:
  using HostHub = MessageHubManager::HostHub;
  using DeathRecipientCookie = HostHub::DeathRecipientCookie;
  using HostHubDownCb = MessageHubManager::HostHubDownCb;

  static constexpr auto kSessionIdMaxRange = HostHub::kSessionIdMaxRange;
  static constexpr auto kHostSessionIdBase =
      MessageHubManager::kHostSessionIdBase;

  void SetUp() override {
    reinit([](std::function<pw::Result<int64_t>()>) { FAIL(); });
  }

  void TearDown() override {
    mManager->forEachHostHub([](HostHub &hub) { delete hub.mCookie; });
  }

  void reinit(HostHubDownCb cb) {
    auto deathRecipient = std::make_unique<NiceMock<MockDeathRecipient>>();
    mDeathRecipient = deathRecipient.get();
    ON_CALL(*deathRecipient, linkCallback(_, _))
        .WillByDefault(Return(pw::OkStatus()));
    ON_CALL(*deathRecipient, unlinkCallback(_, _))
        .WillByDefault(Return(pw::OkStatus()));
    mManager.reset(
        new MessageHubManager(std::move(deathRecipient), std::move(cb)));
  }

  void onClientDeath(const std::shared_ptr<HostHub> &hub) {
    MessageHubManager::onClientDeath(hub->mCookie);
  }

  void setupDefaultHubs() {
    mManager->initEmbeddedState();
    mManager->addEmbeddedHub(kHub2Info);
    mManager->addEmbeddedEndpoint(kEndpoint2_1Info);
    mManager->setEmbeddedEndpointReady(kEndpoint2_1Info.id);
    mManager->addEmbeddedEndpoint(kEndpoint2_2Info);
    mManager->setEmbeddedEndpointReady(kEndpoint2_2Info.id);
    mHostHubCb = SharedRefBase::make<MockEndpointCallback>();
    mHostHub = *mManager->createHostHub(mHostHubCb, kHub1Info, 0, 0);
    EXPECT_TRUE(mHostHub->addEndpoint(kEndpoint1_1Info).ok());
    EXPECT_TRUE(mHostHub->addEndpoint(kEndpoint1_2Info).ok());
  }

  uint16_t setupDefaultHubsAndSession() {
    setupDefaultHubs();
    auto range = *mHostHub->reserveSessionIdRange(1);
    EXPECT_TRUE(mHostHub
                    ->openSession(kEndpoint1_1Info.id, kEndpoint2_1Info.id,
                                  range.first, {}, /*hostInitiated=*/true)
                    .ok());
    EXPECT_CALL(*mHostHubCb, onEndpointSessionOpenComplete(range.first));
    EXPECT_TRUE(mManager->getHostHub(kHub1Id)
                    ->ackSession(range.first,
                                 /*hostAcked=*/false)
                    .ok());
    return range.first;
  }

 protected:
  class MockDeathRecipient : public MessageHubManager::DeathRecipient {
   public:
    MOCK_METHOD(pw::Status, linkCallback,
                (const std::shared_ptr<IEndpointCallback> &,
                 DeathRecipientCookie *),
                (override));
    MOCK_METHOD(pw::Status, unlinkCallback,
                (const std::shared_ptr<IEndpointCallback> &,
                 DeathRecipientCookie *),
                (override));
  };

  std::unique_ptr<MessageHubManager> mManager;
  NiceMock<MockDeathRecipient> *mDeathRecipient;

  std::shared_ptr<HostHub> mHostHub;
  std::shared_ptr<MockEndpointCallback> mHostHubCb;
};

namespace {

MATCHER_P(MatchSp, sp, "Matches an IEndpointCallback") {
  return arg.get() == sp.get();
}

TEST_F(MessageHubManagerTest, CreateAndUnregisterHostHub) {
  mHostHubCb = SharedRefBase::make<MockEndpointCallback>();
  DeathRecipientCookie *cookie;
  EXPECT_CALL(*mDeathRecipient, linkCallback(MatchSp(mHostHubCb), _))
      .WillOnce([&cookie](const std::shared_ptr<IEndpointCallback> &,
                          DeathRecipientCookie *c) {
        cookie = c;
        return pw::OkStatus();
      });
  auto statusOrHub = mManager->createHostHub(mHostHubCb, kHub1Info, 0, 0);
  ASSERT_TRUE(statusOrHub.ok());

  mHostHub = *statusOrHub;
  EXPECT_EQ(mHostHub->id(), kHub1Id);
  EXPECT_EQ(mHostHub, mManager->getHostHub(kHub1Id));

  EXPECT_CALL(*mDeathRecipient, unlinkCallback(MatchSp(mHostHubCb), cookie))
      .WillOnce(Return(pw::OkStatus()));
  mHostHub->unregister();
  EXPECT_EQ(mHostHub->unregister(), pw::Status::Aborted());
  EXPECT_THAT(mManager->getHostHub(kHub1Id), IsNull());
}

TEST_F(MessageHubManagerTest, CreateHostHubFails) {
  mHostHubCb = SharedRefBase::make<MockEndpointCallback>();
  EXPECT_CALL(*mDeathRecipient, linkCallback(MatchSp(mHostHubCb), _))
      .WillOnce(Return(pw::Status::Internal()));
  EXPECT_FALSE(mManager->createHostHub(mHostHubCb, kHub1Info, 0, 0).ok());
}

TEST_F(MessageHubManagerTest, OnClientDeath) {
  bool unlinked = false;
  reinit([&unlinked](std::function<pw::Result<int64_t>()> fn) {
    auto statusOrHubId = fn();
    ASSERT_TRUE(statusOrHubId.ok());
    EXPECT_EQ(*statusOrHubId, kHub1Id);
    unlinked = true;
  });

  mHostHubCb = SharedRefBase::make<MockEndpointCallback>();
  EXPECT_CALL(*mDeathRecipient, linkCallback(MatchSp(mHostHubCb), _))
      .WillOnce(Return(pw::OkStatus()));
  mHostHub = *mManager->createHostHub(mHostHubCb, kHub1Info, 0, 0);
  EXPECT_EQ(mHostHub->id(), kHub1Id);
  EXPECT_EQ(mHostHub, mManager->getHostHub(kHub1Id));

  EXPECT_CALL(*mDeathRecipient, unlinkCallback(_, _)).Times(0);
  onClientDeath(mHostHub);
  EXPECT_THAT(mManager->getHostHub(kHub1Id), IsNull());
}

TEST_F(MessageHubManagerTest, OnClientDeathAfterUnregister) {
  mHostHubCb = SharedRefBase::make<MockEndpointCallback>();
  mHostHub = *mManager->createHostHub(mHostHubCb, kHub1Info, 0, 0);
  mHostHub->unregister();
  onClientDeath(mHostHub);
}

TEST_F(MessageHubManagerTest, InitAndClearEmbeddedState) {
  mManager->addEmbeddedHub(kHub1Info);
  EXPECT_THAT(mManager->getEmbeddedHubs(), IsEmpty());

  mManager->initEmbeddedState();
  mManager->addEmbeddedHub(kHub1Info);
  EXPECT_THAT(mManager->getEmbeddedHubs(),
              UnorderedElementsAreArray({kHub1Info}));

  mManager->clearEmbeddedState();
  EXPECT_THAT(mManager->getEmbeddedHubs(), IsEmpty());
}

TEST_F(MessageHubManagerTest, AddAndRemoveEmbeddedHub) {
  mManager->initEmbeddedState();
  mManager->addEmbeddedHub(kHub1Info);
  EXPECT_THAT(mManager->getEmbeddedHubs(),
              UnorderedElementsAreArray({kHub1Info}));

  mManager->removeEmbeddedHub(kHub1Id);
  EXPECT_THAT(mManager->getEmbeddedHubs(), IsEmpty());
}

MATCHER_P(MatchEndpointInfo, info, "Matches an EndpointInfo") {
  if (arg.id.id != info.id.id || arg.id.hubId != info.id.hubId ||
      arg.services.size() != info.services.size()) {
    return false;
  }
  for (size_t i = 0; i < arg.services.size(); ++i) {
    if (arg.services[i].serviceDescriptor !=
        info.services[i].serviceDescriptor) {
      return false;
    }
  }
  return true;
}

TEST_F(MessageHubManagerTest, AddAndRemoveEmbeddedEndpoint) {
  mHostHubCb = SharedRefBase::make<MockEndpointCallback>();
  auto hostHub = *mManager->createHostHub(mHostHubCb, kHub1Info, 0, 0);
  mManager->initEmbeddedState();
  mManager->addEmbeddedHub(kHub2Info);

  mManager->addEmbeddedEndpoint({.id = kEndpoint2_2Info.id});
  EXPECT_THAT(mManager->getEmbeddedEndpoints(), IsEmpty());

  mManager->addEmbeddedEndpointService(kEndpoint2_2Info.id,
                                       kEndpoint2_2Info.services[0]);
  EXPECT_THAT(mManager->getEmbeddedEndpoints(), IsEmpty());

  EXPECT_CALL(*mHostHubCb, onEndpointStarted(UnorderedElementsAreArray(
                               {MatchEndpointInfo(kEndpoint2_2Info)})));
  mManager->setEmbeddedEndpointReady(kEndpoint2_2Info.id);
  EXPECT_THAT(mManager->getEmbeddedEndpoints(),
              UnorderedElementsAreArray({MatchEndpointInfo(kEndpoint2_2Info)}));

  EXPECT_CALL(*mHostHubCb, onEndpointStopped(
                               UnorderedElementsAreArray({kEndpoint2_2Info.id}),
                               Reason::ENDPOINT_GONE));
  mManager->removeEmbeddedEndpoint(kEndpoint2_2Info.id);
  EXPECT_THAT(mManager->getEmbeddedEndpoints(), IsEmpty());
}

TEST_F(MessageHubManagerTest, RemovingEmbeddedHubRemovesEndpoints) {
  mManager->initEmbeddedState();
  mManager->addEmbeddedHub(kHub2Info);
  mManager->addEmbeddedEndpoint(kEndpoint2_1Info);
  mManager->setEmbeddedEndpointReady(kEndpoint2_1Info.id);
  mManager->addEmbeddedEndpoint(kEndpoint2_2Info);
  mManager->setEmbeddedEndpointReady(kEndpoint2_2Info.id);
  EXPECT_THAT(mManager->getEmbeddedEndpoints(),
              UnorderedElementsAreArray({MatchEndpointInfo(kEndpoint2_1Info),
                                         MatchEndpointInfo(kEndpoint2_2Info)}));

  mHostHubCb = SharedRefBase::make<MockEndpointCallback>();
  auto hostHub = *mManager->createHostHub(mHostHubCb, kHub1Info, 0, 0);
  EXPECT_CALL(*mHostHubCb,
              onEndpointStopped(UnorderedElementsAreArray(
                                    {kEndpoint2_1Info.id, kEndpoint2_2Info.id}),
                                Reason::HUB_RESET));
  mManager->removeEmbeddedHub(kHub2Id);
  EXPECT_THAT(mManager->getEmbeddedEndpoints(), IsEmpty());
}

TEST_F(MessageHubManagerTest, AddEmbeddedEndpointForUnknownHub) {
  mManager->initEmbeddedState();
  mManager->addEmbeddedEndpoint(kEndpoint1_1Info);
  mManager->setEmbeddedEndpointReady(kEndpoint1_1Info.id);
  EXPECT_THAT(mManager->getEmbeddedEndpoints(), IsEmpty());
}

TEST_F(MessageHubManagerTest, AddAndRemoveHostEndpoint) {
  mHostHubCb = SharedRefBase::make<MockEndpointCallback>();
  mHostHub = *mManager->createHostHub(mHostHubCb, kHub1Info, 0, 0);

  EXPECT_TRUE(mHostHub->addEndpoint(kEndpoint1_1Info).ok());
  EXPECT_THAT(mHostHub->getEndpoints(),
              UnorderedElementsAreArray({kEndpoint1_1Info}));

  EXPECT_TRUE(mHostHub->removeEndpoint(kEndpoint1_1Info.id).ok());
  EXPECT_THAT(mHostHub->getEndpoints(), IsEmpty());
}

TEST_F(MessageHubManagerTest, AddDuplicateEndpoint) {
  mHostHubCb = SharedRefBase::make<MockEndpointCallback>();
  mHostHub = *mManager->createHostHub(mHostHubCb, kHub1Info, 0, 0);
  ASSERT_TRUE(mHostHub->addEndpoint(kEndpoint1_1Info).ok());
  EXPECT_EQ(mHostHub->addEndpoint(kEndpoint1_1Info),
            pw::Status::AlreadyExists());
}

TEST_F(MessageHubManagerTest, RemoveNonexistentEndpoint) {
  mHostHubCb = SharedRefBase::make<MockEndpointCallback>();
  mHostHub = *mManager->createHostHub(mHostHubCb, kHub1Info, 0, 0);
  EXPECT_EQ(mHostHub->removeEndpoint(kEndpoint1_1Info.id).status(),
            pw::Status::NotFound());
}

TEST_F(MessageHubManagerTest, ReserveSessionIdRange) {
  mHostHubCb = SharedRefBase::make<MockEndpointCallback>();
  mHostHub = *mManager->createHostHub(mHostHubCb, kHub1Info, 0, 0);
  std::random_device rand;
  auto range = *mHostHub->reserveSessionIdRange(
      std::uniform_int_distribution<size_t>(1, kSessionIdMaxRange)(rand));
  EXPECT_THAT(range.second - range.first + 1,
              AllOf(Ge(1), Le(kSessionIdMaxRange)));
}

TEST_F(MessageHubManagerTest, ReserveBadSessionIdRange) {
  mHostHubCb = SharedRefBase::make<MockEndpointCallback>();
  mHostHub = *mManager->createHostHub(mHostHubCb, kHub1Info, 0, 0);
  EXPECT_EQ(mHostHub->reserveSessionIdRange(0).status(),
            pw::Status::InvalidArgument());
  EXPECT_EQ(mHostHub->reserveSessionIdRange(kSessionIdMaxRange + 1).status(),
            pw::Status::InvalidArgument());
}

TEST_F(MessageHubManagerTest, ReserveSessionIdRangeFull) {
  mHostHubCb = SharedRefBase::make<MockEndpointCallback>();
  mHostHub = *mManager->createHostHub(mHostHubCb, kHub1Info, 0, 0);
  int iterations = (USHRT_MAX - kHostSessionIdBase + 1) / kSessionIdMaxRange;
  for (int i = 0; i < iterations; ++i)
    ASSERT_TRUE(mHostHub->reserveSessionIdRange(kSessionIdMaxRange).ok());
  EXPECT_EQ(mHostHub->reserveSessionIdRange(kSessionIdMaxRange).status(),
            pw::Status::ResourceExhausted());
}

TEST_F(MessageHubManagerTest, OpenHostSessionRequest) {
  setupDefaultHubs();

  auto range = *mHostHub->reserveSessionIdRange(1);
  EXPECT_TRUE(mHostHub
                  ->openSession(kEndpoint1_1Info.id, kEndpoint2_1Info.id,
                                range.first, {}, /*hostInitiated=*/true)
                  .ok());
  EXPECT_FALSE(mHostHub->checkSessionOpen(range.first).ok());
}

TEST_F(MessageHubManagerTest, OpenHostSessionRequestBadSessionId) {
  setupDefaultHubs();

  auto range = *mHostHub->reserveSessionIdRange(1);
  EXPECT_EQ(mHostHub->openSession(kEndpoint1_1Info.id, kEndpoint2_1Info.id,
                                  range.first + 1, {}, /*hostInitiated=*/true),
            pw::Status::OutOfRange());
}

TEST_F(MessageHubManagerTest, OpenEmbeddedSessionRequest) {
  setupDefaultHubs();

  static constexpr uint16_t kSessionId = 1;
  std::optional<std::string> serviceDescriptor;
  EXPECT_CALL(*mHostHubCb, onEndpointSessionOpenRequest(
                               kSessionId, kEndpoint1_1Info.id,
                               kEndpoint2_1Info.id, serviceDescriptor));
  EXPECT_TRUE(mHostHub
                  ->openSession(kEndpoint1_1Info.id, kEndpoint2_1Info.id,
                                kSessionId, {}, /*hostInitiated=*/false)
                  .ok());
  EXPECT_FALSE(mHostHub->checkSessionOpen(kSessionId).ok());
}

TEST_F(MessageHubManagerTest, OpenEmbeddedSessionRequestBadSessionId) {
  setupDefaultHubs();

  EXPECT_FALSE(mHostHub
                   ->openSession(kEndpoint1_1Info.id, kEndpoint2_1Info.id,
                                 kHostSessionIdBase, {},
                                 /*hostInitiated=*/false)
                   .ok());
  EXPECT_EQ(mHostHub->checkSessionOpen(kHostSessionIdBase),
            pw::Status::NotFound());
}

TEST_F(MessageHubManagerTest, OpenSessionRequestUnknownHostEndpoint) {
  mManager->initEmbeddedState();
  mManager->addEmbeddedHub(kHub2Info);
  mManager->addEmbeddedEndpoint(kEndpoint2_1Info);
  mManager->setEmbeddedEndpointReady(kEndpoint2_1Info.id);
  mHostHubCb = SharedRefBase::make<MockEndpointCallback>();
  mHostHub = *mManager->createHostHub(mHostHubCb, kHub1Info, 0, 0);

  auto range = *mHostHub->reserveSessionIdRange(1);
  EXPECT_EQ(mHostHub->openSession(kEndpoint1_1Info.id, kEndpoint2_1Info.id,
                                  range.first, {}, /*hostInitiated=*/true),
            pw::Status::NotFound());
}

TEST_F(MessageHubManagerTest, OpenSessionRequestUnknownEmbeddedEndpoint) {
  mManager->initEmbeddedState();
  mManager->addEmbeddedHub(kHub2Info);
  mHostHubCb = SharedRefBase::make<MockEndpointCallback>();
  mHostHub = *mManager->createHostHub(mHostHubCb, kHub1Info, 0, 0);
  ASSERT_TRUE(mHostHub->addEndpoint(kEndpoint1_1Info).ok());

  auto range = *mHostHub->reserveSessionIdRange(1);
  EXPECT_EQ(mHostHub->openSession(kEndpoint1_1Info.id, kEndpoint2_1Info.id,
                                  range.first, {}, /*hostInitiated=*/true),
            pw::Status::NotFound());
}

TEST_F(MessageHubManagerTest, OpenHostSessionRequestWithService) {
  setupDefaultHubs();

  auto range = *mHostHub->reserveSessionIdRange(1);
  uint16_t sessionId = range.first;
  EXPECT_TRUE(mHostHub
                  ->openSession(kEndpoint1_2Info.id, kEndpoint2_2Info.id,
                                sessionId, kTestServiceDescriptor,
                                /*hostInitiated=*/true)
                  .ok());
}

TEST_F(MessageHubManagerTest, OpenEmbeddedSessionRequestWithService) {
  setupDefaultHubs();

  auto range = *mHostHub->reserveSessionIdRange(1);
  uint16_t sessionId = range.first;
  EXPECT_TRUE(mHostHub
                  ->openSession(kEndpoint1_2Info.id, kEndpoint2_2Info.id,
                                sessionId, kTestServiceDescriptor,
                                /*hostInitiated=*/true)
                  .ok());
}

TEST_F(MessageHubManagerTest, OpenSessionWithServiceHostSideDoesNotSupport) {
  setupDefaultHubs();

  EXPECT_FALSE(mHostHub
                   ->openSession(kEndpoint1_1Info.id, kEndpoint2_2Info.id,
                                 kHostSessionIdBase, kTestServiceDescriptor,
                                 /*hostInitiated=*/true)
                   .ok());
}

TEST_F(MessageHubManagerTest,
       OpenSessionWithServiceEmbeddedSideDoesNotSupport) {
  setupDefaultHubs();

  EXPECT_FALSE(mHostHub
                   ->openSession(kEndpoint1_2Info.id, kEndpoint2_1Info.id,
                                 kHostSessionIdBase, kTestServiceDescriptor,
                                 /*hostInitiated=*/true)
                   .ok());
}

TEST_F(MessageHubManagerTest, OpenSessionRequestServiceSupportedButNotUsed) {
  setupDefaultHubs();

  std::optional<std::string> serviceDescriptor;
  auto range = *mHostHub->reserveSessionIdRange(1);
  uint16_t sessionId = range.first;
  EXPECT_TRUE(mHostHub
                  ->openSession(kEndpoint1_2Info.id, kEndpoint2_2Info.id,
                                sessionId, serviceDescriptor,
                                /*hostInitiated=*/true)
                  .ok());
}

TEST_F(MessageHubManagerTest, OpenHostSessionEmbeddedEndpointAccepts) {
  auto sessionId = setupDefaultHubsAndSession();
  EXPECT_TRUE(mHostHub->checkSessionOpen(sessionId).ok());
}

TEST_F(MessageHubManagerTest, OpenHostSessionEmbeddedEndpointRejects) {
  setupDefaultHubs();
  auto range = *mHostHub->reserveSessionIdRange(1);
  ASSERT_TRUE(mHostHub
                  ->openSession(kEndpoint1_1Info.id, kEndpoint2_1Info.id,
                                range.first, {}, /*hostInitiated=*/true)
                  .ok());

  EXPECT_CALL(*mHostHubCb,
              onCloseEndpointSession(
                  range.first, Reason::OPEN_ENDPOINT_SESSION_REQUEST_REJECTED));
  EXPECT_TRUE(mManager->getHostHub(kHub1Id)
                  ->closeSession(range.first,
                                 Reason::OPEN_ENDPOINT_SESSION_REQUEST_REJECTED)
                  .ok());
  EXPECT_EQ(mHostHub->checkSessionOpen(range.first), pw::Status::NotFound());
}

TEST_F(MessageHubManagerTest, OpenHostSessionHostTriesToAck) {
  setupDefaultHubs();
  auto range = *mHostHub->reserveSessionIdRange(1);
  ASSERT_TRUE(mHostHub
                  ->openSession(kEndpoint1_1Info.id, kEndpoint2_1Info.id,
                                range.first, {}, /*hostInitiated=*/true)
                  .ok());

  EXPECT_FALSE(mHostHub->ackSession(range.first, /*hostAcked=*/true).ok());
}

TEST_F(MessageHubManagerTest, OpenEmbeddedSessionHostEndpointAccepts) {
  setupDefaultHubs();
  static constexpr uint16_t kSessionId = 1;
  EXPECT_CALL(*mHostHubCb, onEndpointSessionOpenRequest(kSessionId, _, _, _));
  ASSERT_TRUE(mHostHub
                  ->openSession(kEndpoint1_1Info.id, kEndpoint2_1Info.id,
                                kSessionId, {}, /*hostInitiated=*/false)
                  .ok());

  EXPECT_TRUE(mHostHub->ackSession(kSessionId, /*hostAcked=*/true).ok());
  EXPECT_FALSE(mHostHub->checkSessionOpen(kSessionId).ok());
}

TEST_F(MessageHubManagerTest, OpenEmbeddedSessionMessageRouterTriesToAck) {
  setupDefaultHubs();
  static constexpr uint16_t kSessionId = 1;
  EXPECT_CALL(*mHostHubCb, onEndpointSessionOpenRequest(kSessionId, _, _, _));
  ASSERT_TRUE(mHostHub
                  ->openSession(kEndpoint1_1Info.id, kEndpoint2_1Info.id,
                                kSessionId, {}, /*hostInitiated=*/false)
                  .ok());

  EXPECT_FALSE(mHostHub->ackSession(kSessionId, /*hostAcked=*/false).ok());
}

TEST_F(MessageHubManagerTest, OpenEmbeddedSessionPrunePendingSession) {
  setupDefaultHubs();
  static constexpr uint16_t kSessionId = 1;
  EXPECT_CALL(*mHostHubCb, onEndpointSessionOpenRequest(kSessionId, _, _, _));
  EXPECT_TRUE(mHostHub
                  ->openSession(kEndpoint1_1Info.id, kEndpoint2_1Info.id,
                                kSessionId, {}, /*hostInitiated=*/false)
                  .ok());
  EXPECT_TRUE(mHostHub->ackSession(kSessionId, /*hostAcked=*/true).ok());

  EXPECT_CALL(*mHostHubCb, onCloseEndpointSession(kSessionId, _));
  EXPECT_CALL(*mHostHubCb, onEndpointSessionOpenRequest(kSessionId, _, _, _));
  EXPECT_TRUE(mHostHub
                  ->openSession(kEndpoint1_1Info.id, kEndpoint2_1Info.id,
                                kSessionId, {}, /*hostInitiated=*/false)
                  .ok());
}

TEST_F(MessageHubManagerTest, OpenEmbeddedSessionMessageRouterAcks) {
  setupDefaultHubs();
  static constexpr uint16_t kSessionId = 1;
  EXPECT_CALL(*mHostHubCb, onEndpointSessionOpenRequest(kSessionId, _, _, _));
  ASSERT_TRUE(mHostHub
                  ->openSession(kEndpoint1_1Info.id, kEndpoint2_1Info.id,
                                kSessionId, {}, /*hostInitiated=*/false)
                  .ok());
  ASSERT_TRUE(mHostHub->ackSession(kSessionId, /*hostAcked=*/true).ok());

  EXPECT_TRUE(mHostHub->ackSession(kSessionId, /*hostAcked=*/false).ok());
  EXPECT_TRUE(mHostHub->checkSessionOpen(kSessionId).ok());
}

TEST_F(MessageHubManagerTest, ActiveSessionEmbeddedHubGone) {
  auto sessionId = setupDefaultHubsAndSession();

  EXPECT_CALL(*mHostHubCb,
              onCloseEndpointSession(sessionId, Reason::HUB_RESET));
  EXPECT_CALL(*mHostHubCb,
              onEndpointStopped(UnorderedElementsAreArray(
                                    {kEndpoint2_1Info.id, kEndpoint2_2Info.id}),
                                Reason::HUB_RESET));
  mManager->removeEmbeddedHub(kHub2Id);
  EXPECT_EQ(mHostHub->checkSessionOpen(sessionId), pw::Status::NotFound());
}

TEST_F(MessageHubManagerTest, ActiveSessionEmbeddedEndpointGone) {
  auto sessionId = setupDefaultHubsAndSession();

  EXPECT_CALL(*mHostHubCb,
              onCloseEndpointSession(sessionId, Reason::ENDPOINT_GONE));
  EXPECT_CALL(*mHostHubCb, onEndpointStopped(
                               UnorderedElementsAreArray({kEndpoint2_1Info.id}),
                               Reason::ENDPOINT_GONE));
  mManager->removeEmbeddedEndpoint(kEndpoint2_1Info.id);
  EXPECT_EQ(mHostHub->checkSessionOpen(sessionId), pw::Status::NotFound());
}

TEST_F(MessageHubManagerTest, ActiveSessionHostEndpointGone) {
  auto sessionId = setupDefaultHubsAndSession();

  EXPECT_THAT(*mHostHub->removeEndpoint(kEndpoint1_1Info.id),
              UnorderedElementsAreArray({sessionId}));
  EXPECT_EQ(mHostHub->checkSessionOpen(sessionId), pw::Status::NotFound());
}

TEST_F(MessageHubManagerTest, HandleMessage) {
  auto sessionId = setupDefaultHubsAndSession();

  Message message{.content = {0xde, 0xad, 0xbe, 0xef}};
  EXPECT_CALL(*mHostHubCb, onMessageReceived(sessionId, message));
  EXPECT_TRUE(mHostHub->handleMessage(sessionId, message).ok());
}

TEST_F(MessageHubManagerTest, HandleMessageForUnknownSession) {
  setupDefaultHubs();

  Message message{.content = {0xde, 0xad, 0xbe, 0xef}};
  EXPECT_CALL(*mHostHubCb, onMessageReceived(_, _)).Times(0);
  EXPECT_FALSE(mHostHub->handleMessage(1, message).ok());
}

TEST_F(MessageHubManagerTest, HandleMessageDeliveryStatus) {
  auto sessionId = setupDefaultHubsAndSession();

  MessageDeliveryStatus status{.errorCode = ErrorCode::TRANSIENT_ERROR};
  EXPECT_CALL(*mHostHubCb, onMessageDeliveryStatusReceived(sessionId, status));
  EXPECT_TRUE(mHostHub->handleMessageDeliveryStatus(sessionId, status).ok());
}

TEST_F(MessageHubManagerTest, HandleMessageDeliveryStatusForUnknownSession) {
  setupDefaultHubs();

  MessageDeliveryStatus status{.errorCode = ErrorCode::TRANSIENT_ERROR};
  EXPECT_CALL(*mHostHubCb, onMessageDeliveryStatusReceived(_, _)).Times(0);
  EXPECT_FALSE(mHostHub->handleMessageDeliveryStatus(1, status).ok());
}

}  // namespace
}  // namespace android::hardware::contexthub::common::implementation
