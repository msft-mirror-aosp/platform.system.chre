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

#include <pw_allocator/allocator.h>
#include <pw_allocator/capability.h>
#include <pw_allocator/unique_ptr.h>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "chre/util/dynamic_vector.h"
#include "chre/util/system/message_common.h"
#include "chre/util/system/message_router.h"
#include "chre_api/chre.h"
#include "gtest/gtest.h"

namespace chre::message {
namespace {

constexpr static size_t kMaxMessageHubs = 3;
constexpr static size_t kMaxSessions = 10;
constexpr static size_t kNumEndpoints = 3;

static EndpointInfo kEndpointInfos[kNumEndpoints] = {
    EndpointInfo(/* id= */ 1, /* name= */ "endpoint1", /* version= */ 1,
                 EndpointType::NANOAPP, CHRE_MESSAGE_PERMISSION_NONE),
    EndpointInfo(/* id= */ 2, /* name= */ "endpoint2", /* version= */ 10,
                 EndpointType::HOST_ENDPOINT, CHRE_MESSAGE_PERMISSION_BLE),
    EndpointInfo(/* id= */ 3, /* name= */ "endpoint3", /* version= */ 100,
                 EndpointType::GENERIC, CHRE_MESSAGE_PERMISSION_AUDIO)};

class TestAllocator : public pw::Allocator {
 public:
  static constexpr Capabilities kCapabilities = 0;

  TestAllocator() : pw::Allocator(kCapabilities) {}

  virtual void *DoAllocate(Layout layout) override {
    if (layout.alignment() > alignof(std::max_align_t)) {
      void *ptr;
      return posix_memalign(&ptr, layout.alignment(), layout.size()) == 0
                 ? ptr
                 : nullptr;
    } else {
      return malloc(layout.size());
    }
  }

  virtual void DoDeallocate(void *ptr) override {
    free(ptr);
  }
};

class MessageRouterTest : public ::testing::Test {
 protected:
  void SetUp() override {}

  TestAllocator mAllocator;
};

//! Base class for MessageHubCallbacks used in tests
class MessageHubCallbackBase : public MessageRouter::MessageHubCallback {
 public:
  void forEachEndpoint(
      const pw::Function<bool(const EndpointInfo &)> &function) override {
    for (const EndpointInfo &endpointInfo : kEndpointInfos) {
      if (function(endpointInfo)) {
        return;
      }
    }
  }

  std::optional<EndpointInfo> getEndpointInfo(EndpointId endpointId) override {
    for (const EndpointInfo &endpointInfo : kEndpointInfos) {
      if (endpointInfo.id == endpointId) {
        return endpointInfo;
      }
    }
    return std::nullopt;
  }
};

//! MessageHubCallback that stores the data passed to onMessageReceived and
//! onSessionClosed
class MessageHubCallbackStoreData : public MessageHubCallbackBase {
 public:
  MessageHubCallbackStoreData(Message *message, Session *session)
      : mMessage(message), mSession(session) {}

  bool onMessageReceived(pw::UniquePtr<std::byte[]> &&data, size_t length,
                         uint32_t messageType, uint32_t messagePermissions,
                         const Session &session,
                         bool sentBySessionInitiator) override {
    if (mMessage != nullptr) {
      mMessage->sender = sentBySessionInitiator ? session.initiator
                                                : session.peer;
      mMessage->recipient =
          sentBySessionInitiator ? session.peer : session.initiator;
      mMessage->sessionId = session.sessionId;
      mMessage->data = std::move(data);
      mMessage->length = length;
      mMessage->messageType = messageType;
      mMessage->messagePermissions = messagePermissions;
    }
    return true;
  }

  void onSessionClosed(const Session &session) override {
    if (mSession != nullptr) {
      *mSession = session;
    }
  }

 private:
  Message *mMessage;
  Session *mSession;
};

//! MessageHubCallback that always fails to process messages
class MessageHubCallbackAlwaysFails : public MessageHubCallbackBase {
 public:
  MessageHubCallbackAlwaysFails(bool *wasMessageReceivedCalled,
                                bool *wasSessionClosedCalled)
      : mWasMessageReceivedCalled(wasMessageReceivedCalled),
        mWasSessionClosedCalled(wasSessionClosedCalled) {}

  bool onMessageReceived(pw::UniquePtr<std::byte[]> && /* data */,
                         size_t /* length */, uint32_t /* messageType */,
                         uint32_t /* messagePermissions */,
                         const Session & /* session */,
                         bool /* sentBySessionInitiator */) override {
    if (mWasMessageReceivedCalled != nullptr) {
      *mWasMessageReceivedCalled = true;
    }
    return false;
  }

  void onSessionClosed(const Session & /* session */) override {
    if (mWasSessionClosedCalled != nullptr) {
      *mWasSessionClosedCalled = true;
    }
  }

