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

#include <cstring>
#include <optional>
#include <unordered_set>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test_base.h"

#include "chre/core/event_loop_manager.h"
#include "chre/core/host_message_hub_manager.h"
#include "chre/util/system/message_common.h"
#include "chre/util/system/message_router.h"
#include "chre/util/system/message_router_mocks.h"
#include "chre_api/chre/event.h"

#include "pw_allocator/libc_allocator.h"
#include "pw_allocator/unique_ptr.h"
#include "pw_function/function.h"

namespace chre {
namespace {

using ::chre::message::EndpointId;
using ::chre::message::EndpointInfo;
using ::chre::message::EndpointType;
using ::chre::message::Message;
using ::chre::message::MessageHubId;
using ::chre::message::MessageHubInfo;
using ::chre::message::MessageRouter;
using ::chre::message::MessageRouterSingleton;
using ::chre::message::MockMessageHubCallback;
using ::chre::message::Reason;
using ::chre::message::Session;
using ::chre::message::SessionId;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::NiceMock;
using ::testing::Sequence;
using ::testing::UnorderedElementsAreArray;

class MockHostCallback : public HostMessageHubManager::HostCallback {
 public:
  MOCK_METHOD(void, onReset, (), (override));
  MOCK_METHOD(void, onHubRegistered, (const MessageHubInfo &hub), (override));
  MOCK_METHOD(void, onHubUnregistered, (MessageHubId), (override));
  MOCK_METHOD(void, onEndpointRegistered,
              (MessageHubId hub, const EndpointInfo &endpoint), (override));
  MOCK_METHOD(void, onEndpointUnregistered,
              (MessageHubId hub, EndpointId endpoint), (override));
  MOCK_METHOD(bool, onMessageReceived,
              (MessageHubId hub, SessionId session,
               pw::UniquePtr<std::byte[]> &&data, uint32_t type,
               uint32_t permissions),
              (override));
  MOCK_METHOD(void, onSessionOpenRequest, (const Session &session), (override));
  MOCK_METHOD(void, onSessionOpened, (MessageHubId hub, SessionId session),
              (override));
  MOCK_METHOD(void, onSessionClosed,
              (MessageHubId hub, SessionId session, Reason reason), (override));
};

HostMessageHubManager &getManager() {
  return EventLoopManagerSingleton::get()->getHostMessageHubManager();
}

MessageRouter &getRouter() {
  return *MessageRouterSingleton::get();
}

const EndpointInfo kEndpoints[] = {
    EndpointInfo(0x1, nullptr, 0, EndpointType::GENERIC, 0),
    EndpointInfo(0x2, nullptr, 0, EndpointType::GENERIC, 0)};
const EndpointInfo kExtraEndpoint(0x3, nullptr, 0, EndpointType::GENERIC, 0);
const EndpointId kEndpointIds[] = {0x1, 0x2};
const char *kEmbeddedHubName = "embedded hub";
const MessageHubInfo kEmbeddedHub{.id = CHRE_PLATFORM_ID + 1,
                                  .name = kEmbeddedHubName};
const char *kHostHubName = "host hub";
const MessageHubInfo kHostHub{.id = kEmbeddedHub.id + 1, .name = kHostHubName};

class HostMessageHubTest : public TestBase {
 public:
  HostMessageHubTest()
      : TestBase(),
        mEmbeddedEndpoints(kEndpoints, kEndpoints + std::size(kEndpoints)) {}