 private:
  bool *mWasMessageReceivedCalled;
  bool *mWasSessionClosedCalled;
};

//! MessageHubCallback that calls MessageHub APIs during callbacks
class MessageHubCallbackCallsMessageHubApisDuringCallback
    : public MessageHubCallbackBase {
 public:
  bool onMessageReceived(pw::UniquePtr<std::byte[]> && /* data */,
                         size_t /* length */, uint32_t /* messageType */,
                         uint32_t /* messagePermissions */,
                         const Session & /* session */,
                         bool /* sentBySessionInitiator */) override {
    if (mMessageHub != nullptr) {
      // Call a function that locks the MessageRouter mutex
      mMessageHub->openSession(kEndpointInfos[0].id, mMessageHub->getId(),
                               kEndpointInfos[1].id);
    }
    return true;
  }

  void onSessionClosed(const Session & /* session */) override {
    if (mMessageHub != nullptr) {
      // Call a function that locks the MessageRouter mutex
      mMessageHub->openSession(kEndpointInfos[0].id, mMessageHub->getId(),
                               kEndpointInfos[1].id);
    }
  }

  void setMessageHub(MessageRouter::MessageHub *messageHub) {
    mMessageHub = messageHub;
  }

 private:
  MessageRouter::MessageHub *mMessageHub = nullptr;
};

TEST_F(MessageRouterTest, RegisterMessageHubNameIsUnique) {
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;

  MessageHubCallbackStoreData callback(/* message= */ nullptr,
                                       /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub1 =
      router.registerMessageHub("hub1", /* id= */ 1, callback);
  EXPECT_TRUE(messageHub1.has_value());
  std::optional<MessageRouter::MessageHub> messageHub2 =
      router.registerMessageHub("hub2", /* id= */ 2, callback);
  EXPECT_TRUE(messageHub2.has_value());

  std::optional<MessageRouter::MessageHub> messageHub3 =
      router.registerMessageHub("hub1", /* id= */ 1, callback);
  EXPECT_FALSE(messageHub3.has_value());
}

TEST_F(MessageRouterTest, RegisterMessageHubIdIsUnique) {
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;

  MessageHubCallbackStoreData callback(/* message= */ nullptr,
                                       /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub1 =
      router.registerMessageHub("hub1", /* id= */ 1, callback);
  EXPECT_TRUE(messageHub1.has_value());
  std::optional<MessageRouter::MessageHub> messageHub2 =
      router.registerMessageHub("hub2", /* id= */ 2, callback);
  EXPECT_TRUE(messageHub2.has_value());

  std::optional<MessageRouter::MessageHub> messageHub3 =
      router.registerMessageHub("hub3", /* id= */ 1, callback);
  EXPECT_FALSE(messageHub3.has_value());
}

TEST_F(MessageRouterTest, RegisterMessageHubGetListOfHubs) {
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;

  MessageHubCallbackStoreData callback(/* message= */ nullptr,
                                       /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub1 =
      router.registerMessageHub("hub1", /* id= */ 1, callback);
  EXPECT_TRUE(messageHub1.has_value());
  std::optional<MessageRouter::MessageHub> messageHub2 =
      router.registerMessageHub("hub2", /* id= */ 2, callback);
  EXPECT_TRUE(messageHub2.has_value());
  std::optional<MessageRouter::MessageHub> messageHub3 =
      router.registerMessageHub("hub3", /* id= */ 3, callback);
  EXPECT_TRUE(messageHub3.has_value());

  DynamicVector<MessageHubInfo> messageHubs;
  router.forEachMessageHub(
      [&messageHubs](const MessageHubInfo &messageHubInfo) {
        messageHubs.push_back(messageHubInfo);
        return false;
      });
  EXPECT_EQ(messageHubs.size(), 3);
  EXPECT_EQ(messageHubs[0].name, "hub1");
  EXPECT_EQ(messageHubs[1].name, "hub2");
  EXPECT_EQ(messageHubs[2].name, "hub3");
  EXPECT_EQ(messageHubs[0].id, 1);
  EXPECT_EQ(messageHubs[1].id, 2);
  EXPECT_EQ(messageHubs[2].id, 3);
  EXPECT_EQ(messageHubs[0].id, messageHub1->getId());
  EXPECT_EQ(messageHubs[1].id, messageHub2->getId());
  EXPECT_EQ(messageHubs[2].id, messageHub3->getId());
}

TEST_F(MessageRouterTest, RegisterMessageHubGetListOfHubsWithUnregister) {
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;

  MessageHubCallbackStoreData callback(/* message= */ nullptr,
                                       /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub1 =
      router.registerMessageHub("hub1", /* id= */ 1, callback);
  EXPECT_TRUE(messageHub1.has_value());
  std::optional<MessageRouter::MessageHub> messageHub2 =
      router.registerMessageHub("hub2", /* id= */ 2, callback);
  EXPECT_TRUE(messageHub2.has_value());
  std::optional<MessageRouter::MessageHub> messageHub3 =
      router.registerMessageHub("hub3", /* id= */ 3, callback);
  EXPECT_TRUE(messageHub3.has_value());

  DynamicVector<MessageHubInfo> messageHubs;
  router.forEachMessageHub(
      [&messageHubs](const MessageHubInfo &messageHubInfo) {
        messageHubs.push_back(messageHubInfo);
        return false;
      });
  EXPECT_EQ(messageHubs.size(), 3);
  EXPECT_EQ(messageHubs[0].name, "hub1");
  EXPECT_EQ(messageHubs[1].name, "hub2");
  EXPECT_EQ(messageHubs[2].name, "hub3");
  EXPECT_EQ(messageHubs[0].id, 1);
  EXPECT_EQ(messageHubs[1].id, 2);
  EXPECT_EQ(messageHubs[2].id, 3);
  EXPECT_EQ(messageHubs[0].id, messageHub1->getId());
  EXPECT_EQ(messageHubs[1].id, messageHub2->getId());
  EXPECT_EQ(messageHubs[2].id, messageHub3->getId());

  // Clear messageHubs and reset messageHub2
  messageHubs.clear();
  messageHub2.reset();

  router.forEachMessageHub(
      [&messageHubs](const MessageHubInfo &messageHubInfo) {
        messageHubs.push_back(messageHubInfo);
        return false;
      });
  EXPECT_EQ(messageHubs.size(), 2);
  EXPECT_EQ(messageHubs[0].name, "hub1");
  EXPECT_EQ(messageHubs[1].name, "hub3");
  EXPECT_EQ(messageHubs[0].id, 1);
  EXPECT_EQ(messageHubs[1].id, 3);
  EXPECT_EQ(messageHubs[0].id, messageHub1->getId());
  EXPECT_EQ(messageHubs[1].id, messageHub3->getId());
}

TEST_F(MessageRouterTest, RegisterMessageHubTooManyFails) {
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;
  static_assert(kMaxMessageHubs == 3);
  constexpr const char *kNames[3] = {"hub1", "hub2", "hub3"};

  MessageHubCallbackStoreData callback(/* message= */ nullptr,
                                       /* session= */ nullptr);
  MessageRouter::MessageHub messageHubs[kMaxMessageHubs];
  for (size_t i = 0; i < kMaxMessageHubs; ++i) {
    std::optional<MessageRouter::MessageHub> messageHub =
        router.registerMessageHub(kNames[i], /* id= */ i, callback);
    EXPECT_TRUE(messageHub.has_value());
    messageHubs[i] = std::move(*messageHub);
  }

  std::optional<MessageRouter::MessageHub> messageHub =
      router.registerMessageHub("shouldfail", /* id= */ kMaxMessageHubs * 2,
                                callback);
  EXPECT_FALSE(messageHub.has_value());
}

TEST_F(MessageRouterTest, GetEndpointInfo) {
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;

  MessageHubCallbackStoreData callback(/* message= */ nullptr,
                                       /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub1 =
      router.registerMessageHub("hub1", /* id= */ 1, callback);
  EXPECT_TRUE(messageHub1.has_value());
  std::optional<MessageRouter::MessageHub> messageHub2 =
      router.registerMessageHub("hub2", /* id= */ 2, callback);
  EXPECT_TRUE(messageHub2.has_value());
  std::optional<MessageRouter::MessageHub> messageHub3 =
      router.registerMessageHub("hub3", /* id= */ 3, callback);
  EXPECT_TRUE(messageHub3.has_value());

  for (size_t i = 0; i < kNumEndpoints; ++i) {
    EXPECT_EQ(
        router.getEndpointInfo(messageHub1->getId(), kEndpointInfos[i].id),
        kEndpointInfos[i]);
    EXPECT_EQ(
        router.getEndpointInfo(messageHub2->getId(), kEndpointInfos[i].id),
        kEndpointInfos[i]);
    EXPECT_EQ(
        router.getEndpointInfo(messageHub3->getId(), kEndpointInfos[i].id),
        kEndpointInfos[i]);
  }
}

TEST_F(MessageRouterTest, RegisterSessionTwoDifferentMessageHubs) {
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;
  Session sessionFromCallback1;
  Session sessionFromCallback2;
  MessageHubCallbackStoreData callback(/* message= */ nullptr,
                                       &sessionFromCallback1);
  MessageHubCallbackStoreData callback2(/* message= */ nullptr,
                                        &sessionFromCallback2);

  std::optional<MessageRouter::MessageHub> messageHub =
      router.registerMessageHub("hub1", /* id= */ 1, callback);
  EXPECT_TRUE(messageHub.has_value());
  std::optional<MessageRouter::MessageHub> messageHub2 =
      router.registerMessageHub("hub2", /* id= */ 2, callback2);
  EXPECT_TRUE(messageHub2.has_value());

  // Open session from messageHub:1 to messageHub2:2
  SessionId sessionId = messageHub->openSession(
      kEndpointInfos[0].id, messageHub2->getId(), kEndpointInfos[1].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);

  // Get session from messageHub and compare it with messageHub2
  std::optional<Session> sessionAfterRegistering =
      messageHub->getSessionWithId(sessionId);
  EXPECT_TRUE(sessionAfterRegistering.has_value());
  EXPECT_EQ(sessionAfterRegistering->sessionId, sessionId);
  EXPECT_EQ(sessionAfterRegistering->initiator.messageHubId,
            messageHub->getId());
  EXPECT_EQ(sessionAfterRegistering->initiator.endpointId,
            kEndpointInfos[0].id);
  EXPECT_EQ(sessionAfterRegistering->peer.messageHubId, messageHub2->getId());
  EXPECT_EQ(sessionAfterRegistering->peer.endpointId, kEndpointInfos[1].id);
  std::optional<Session> sessionAfterRegistering2 =
      messageHub2->getSessionWithId(sessionId);
  EXPECT_TRUE(sessionAfterRegistering2.has_value());
  EXPECT_EQ(*sessionAfterRegistering, *sessionAfterRegistering2);

  // Close the session and verify it is closed on both message hubs
  EXPECT_NE(*sessionAfterRegistering, sessionFromCallback1);
  EXPECT_NE(*sessionAfterRegistering, sessionFromCallback2);
  EXPECT_TRUE(messageHub->closeSession(sessionId));
  EXPECT_EQ(*sessionAfterRegistering, sessionFromCallback1);
  EXPECT_EQ(*sessionAfterRegistering, sessionFromCallback2);
  EXPECT_FALSE(messageHub->getSessionWithId(sessionId).has_value());
  EXPECT_FALSE(messageHub2->getSessionWithId(sessionId).has_value());
}

TEST_F(MessageRouterTest, RegisterSessionSameMessageHubInvalid) {
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;
  Session sessionFromCallback1;
  Session sessionFromCallback2;
  MessageHubCallbackStoreData callback(/* message= */ nullptr,
                                       &sessionFromCallback1);
  MessageHubCallbackStoreData callback2(/* message= */ nullptr,
                                        &sessionFromCallback2);

  std::optional<MessageRouter::MessageHub> messageHub =
      router.registerMessageHub("hub1", /* id= */ 1, callback);
  EXPECT_TRUE(messageHub.has_value());
  std::optional<MessageRouter::MessageHub> messageHub2 =
      router.registerMessageHub("hub2", /* id= */ 2, callback2);
  EXPECT_TRUE(messageHub2.has_value());

  // Open session from messageHub:2 to messageHub:2
  SessionId sessionId = messageHub->openSession(
      kEndpointInfos[1].id, messageHub->getId(), kEndpointInfos[1].id);
  EXPECT_EQ(sessionId, SESSION_ID_INVALID);

  // Open session from messageHub:1 to messageHub:3
  sessionId = messageHub->openSession(kEndpointInfos[0].id, messageHub->getId(),
                                      kEndpointInfos[2].id);
  EXPECT_EQ(sessionId, SESSION_ID_INVALID);
}

TEST_F(MessageRouterTest, RegisterSessionDifferentMessageHubsSameEndpoints) {
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;
  Session sessionFromCallback1;
  Session sessionFromCallback2;
  MessageHubCallbackStoreData callback(/* message= */ nullptr,
                                       &sessionFromCallback1);
  MessageHubCallbackStoreData callback2(/* message= */ nullptr,
                                        &sessionFromCallback2);

  std::optional<MessageRouter::MessageHub> messageHub =
      router.registerMessageHub("hub1", /* id= */ 1, callback);
  EXPECT_TRUE(messageHub.has_value());
  std::optional<MessageRouter::MessageHub> messageHub2 =
      router.registerMessageHub("hub2", /* id= */ 2, callback2);
  EXPECT_TRUE(messageHub2.has_value());

  // Open session from messageHub:1 to messageHub:2
  SessionId sessionId = messageHub->openSession(
      kEndpointInfos[0].id, messageHub2->getId(), kEndpointInfos[0].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);
}

TEST_F(MessageRouterTest,
       RegisterSessionTwoDifferentMessageHubsInvalidEndpoint) {
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;
  MessageHubCallbackStoreData callback(/* message= */ nullptr,
                                       /* session= */ nullptr);
  MessageHubCallbackStoreData callback2(/* message= */ nullptr,
                                        /* session= */ nullptr);

  std::optional<MessageRouter::MessageHub> messageHub =
      router.registerMessageHub("hub1", /* id= */ 1, callback);
  EXPECT_TRUE(messageHub.has_value());
  std::optional<MessageRouter::MessageHub> messageHub2 =
      router.registerMessageHub("hub2", /* id= */ 2, callback2);
  EXPECT_TRUE(messageHub2.has_value());

  // Open session from messageHub with other non-registered endpoint - not
  // valid
  SessionId sessionId = messageHub->openSession(
      kEndpointInfos[0].id, messageHub2->getId(), /* toEndpointId= */ 10);
  EXPECT_EQ(sessionId, SESSION_ID_INVALID);
}

TEST_F(MessageRouterTest, ThirdMessageHubTriesToFindOthersSession) {
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;
  Session sessionFromCallback1;
  Session sessionFromCallback2;
  Session sessionFromCallback3;
  MessageHubCallbackStoreData callback(/* message= */ nullptr,
                                       &sessionFromCallback1);
  MessageHubCallbackStoreData callback2(/* message= */ nullptr,
                                        &sessionFromCallback2);
  MessageHubCallbackStoreData callback3(/* message= */ nullptr,
                                        &sessionFromCallback3);

  std::optional<MessageRouter::MessageHub> messageHub =
      router.registerMessageHub("hub1", /* id= */ 1, callback);
  EXPECT_TRUE(messageHub.has_value());
  std::optional<MessageRouter::MessageHub> messageHub2 =
      router.registerMessageHub("hub2", /* id= */ 2, callback2);
  EXPECT_TRUE(messageHub2.has_value());
  std::optional<MessageRouter::MessageHub> messageHub3 =
      router.registerMessageHub("hub3", /* id= */ 3, callback3);
  EXPECT_TRUE(messageHub3.has_value());

  // Open session from messageHub:1 to messageHub2:2
  SessionId sessionId = messageHub->openSession(
      kEndpointInfos[0].id, messageHub2->getId(), kEndpointInfos[1].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);

  // Get session from messageHub and compare it with messageHub2
  std::optional<Session> sessionAfterRegistering =
      messageHub->getSessionWithId(sessionId);
  EXPECT_TRUE(sessionAfterRegistering.has_value());
  EXPECT_EQ(sessionAfterRegistering->sessionId, sessionId);
  EXPECT_EQ(sessionAfterRegistering->initiator.messageHubId,
            messageHub->getId());
  EXPECT_EQ(sessionAfterRegistering->initiator.endpointId,
            kEndpointInfos[0].id);
  EXPECT_EQ(sessionAfterRegistering->peer.messageHubId, messageHub2->getId());
  EXPECT_EQ(sessionAfterRegistering->peer.endpointId, kEndpointInfos[1].id);
  std::optional<Session> sessionAfterRegistering2 =
      messageHub2->getSessionWithId(sessionId);
  EXPECT_TRUE(sessionAfterRegistering2.has_value());
  EXPECT_EQ(*sessionAfterRegistering, *sessionAfterRegistering2);

  // Third message hub tries to find the session - not found
  EXPECT_FALSE(messageHub3->getSessionWithId(sessionId).has_value());
  // Third message hub tries to close the session - not found
  EXPECT_FALSE(messageHub3->closeSession(sessionId));

  // Get session from messageHub and compare it with messageHub2 again
  sessionAfterRegistering = messageHub->getSessionWithId(sessionId);
  EXPECT_TRUE(sessionAfterRegistering.has_value());
  EXPECT_EQ(sessionAfterRegistering->sessionId, sessionId);
  EXPECT_EQ(sessionAfterRegistering->initiator.messageHubId,
            messageHub->getId());
  EXPECT_EQ(sessionAfterRegistering->initiator.endpointId,
            kEndpointInfos[0].id);
  EXPECT_EQ(sessionAfterRegistering->peer.messageHubId, messageHub2->getId());
  EXPECT_EQ(sessionAfterRegistering->peer.endpointId, kEndpointInfos[1].id);
  sessionAfterRegistering2 = messageHub2->getSessionWithId(sessionId);
  EXPECT_TRUE(sessionAfterRegistering2.has_value());
  EXPECT_EQ(*sessionAfterRegistering, *sessionAfterRegistering2);

  // Close the session and verify it is closed on both message hubs
  EXPECT_NE(*sessionAfterRegistering, sessionFromCallback1);
  EXPECT_NE(*sessionAfterRegistering, sessionFromCallback2);
  EXPECT_TRUE(messageHub->closeSession(sessionId));
  EXPECT_EQ(*sessionAfterRegistering, sessionFromCallback1);
  EXPECT_EQ(*sessionAfterRegistering, sessionFromCallback2);
  EXPECT_NE(*sessionAfterRegistering, sessionFromCallback3);
  EXPECT_FALSE(messageHub->getSessionWithId(sessionId).has_value());
  EXPECT_FALSE(messageHub2->getSessionWithId(sessionId).has_value());
}

TEST_F(MessageRouterTest, ThreeMessageHubsAndThreeSessions) {
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;
  MessageHubCallbackStoreData callback(/* message= */ nullptr,
                                       /* session= */ nullptr);
  MessageHubCallbackStoreData callback2(/* message= */ nullptr,
                                        /* session= */ nullptr);
  MessageHubCallbackStoreData callback3(/* message= */ nullptr,
                                        /* session= */ nullptr);

  std::optional<MessageRouter::MessageHub> messageHub =
      router.registerMessageHub("hub1", /* id= */ 1, callback);
  EXPECT_TRUE(messageHub.has_value());
  std::optional<MessageRouter::MessageHub> messageHub2 =
      router.registerMessageHub("hub2", /* id= */ 2, callback2);
  EXPECT_TRUE(messageHub2.has_value());
  std::optional<MessageRouter::MessageHub> messageHub3 =
      router.registerMessageHub("hub3", /* id= */ 3, callback3);
  EXPECT_TRUE(messageHub3.has_value());

  // Open session from messageHub:1 to messageHub2:2
  SessionId sessionId = messageHub->openSession(
      kEndpointInfos[0].id, messageHub2->getId(), kEndpointInfos[1].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);

  // Open session from messageHub2:2 to messageHub3:3
  SessionId sessionId2 = messageHub2->openSession(
      kEndpointInfos[1].id, messageHub3->getId(), kEndpointInfos[2].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);

  // Open session from messageHub3:3 to messageHub1:1
  SessionId sessionId3 = messageHub3->openSession(
      kEndpointInfos[2].id, messageHub->getId(), kEndpointInfos[0].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);

  // Get sessions and compare
  // Find session: MessageHub1:1 -> MessageHub2:2
  std::optional<Session> sessionAfterRegistering =
      messageHub->getSessionWithId(sessionId);
  EXPECT_TRUE(sessionAfterRegistering.has_value());
  std::optional<Session> sessionAfterRegistering2 =
      messageHub2->getSessionWithId(sessionId);
  EXPECT_TRUE(sessionAfterRegistering2.has_value());
  EXPECT_FALSE(messageHub3->getSessionWithId(sessionId).has_value());
  EXPECT_EQ(*sessionAfterRegistering, *sessionAfterRegistering2);

  // Find session: MessageHub2:2 -> MessageHub3:3
  sessionAfterRegistering = messageHub2->getSessionWithId(sessionId2);
  EXPECT_TRUE(sessionAfterRegistering.has_value());
  sessionAfterRegistering2 = messageHub3->getSessionWithId(sessionId2);
  EXPECT_TRUE(sessionAfterRegistering2.has_value());
  EXPECT_FALSE(messageHub->getSessionWithId(sessionId2).has_value());
  EXPECT_EQ(*sessionAfterRegistering, *sessionAfterRegistering2);

  // Find session: MessageHub3:3 -> MessageHub1:1
  sessionAfterRegistering = messageHub3->getSessionWithId(sessionId3);
  EXPECT_TRUE(sessionAfterRegistering.has_value());
  sessionAfterRegistering2 = messageHub->getSessionWithId(sessionId3);
  EXPECT_TRUE(sessionAfterRegistering2.has_value());
  EXPECT_FALSE(messageHub2->getSessionWithId(sessionId3).has_value());
  EXPECT_EQ(*sessionAfterRegistering, *sessionAfterRegistering2);

  // Close sessions from receivers and verify they are closed on all hubs
  EXPECT_TRUE(messageHub2->closeSession(sessionId));
  EXPECT_TRUE(messageHub3->closeSession(sessionId2));
  EXPECT_TRUE(messageHub->closeSession(sessionId3));
  for (SessionId id : {sessionId, sessionId2, sessionId3}) {
    EXPECT_FALSE(messageHub->getSessionWithId(id).has_value());
    EXPECT_FALSE(messageHub2->getSessionWithId(id).has_value());
    EXPECT_FALSE(messageHub3->getSessionWithId(id).has_value());
  }
}

TEST_F(MessageRouterTest, SendMessageToSession) {
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;
  constexpr size_t kMessageSize = 5;
  pw::UniquePtr<std::byte[]> messageData =
      mAllocator.MakeUniqueArray<std::byte>(kMessageSize);
  for (size_t i = 0; i < 5; ++i) {
    messageData[i] = static_cast<std::byte>(i + 1);
  }

  Message messageFromCallback1;
  Message messageFromCallback2;
  Message messageFromCallback3;
  Session sessionFromCallback1;
  Session sessionFromCallback2;
  Session sessionFromCallback3;
  MessageHubCallbackStoreData callback(&messageFromCallback1,
                                       &sessionFromCallback1);
  MessageHubCallbackStoreData callback2(&messageFromCallback2,
                                        &sessionFromCallback2);
  MessageHubCallbackStoreData callback3(&messageFromCallback3,
                                        &sessionFromCallback3);

  std::optional<MessageRouter::MessageHub> messageHub =
      router.registerMessageHub("hub1", /* id= */ 1, callback);
  EXPECT_TRUE(messageHub.has_value());
  std::optional<MessageRouter::MessageHub> messageHub2 =
      router.registerMessageHub("hub2", /* id= */ 2, callback2);
  EXPECT_TRUE(messageHub2.has_value());
  std::optional<MessageRouter::MessageHub> messageHub3 =
      router.registerMessageHub("hub3", /* id= */ 3, callback3);
  EXPECT_TRUE(messageHub3.has_value());

  // Open session from messageHub:1 to messageHub2:2
  SessionId sessionId = messageHub->openSession(
      kEndpointInfos[0].id, messageHub2->getId(), kEndpointInfos[1].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);

  // Open session from messageHub2:2 to messageHub3:3
  SessionId sessionId2 = messageHub2->openSession(
      kEndpointInfos[1].id, messageHub3->getId(), kEndpointInfos[2].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);

  // Open session from messageHub3:3 to messageHub1:1
  SessionId sessionId3 = messageHub3->openSession(
      kEndpointInfos[2].id, messageHub->getId(), kEndpointInfos[0].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);

  // Send message from messageHub:1 to messageHub2:2
  ASSERT_TRUE(messageHub->sendMessage(std::move(messageData), kMessageSize,
                                      /* messageType= */ 1,
                                      /* messagePermissions= */ 0, sessionId));
  EXPECT_EQ(messageFromCallback2.sessionId, sessionId);
  EXPECT_EQ(messageFromCallback2.sender.messageHubId, messageHub->getId());
  EXPECT_EQ(messageFromCallback2.sender.endpointId, kEndpointInfos[0].id);
  EXPECT_EQ(messageFromCallback2.recipient.messageHubId, messageHub2->getId());
  EXPECT_EQ(messageFromCallback2.recipient.endpointId, kEndpointInfos[1].id);
  EXPECT_EQ(messageFromCallback2.messageType, 1);
  EXPECT_EQ(messageFromCallback2.messagePermissions, 0);
  EXPECT_EQ(messageFromCallback2.length, kMessageSize);
  for (size_t i = 0; i < kMessageSize; ++i) {
    EXPECT_EQ(messageFromCallback2.data[i], static_cast<std::byte>(i + 1));
  }

  messageData = mAllocator.MakeUniqueArray<std::byte>(kMessageSize);
  for (size_t i = 0; i < 5; ++i) {
    messageData[i] = static_cast<std::byte>(i + 1);
  }

  // Send message from messageHub2:2 to messageHub:1
  ASSERT_TRUE(messageHub2->sendMessage(std::move(messageData), kMessageSize,
                                       /* messageType= */ 2,
                                       /* messagePermissions= */ 3, sessionId));
  EXPECT_EQ(messageFromCallback1.sessionId, sessionId);
  EXPECT_EQ(messageFromCallback1.sender.messageHubId, messageHub2->getId());
  EXPECT_EQ(messageFromCallback1.sender.endpointId, kEndpointInfos[1].id);
  EXPECT_EQ(messageFromCallback1.recipient.messageHubId, messageHub->getId());
  EXPECT_EQ(messageFromCallback1.recipient.endpointId, kEndpointInfos[0].id);
  EXPECT_EQ(messageFromCallback1.messageType, 2);
  EXPECT_EQ(messageFromCallback1.messagePermissions, 3);
  EXPECT_EQ(messageFromCallback1.length, kMessageSize);
  for (size_t i = 0; i < kMessageSize; ++i) {
    EXPECT_EQ(messageFromCallback1.data[i], static_cast<std::byte>(i + 1));
  }
}

TEST_F(MessageRouterTest, SendMessageToSessionInvalidHubAndSession) {
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;
  constexpr size_t kMessageSize = 5;
  pw::UniquePtr<std::byte[]> messageData =
      mAllocator.MakeUniqueArray<std::byte>(kMessageSize);
  for (size_t i = 0; i < 5; ++i) {
    messageData[i] = static_cast<std::byte>(i + 1);
  }

  Message messageFromCallback1;
  Message messageFromCallback2;
  Message messageFromCallback3;
  Session sessionFromCallback1;
  Session sessionFromCallback2;
  Session sessionFromCallback3;
  MessageHubCallbackStoreData callback(&messageFromCallback1,
                                       &sessionFromCallback1);
  MessageHubCallbackStoreData callback2(&messageFromCallback2,
                                        &sessionFromCallback2);
  MessageHubCallbackStoreData callback3(&messageFromCallback3,
                                        &sessionFromCallback3);

  std::optional<MessageRouter::MessageHub> messageHub =
      router.registerMessageHub("hub1", /* id= */ 1, callback);
  EXPECT_TRUE(messageHub.has_value());
  std::optional<MessageRouter::MessageHub> messageHub2 =
      router.registerMessageHub("hub2", /* id= */ 2, callback2);
  EXPECT_TRUE(messageHub2.has_value());
  std::optional<MessageRouter::MessageHub> messageHub3 =
      router.registerMessageHub("hub3", /* id= */ 3, callback3);
  EXPECT_TRUE(messageHub3.has_value());

  // Open session from messageHub:1 to messageHub2:2
  SessionId sessionId = messageHub->openSession(
      kEndpointInfos[0].id, messageHub2->getId(), kEndpointInfos[1].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);

  // Open session from messageHub2:2 to messageHub3:3
  SessionId sessionId2 = messageHub2->openSession(
      kEndpointInfos[1].id, messageHub3->getId(), kEndpointInfos[2].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);

  // Open session from messageHub3:3 to messageHub1:1
  SessionId sessionId3 = messageHub3->openSession(
      kEndpointInfos[2].id, messageHub->getId(), kEndpointInfos[0].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);

  // Send message from messageHub:1 to messageHub2:2
  EXPECT_FALSE(messageHub->sendMessage(std::move(messageData), kMessageSize,
                                       /* messageType= */ 1,
                                       /* messagePermissions= */ 0,
                                       sessionId2));
  EXPECT_FALSE(messageHub2->sendMessage(std::move(messageData), kMessageSize,
                                        /* messageType= */ 2,
                                        /* messagePermissions= */ 3,
                                        sessionId3));
  EXPECT_FALSE(messageHub3->sendMessage(std::move(messageData), kMessageSize,
                                        /* messageType= */ 2,
                                        /* messagePermissions= */ 3,
                                        sessionId));
}

TEST_F(MessageRouterTest, SendMessageToSessionCallbackFailureClosesSession) {
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;
  constexpr size_t kMessageSize = 5;
  pw::UniquePtr<std::byte[]> messageData =
      mAllocator.MakeUniqueArray<std::byte>(kMessageSize);
  for (size_t i = 0; i < 5; ++i) {
    messageData[i] = static_cast<std::byte>(i + 1);
  }

  bool wasMessageReceivedCalled1 = false;
  bool wasMessageReceivedCalled2 = false;
  bool wasMessageReceivedCalled3 = false;
  MessageHubCallbackAlwaysFails callback1(
      &wasMessageReceivedCalled1,
      /* wasSessionClosedCalled= */ nullptr);
  MessageHubCallbackAlwaysFails callback2(
      &wasMessageReceivedCalled2,
      /* wasSessionClosedCalled= */ nullptr);
  MessageHubCallbackAlwaysFails callback3(
      &wasMessageReceivedCalled3,
      /* wasSessionClosedCalled= */ nullptr);

  std::optional<MessageRouter::MessageHub> messageHub =
      router.registerMessageHub("hub1", /* id= */ 1, callback1);
  EXPECT_TRUE(messageHub.has_value());
  std::optional<MessageRouter::MessageHub> messageHub2 =
      router.registerMessageHub("hub2", /* id= */ 2, callback2);
  EXPECT_TRUE(messageHub2.has_value());
  std::optional<MessageRouter::MessageHub> messageHub3 =
      router.registerMessageHub("hub3", /* id= */ 3, callback3);
  EXPECT_TRUE(messageHub3.has_value());

  // Open session from messageHub:1 to messageHub2:2
  SessionId sessionId = messageHub->openSession(
      kEndpointInfos[0].id, messageHub2->getId(), kEndpointInfos[1].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);

  // Open session from messageHub2:2 to messageHub3:3
  SessionId sessionId2 = messageHub2->openSession(
      kEndpointInfos[1].id, messageHub3->getId(), kEndpointInfos[2].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);

  // Open session from messageHub3:3 to messageHub1:1
  SessionId sessionId3 = messageHub3->openSession(
      kEndpointInfos[2].id, messageHub->getId(), kEndpointInfos[0].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);

  // Send message from messageHub2:2 to messageHub3:3
  EXPECT_FALSE(wasMessageReceivedCalled1);
  EXPECT_FALSE(wasMessageReceivedCalled2);
  EXPECT_FALSE(wasMessageReceivedCalled3);
  EXPECT_FALSE(messageHub->getSessionWithId(sessionId2).has_value());
  EXPECT_TRUE(messageHub2->getSessionWithId(sessionId2).has_value());
  EXPECT_TRUE(messageHub3->getSessionWithId(sessionId2).has_value());

  EXPECT_FALSE(messageHub2->sendMessage(std::move(messageData), kMessageSize,
                                        /* messageType= */ 1,
                                        /* messagePermissions= */ 0,
                                        sessionId2));
  EXPECT_FALSE(wasMessageReceivedCalled1);
  EXPECT_FALSE(wasMessageReceivedCalled2);
  EXPECT_TRUE(wasMessageReceivedCalled3);
  EXPECT_FALSE(messageHub->getSessionWithId(sessionId2).has_value());
  EXPECT_FALSE(messageHub2->getSessionWithId(sessionId2).has_value());
  EXPECT_FALSE(messageHub3->getSessionWithId(sessionId2).has_value());

  // Try to send a message on the same session - should fail
  wasMessageReceivedCalled1 = false;
  wasMessageReceivedCalled2 = false;
  wasMessageReceivedCalled3 = false;
  messageData = mAllocator.MakeUniqueArray<std::byte>(kMessageSize);
  for (size_t i = 0; i < 5; ++i) {
    messageData[i] = static_cast<std::byte>(i + 1);
  }
  EXPECT_FALSE(messageHub2->sendMessage(std::move(messageData), kMessageSize,
                                        /* messageType= */ 1,
                                        /* messagePermissions= */ 0,
                                        sessionId2));
  messageData = mAllocator.MakeUniqueArray<std::byte>(kMessageSize);
  for (size_t i = 0; i < 5; ++i) {
    messageData[i] = static_cast<std::byte>(i + 1);
  }
  EXPECT_FALSE(messageHub3->sendMessage(std::move(messageData), kMessageSize,
                                        /* messageType= */ 1,
                                        /* messagePermissions= */ 0,
                                        sessionId2));
  EXPECT_FALSE(wasMessageReceivedCalled1);
  EXPECT_FALSE(wasMessageReceivedCalled2);
  EXPECT_FALSE(wasMessageReceivedCalled3);
}

TEST_F(MessageRouterTest, SendMessageToSessionResetHubCausesSessionClose) {
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;
  constexpr size_t kMessageSize = 5;
  pw::UniquePtr<std::byte[]> messageData =
      mAllocator.MakeUniqueArray<std::byte>(kMessageSize);
  for (size_t i = 0; i < 5; ++i) {
    messageData[i] = static_cast<std::byte>(i + 1);
  }

  Message messageFromCallback1;
  Message messageFromCallback2;
  Message messageFromCallback3;
  Session sessionFromCallback1;
  Session sessionFromCallback2;
  Session sessionFromCallback3;
  MessageHubCallbackStoreData callback(&messageFromCallback1,
                                       &sessionFromCallback1);
  MessageHubCallbackStoreData callback2(&messageFromCallback2,
                                        &sessionFromCallback2);
  MessageHubCallbackStoreData callback3(&messageFromCallback3,
                                        &sessionFromCallback3);

  std::optional<MessageRouter::MessageHub> messageHub =
      router.registerMessageHub("hub1", /* id= */ 1, callback);
  EXPECT_TRUE(messageHub.has_value());
  std::optional<MessageRouter::MessageHub> messageHub2 =
      router.registerMessageHub("hub2", /* id= */ 2, callback2);
  EXPECT_TRUE(messageHub2.has_value());
  std::optional<MessageRouter::MessageHub> messageHub3 =
      router.registerMessageHub("hub3", /* id= */ 3, callback3);
  EXPECT_TRUE(messageHub3.has_value());

  // Open session from messageHub:1 to messageHub2:2
  SessionId sessionId = messageHub->openSession(
      kEndpointInfos[0].id, messageHub2->getId(), kEndpointInfos[1].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);

  // Open session from messageHub2:2 to messageHub3:3
  SessionId sessionId2 = messageHub2->openSession(
      kEndpointInfos[1].id, messageHub3->getId(), kEndpointInfos[2].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);

  // Open session from messageHub3:3 to messageHub1:1
  SessionId sessionId3 = messageHub3->openSession(
      kEndpointInfos[2].id, messageHub->getId(), kEndpointInfos[0].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);

  // Send message from messageHub:1 to messageHub2:2
  ASSERT_TRUE(messageHub->sendMessage(std::move(messageData), kMessageSize,
                                      /* messageType= */ 1,
                                      /* messagePermissions= */ 0, sessionId));
  EXPECT_EQ(messageFromCallback2.sessionId, sessionId);
  EXPECT_EQ(messageFromCallback2.sender.messageHubId, messageHub->getId());
  EXPECT_EQ(messageFromCallback2.sender.endpointId, kEndpointInfos[0].id);
  EXPECT_EQ(messageFromCallback2.recipient.messageHubId, messageHub2->getId());
  EXPECT_EQ(messageFromCallback2.recipient.endpointId, kEndpointInfos[1].id);
  EXPECT_EQ(messageFromCallback2.messageType, 1);
  EXPECT_EQ(messageFromCallback2.messagePermissions, 0);
  EXPECT_EQ(messageFromCallback2.length, kMessageSize);
  for (size_t i = 0; i < kMessageSize; ++i) {
    EXPECT_EQ(messageFromCallback2.data[i], static_cast<std::byte>(i + 1));
  }

  messageData = mAllocator.MakeUniqueArray<std::byte>(kMessageSize);
  for (size_t i = 0; i < 5; ++i) {
    messageData[i] = static_cast<std::byte>(i + 1);
  }

  // Send message from messageHub2:2 to messageHub:1
  ASSERT_TRUE(messageHub2->sendMessage(std::move(messageData), kMessageSize,
                                       /* messageType= */ 2,
                                       /* messagePermissions= */ 3, sessionId));
  EXPECT_EQ(messageFromCallback1.sessionId, sessionId);
  EXPECT_EQ(messageFromCallback1.sender.messageHubId, messageHub2->getId());
  EXPECT_EQ(messageFromCallback1.sender.endpointId, kEndpointInfos[1].id);
  EXPECT_EQ(messageFromCallback1.recipient.messageHubId, messageHub->getId());
  EXPECT_EQ(messageFromCallback1.recipient.endpointId, kEndpointInfos[0].id);
  EXPECT_EQ(messageFromCallback1.messageType, 2);
  EXPECT_EQ(messageFromCallback1.messagePermissions, 3);
  EXPECT_EQ(messageFromCallback1.length, kMessageSize);
  for (size_t i = 0; i < kMessageSize; ++i) {
    EXPECT_EQ(messageFromCallback1.data[i], static_cast<std::byte>(i + 1));
  }

  // Reset messageHub2
  messageHub2.reset();

  messageData = mAllocator.MakeUniqueArray<std::byte>(kMessageSize);
  for (size_t i = 0; i < 5; ++i) {
    messageData[i] = static_cast<std::byte>(i + 1);
  }

  // Send message from messageHub:1 to messageHub2:2
  EXPECT_FALSE(messageHub->sendMessage(std::move(messageData), kMessageSize,
                                       /* messageType= */ 1,
                                       /* messagePermissions= */ 0, sessionId));

  // Try to get session 1 - should be invalid
  EXPECT_FALSE(messageHub->getSessionWithId(sessionId).has_value());

  // Session 2 should still be valid as no operation has been performed
  EXPECT_TRUE(messageHub3->getSessionWithId(sessionId2).has_value());

  messageData = mAllocator.MakeUniqueArray<std::byte>(kMessageSize);
  for (size_t i = 0; i < 5; ++i) {
    messageData[i] = static_cast<std::byte>(i + 1);
  }

  // Send message from messageHub3:3 to messageHub2:2
  EXPECT_FALSE(messageHub3->sendMessage(std::move(messageData), kMessageSize,
                                        /* messageType= */ 2,
                                        /* messagePermissions= */ 3,
                                        sessionId2));

  // Session 2 should be invalid now
  EXPECT_FALSE(messageHub3->getSessionWithId(sessionId2).has_value());
}

TEST_F(MessageRouterTest, MessageHubCallbackCanCallOtherMessageHubAPIs) {
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;
  constexpr size_t kMessageSize = 5;
  pw::UniquePtr<std::byte[]> messageData =
      mAllocator.MakeUniqueArray<std::byte>(kMessageSize);
  for (size_t i = 0; i < 5; ++i) {
    messageData[i] = static_cast<std::byte>(i + 1);
  }

  MessageHubCallbackCallsMessageHubApisDuringCallback callback;
  MessageHubCallbackCallsMessageHubApisDuringCallback callback2;
  MessageHubCallbackCallsMessageHubApisDuringCallback callback3;

  std::optional<MessageRouter::MessageHub> messageHub =
      router.registerMessageHub("hub1", /* id= */ 1, callback);
  EXPECT_TRUE(messageHub.has_value());
  callback.setMessageHub(&messageHub.value());
  std::optional<MessageRouter::MessageHub> messageHub2 =
      router.registerMessageHub("hub2", /* id= */ 2, callback2);
  EXPECT_TRUE(messageHub2.has_value());
  callback2.setMessageHub(&messageHub2.value());
  std::optional<MessageRouter::MessageHub> messageHub3 =
      router.registerMessageHub("hub3", /* id= */ 3, callback3);
  EXPECT_TRUE(messageHub3.has_value());
  callback3.setMessageHub(&messageHub3.value());

  // Open session from messageHub:1 to messageHub2:2
  SessionId sessionId = messageHub->openSession(
      kEndpointInfos[0].id, messageHub2->getId(), kEndpointInfos[1].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);

  // Open session from messageHub2:2 to messageHub3:3
  SessionId sessionId2 = messageHub2->openSession(
      kEndpointInfos[1].id, messageHub3->getId(), kEndpointInfos[2].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);

  // Open session from messageHub3:3 to messageHub1:1
  SessionId sessionId3 = messageHub3->openSession(
      kEndpointInfos[2].id, messageHub->getId(), kEndpointInfos[0].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);

  // Send message from messageHub:1 to messageHub2:2
  EXPECT_TRUE(messageHub->sendMessage(std::move(messageData), kMessageSize,
                                      /* messageType= */ 1,
                                      /* messagePermissions= */ 0, sessionId));

  // Send message from messageHub2:2 to messageHub:1
  messageData = mAllocator.MakeUniqueArray<std::byte>(kMessageSize);
  for (size_t i = 0; i < 5; ++i) {
    messageData[i] = static_cast<std::byte>(i + 1);
  }
  EXPECT_TRUE(messageHub2->sendMessage(std::move(messageData), kMessageSize,
                                       /* messageType= */ 2,
                                       /* messagePermissions= */ 3, sessionId));

  // Close all sessions
  EXPECT_TRUE(messageHub->closeSession(sessionId));
  EXPECT_TRUE(messageHub2->closeSession(sessionId2));
  EXPECT_TRUE(messageHub3->closeSession(sessionId3));

  // If we finish the test, both callbacks should have been called
  // If the router holds the lock during the callback, this test will timeout
}

TEST_F(MessageRouterTest, ForEachEndpointOfHub) {
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;
  MessageHubCallbackStoreData callback(/* message= */ nullptr,
                                       /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      router.registerMessageHub("hub1", /* id= */ 1, callback);
  EXPECT_TRUE(messageHub.has_value());

  DynamicVector<EndpointInfo> endpoints;
  EXPECT_TRUE(router.forEachEndpointOfHub(
      /* messageHubId= */ 1, [&endpoints](const EndpointInfo &info) {
        endpoints.push_back(info);
        return false;
      }));
  EXPECT_EQ(endpoints.size(), 3);
  for (size_t i = 0; i < endpoints.size(); ++i) {
    EXPECT_EQ(endpoints[i].id, kEndpointInfos[i].id);
    EXPECT_EQ(std::strcmp(endpoints[i].name, kEndpointInfos[i].name), 0);
    EXPECT_EQ(endpoints[i].version, kEndpointInfos[i].version);
    EXPECT_EQ(endpoints[i].type, kEndpointInfos[i].type);
    EXPECT_EQ(endpoints[i].requiredPermissions,
              kEndpointInfos[i].requiredPermissions);
  }
}

TEST_F(MessageRouterTest, ForEachEndpointOfHubInvalidHub) {
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;
  MessageHubCallbackStoreData callback(/* message= */ nullptr,
                                       /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      router.registerMessageHub("hub1", /* id= */ 1, callback);
  EXPECT_TRUE(messageHub.has_value());

  DynamicVector<EndpointInfo> endpoints;
  EXPECT_FALSE(router.forEachEndpointOfHub(
      /* messageHubId= */ 2, [&endpoints](const EndpointInfo &info) {
        endpoints.push_back(info);
        return false;
      }));
  EXPECT_EQ(endpoints.size(), 0);
}

}  // namespace
}  // namespace chre::message