  void SetUp() override {
    TestBase::SetUp();

    mEmbeddedHubCb = pw::MakeRefCounted<NiceMock<MockMessageHubCallback>>();
    EXPECT_NE(mEmbeddedHubCb.get(), nullptr);

    // Specify uninteresting behaviors for the mock embedded hub callback.
    ON_CALL(*mEmbeddedHubCb, forEachEndpoint(_))
        .WillByDefault(
            [this](const pw::Function<bool(const EndpointInfo &)> &fn) {
              for (const auto &info : mEmbeddedEndpoints)
                if (fn(info)) return;
            });
    ON_CALL(*mEmbeddedHubCb, getEndpointInfo(_))
        .WillByDefault([this](EndpointId id) -> std::optional<EndpointInfo> {
          for (const auto &endpoint : mEmbeddedEndpoints)
            if (endpoint.id == id) return endpoint;
          return {};
        });
    ON_CALL(*mEmbeddedHubCb, getEndpointForService(_))
        .WillByDefault(
            [](const char *) -> std::optional<EndpointId> { return {}; });
    ON_CALL(*mEmbeddedHubCb, doesEndpointHaveService(_, _))
        .WillByDefault([](EndpointId, const char *) { return false; });
    ON_CALL(*mEmbeddedHubCb, forEachService(_))
        .WillByDefault(
            [](const pw::Function<bool(const EndpointInfo &,
                                       const message::ServiceInfo &)> &) {});

    // We mostly don't care about this. Individual tests may override this
    // behavior.
    EXPECT_CALL(*mEmbeddedHubCb, onHubRegistered(_)).Times(AnyNumber());
    EXPECT_CALL(*mEmbeddedHubCb, onHubUnregistered(_)).Times(AnyNumber());
    EXPECT_CALL(mHostCallback, onHubRegistered(_)).Times(AnyNumber());
    EXPECT_CALL(mHostCallback, onHubUnregistered(_)).Times(AnyNumber());

    // Register the embedded message hub with MessageRouter.
    auto maybeEmbeddedHub = getRouter().registerMessageHub(
        kEmbeddedHubName, kEmbeddedHub.id, mEmbeddedHubCb);
    if (maybeEmbeddedHub) {
      mEmbeddedHubIntf = std::move(*maybeEmbeddedHub);
    } else {
      FAIL() << "Failed to register test embedded message hub";
    }

    // Initialize the manager with a mock HostCallback.
    getManager().onHostTransportReady(mHostCallback);
  }

  void TearDown() override {
    EXPECT_CALL(mHostCallback, onReset());
    EXPECT_CALL(mHostCallback, onHubRegistered(_)).Times(AnyNumber());
    EXPECT_CALL(mHostCallback, onEndpointRegistered(_, _)).Times(AnyNumber());
    getManager().reset();
    mEmbeddedHubIntf.unregister();

    TestBase::TearDown();
  }

 protected:
  pw::IntrusivePtr<NiceMock<MockMessageHubCallback>> mEmbeddedHubCb;
  MessageRouter::MessageHub mEmbeddedHubIntf;
  MockHostCallback mHostCallback;

  std::vector<EndpointInfo> mEmbeddedEndpoints;
};

MATCHER_P(HubIdMatcher, id, "Matches a MessageHubInfo by id") {
  return arg.id == id;
}

TEST_F(HostMessageHubTest, Reset) {
  // On each reset(), expect onReset() followed by onHubRegistered() and
  // onEndpointRegistered() for each endpoint.
  auto resetExpectations = [this] {
    Sequence defaultHub, testHub;
    EXPECT_CALL(mHostCallback, onReset())
        .InSequence(defaultHub, testHub)
        .RetiresOnSaturation();
    EXPECT_CALL(mHostCallback, onHubRegistered(HubIdMatcher(CHRE_PLATFORM_ID)))
        .InSequence(defaultHub)
        .RetiresOnSaturation();
    EXPECT_CALL(mHostCallback, onHubRegistered(kEmbeddedHub))
        .InSequence(defaultHub)
        .RetiresOnSaturation();
    for (const auto &info : kEndpoints) {
      EXPECT_CALL(mHostCallback, onEndpointRegistered(kEmbeddedHub.id, info))
          .InSequence(defaultHub)
          .RetiresOnSaturation();
    }
  };

  // reset() with no host endpoints.
  resetExpectations();
  getManager().reset();
  getRouter().forEachEndpoint(
      [](const MessageHubInfo &hub, const EndpointInfo &) {
        EXPECT_EQ(hub.id, kEmbeddedHub.id);
      });

  // Add a host hub and endpoint. MessageRouter should see none of them after a
  // second reset().
  getManager().registerHub(kHostHub);
  getManager().registerEndpoint(kHostHub.id, kEndpoints[0]);
  resetExpectations();
  getManager().reset();
  getRouter().forEachEndpoint(
      [](const MessageHubInfo &hub, const EndpointInfo &) {
        EXPECT_EQ(hub.id, kEmbeddedHub.id);
      });
}

TEST_F(HostMessageHubTest, RegisterAndUnregisterHub) {
  EXPECT_FALSE(getRouter().forEachEndpointOfHub(
      kHostHub.id, [](const EndpointInfo &) { return true; }));

  EXPECT_CALL(*mEmbeddedHubCb, onHubRegistered(kHostHub));
  getManager().registerHub(kHostHub);
  getManager().registerEndpoint(kHostHub.id, kEndpoints[0]);
  EXPECT_TRUE(getRouter().forEachEndpointOfHub(
      kHostHub.id, [](const EndpointInfo &) { return true; }));

  EXPECT_CALL(*mEmbeddedHubCb, onHubUnregistered(kHostHub.id));
  getManager().unregisterHub(kHostHub.id);
  // NOTE: The hub stays registered with MessageRouter to avoid races with
  // unregistering message hubs, however its endpoints are no longer accessible.
  getRouter().forEachEndpointOfHub(kHostHub.id, [](const EndpointInfo &) {
    ADD_FAILURE();
    return true;
  });
}

// Hubs are expected to be static over the runtime, i.e. regardless of when a
// hub is registered, the total set of hubs is fixed. A different hub cannot
// take the slot of an unregistered hub.
TEST_F(HostMessageHubTest, RegisterHubStaticHubLimit) {
  // Register a hub to occupy a slot.
  getManager().registerHub(kHostHub);

  // Attempt to register a hub for each slot. The final registration should fail
  // due to the occupied slot.
  std::vector<std::string> hubNames;
  for (uint64_t i = 1; i <= CHRE_MESSAGE_ROUTER_MAX_HOST_HUBS; ++i) {
    MessageHubId id = kHostHub.id + i;
    hubNames.push_back(std::string(kHostHubName) + '0');
    hubNames.back().back() = i + '0';
    getManager().registerHub({.id = id, .name = hubNames[i - 1].c_str()});
    if (i < CHRE_MESSAGE_ROUTER_MAX_HOST_HUBS) {
      EXPECT_TRUE(getRouter().forEachEndpointOfHub(
          id, [](const EndpointInfo &) { return true; }));
    } else {
      EXPECT_FALSE(getRouter().forEachEndpointOfHub(
          id, [](const EndpointInfo &) { return true; }));
    }
  }
}

MATCHER_P(HubMatcher, id, "matches the hub id in MessageHubInfo") {
  return arg.id == id;
}

TEST_F(HostMessageHubTest, OnHubRegisteredAndUnregistered) {
  getManager().registerHub(kHostHub);

  const MessageHubId kHubId = kHostHub.id + 1;
  EXPECT_CALL(mHostCallback, onHubRegistered(HubMatcher(kHubId)));
  pw::IntrusivePtr<MockMessageHubCallback> newHubCb =
      pw::MakeRefCounted<MockMessageHubCallback>();
  const char *name = "test embedded hub";
  auto newHub = getRouter().registerMessageHub(name, kHubId, newHubCb);
  EXPECT_TRUE(newHub);

  EXPECT_CALL(mHostCallback, onHubUnregistered(kHubId));
  newHub.reset();
}

TEST_F(HostMessageHubTest, RegisterAndUnregisterEndpoint) {
  getManager().registerHub(kHostHub);

  EXPECT_CALL(*mEmbeddedHubCb,
              onEndpointRegistered(kHostHub.id, kEndpoints[0].id));
  getManager().registerEndpoint(kHostHub.id, kEndpoints[0]);
  getRouter().forEachEndpointOfHub(kHostHub.id, [](const EndpointInfo &info) {
    EXPECT_EQ(info.id, kEndpoints[0].id);
    return true;
  });

  EXPECT_CALL(*mEmbeddedHubCb,
              onEndpointUnregistered(kHostHub.id, kEndpoints[0].id));
  getManager().unregisterEndpoint(kHostHub.id, kEndpoints[0].id);
  bool found = false;
  getRouter().forEachEndpointOfHub(kHostHub.id, [&found](const EndpointInfo &) {
    found = true;
    return true;
  });
  EXPECT_FALSE(found);
}

TEST_F(HostMessageHubTest, OnEndpointRegisteredAndUnregistered) {
  getManager().registerHub(kHostHub);

  mEmbeddedEndpoints.push_back(kExtraEndpoint);
  EXPECT_CALL(mHostCallback,
              onEndpointRegistered(kEmbeddedHub.id, kExtraEndpoint));
  mEmbeddedHubIntf.registerEndpoint(kExtraEndpoint.id);

  EXPECT_CALL(mHostCallback,
              onEndpointUnregistered(kEmbeddedHub.id, kExtraEndpoint.id));
  mEmbeddedHubIntf.unregisterEndpoint(kExtraEndpoint.id);
}

TEST_F(HostMessageHubTest, RegisterMaximumEndpoints) {
  getManager().registerHub(kHostHub);

  // Try to register one more than the maximum endpoints.
  for (int i = 0; i <= CHRE_MESSAGE_ROUTER_MAX_HOST_ENDPOINTS; ++i) {
    EndpointInfo endpoint(0x1 + i, nullptr, 0, EndpointType::GENERIC, 0);
    getManager().registerEndpoint(kHostHub.id, endpoint);
  }

  int count = 0;
  getRouter().forEachEndpointOfHub(kHostHub.id, [&count](const EndpointInfo &) {
    count++;
    return false;
  });
  EXPECT_EQ(count, CHRE_MESSAGE_ROUTER_MAX_HOST_ENDPOINTS);

  // Unregister one endpoint and register another one.
  getManager().unregisterEndpoint(kHostHub.id, 0x1);
  EndpointInfo endpoint(0x1 + CHRE_MESSAGE_ROUTER_MAX_HOST_ENDPOINTS, nullptr,
                        0, EndpointType::GENERIC, 0);
  getManager().registerEndpoint(kHostHub.id, endpoint);
  bool found = false;
  getRouter().forEachEndpointOfHub(
      kHostHub.id, [&found](const EndpointInfo &info) {
        if (info.id == 0x1 + CHRE_MESSAGE_ROUTER_MAX_HOST_ENDPOINTS) {
          found = true;
          return true;
        }
        return false;
      });
  EXPECT_TRUE(found);
}

TEST_F(HostMessageHubTest, OpenAndCloseSession) {
  getManager().registerHub(kHostHub);
  getManager().registerEndpoint(kHostHub.id, kEndpoints[0]);

  constexpr auto sessionId = MessageRouter::kDefaultReservedSessionId;
  EXPECT_CALL(mHostCallback, onSessionOpened(kHostHub.id, sessionId)).Times(1);
  EXPECT_CALL(*mEmbeddedHubCb, onSessionOpenRequest(_))
      .WillOnce([this](const Session &session) {
        mEmbeddedHubIntf.onSessionOpenComplete(session.sessionId);
      });
  getManager().openSession(kHostHub.id, kEndpoints[0].id, kEmbeddedHub.id,
                           kEndpoints[1].id, sessionId,
                           /*serviceDescriptor=*/nullptr);

  EXPECT_CALL(*mEmbeddedHubCb,
              onSessionClosed(_, Reason::CLOSE_ENDPOINT_SESSION_REQUESTED))
      .Times(1);
  getManager().closeSession(kHostHub.id, sessionId,
                            Reason::CLOSE_ENDPOINT_SESSION_REQUESTED);
}

TEST_F(HostMessageHubTest, OpenSessionAndHandleClose) {
  getManager().registerHub(kHostHub);
  getManager().registerEndpoint(kHostHub.id, kEndpoints[0]);

  constexpr auto sessionId = MessageRouter::kDefaultReservedSessionId;
  EXPECT_CALL(mHostCallback, onSessionOpened(kHostHub.id, sessionId)).Times(1);
  EXPECT_CALL(*mEmbeddedHubCb, onSessionOpenRequest(_))
      .WillOnce([this](const Session &session) {
        mEmbeddedHubIntf.onSessionOpenComplete(session.sessionId);
      });
  getManager().openSession(kHostHub.id, kEndpoints[0].id, kEmbeddedHub.id,
                           kEndpoints[1].id, sessionId,
                           /*serviceDescriptor=*/nullptr);

  EXPECT_CALL(mHostCallback,
              onSessionClosed(kHostHub.id, sessionId,
                              Reason::CLOSE_ENDPOINT_SESSION_REQUESTED))
      .Times(1);
  mEmbeddedHubIntf.closeSession(sessionId,
                                Reason::CLOSE_ENDPOINT_SESSION_REQUESTED);
}

TEST_F(HostMessageHubTest, OpenSessionRejected) {
  getManager().registerHub(kHostHub);
  getManager().registerEndpoint(kHostHub.id, kEndpoints[0]);

  constexpr auto sessionId = MessageRouter::kDefaultReservedSessionId;
  EXPECT_CALL(mHostCallback,
              onSessionClosed(kHostHub.id, sessionId,
                              Reason::OPEN_ENDPOINT_SESSION_REQUEST_REJECTED))
      .Times(1);
  EXPECT_CALL(*mEmbeddedHubCb, onSessionOpenRequest(_))
      .WillOnce([this](const Session &session) {
        mEmbeddedHubIntf.closeSession(
            session.sessionId, Reason::OPEN_ENDPOINT_SESSION_REQUEST_REJECTED);
      });
  getManager().openSession(kHostHub.id, kEndpoints[0].id, kEmbeddedHub.id,
                           kEndpoints[1].id, sessionId,
                           /*serviceDescriptor=*/nullptr);
}

TEST_F(HostMessageHubTest, AckSession) {
  getManager().registerHub(kHostHub);
  getManager().registerEndpoint(kHostHub.id, kEndpoints[0]);

  SessionId receivedSessionId;
  EXPECT_CALL(mHostCallback, onSessionOpenRequest(_))
      .WillOnce([&receivedSessionId](const Session &session) {
        receivedSessionId = session.sessionId;
      });
  auto sessionId = mEmbeddedHubIntf.openSession(kEndpoints[1].id, kHostHub.id,
                                                kEndpoints[0].id);
  EXPECT_EQ(sessionId, receivedSessionId);

  EXPECT_CALL(*mEmbeddedHubCb, onSessionOpened(_)).Times(1);
  getManager().ackSession(kHostHub.id, sessionId);
}

MATCHER_P(DataMatcher, data, "matches data in pw::UniquePtr<std::byte[]>") {
  return arg != nullptr && !std::memcmp(arg.get(), data, arg.size());
}

MATCHER_P(SessionIdMatcher, session, "matches the session id in Session") {
  return arg.sessionId == session;
}

TEST_F(HostMessageHubTest, SendMessage) {
  getManager().registerHub(kHostHub);
  getManager().registerEndpoint(kHostHub.id, kEndpoints[0]);
  constexpr auto sessionId = MessageRouter::kDefaultReservedSessionId;
  EXPECT_CALL(mHostCallback, onSessionOpened(kHostHub.id, sessionId)).Times(1);
  EXPECT_CALL(*mEmbeddedHubCb, onSessionOpenRequest(_))
      .WillOnce([this](const Session &session) {
        mEmbeddedHubIntf.onSessionOpenComplete(session.sessionId);
      });
  getManager().openSession(kHostHub.id, kEndpoints[0].id, kEmbeddedHub.id,
                           kEndpoints[1].id, sessionId,
                           /*serviceDescriptor=*/nullptr);

  std::byte data[] = {std::byte{0xde}, std::byte{0xad}, std::byte{0xbe},
                      std::byte{0xef}};
  EXPECT_CALL(*mEmbeddedHubCb,
              onMessageReceived(DataMatcher(data), 1, 2,
                                SessionIdMatcher(sessionId), _))
      .Times(1);
  getManager().sendMessage(kHostHub.id, sessionId, {data, sizeof(data)}, 1, 2);
}

TEST_F(HostMessageHubTest, ReceiveMessage) {
  getManager().registerHub(kHostHub);
  getManager().registerEndpoint(kHostHub.id, kEndpoints[0]);
  constexpr auto sessionId = MessageRouter::kDefaultReservedSessionId;
  EXPECT_CALL(mHostCallback, onSessionOpened(kHostHub.id, sessionId)).Times(1);
  EXPECT_CALL(*mEmbeddedHubCb, onSessionOpenRequest(_))
      .WillOnce([this](const Session &session) {
        mEmbeddedHubIntf.onSessionOpenComplete(session.sessionId);
      });
  getManager().openSession(kHostHub.id, kEndpoints[0].id, kEmbeddedHub.id,
                           kEndpoints[1].id, sessionId,
                           /*serviceDescriptor=*/nullptr);

  std::byte bytes[] = {std::byte{0xde}, std::byte{0xad}, std::byte{0xbe},
                       std::byte{0xef}};
  auto data = pw::allocator::GetLibCAllocator().MakeUniqueArray<std::byte>(4);
  std::memcpy(data.get(), bytes, sizeof(bytes));
  EXPECT_CALL(mHostCallback, onMessageReceived(kHostHub.id, sessionId,
                                               DataMatcher(bytes), 1, 2))
      .Times(1);
  mEmbeddedHubIntf.sendMessage(std::move(data), 1, 2, sessionId);
}

}  // namespace
}  // namespace chre
