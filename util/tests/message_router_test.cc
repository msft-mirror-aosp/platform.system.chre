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

#include "chre/util/dynamic_vector.h"
#include "chre/util/system/callback_allocator.h"
#include "chre/util/system/message_common.h"
#include "chre/util/system/message_router.h"
#include "chre_api/chre.h"

#include "pw_allocator/libc_allocator.h"
#include "pw_allocator/unique_ptr.h"

#include "gtest/gtest.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace chre::message {
namespace {

constexpr size_t kMaxMessageHubs = 3;
constexpr size_t kMaxSessions = 10;
constexpr size_t kMaxFreeCallbackRecords = kMaxSessions * 2;
constexpr size_t kNumEndpoints = 3;

const EndpointInfo kEndpointInfos[kNumEndpoints] = {
    EndpointInfo(/* id= */ 1, /* name= */ "endpoint1", /* version= */ 1,
                 EndpointType::NANOAPP, CHRE_MESSAGE_PERMISSION_NONE),
    EndpointInfo(/* id= */ 2, /* name= */ "endpoint2", /* version= */ 10,
                 EndpointType::HOST_NATIVE, CHRE_MESSAGE_PERMISSION_BLE),
    EndpointInfo(/* id= */ 3, /* name= */ "endpoint3", /* version= */ 100,
                 EndpointType::GENERIC, CHRE_MESSAGE_PERMISSION_AUDIO)};
const char kServiceDescriptorForEndpoint2[] = "TEST_SERVICE.TEST";

class MessageRouterTest : public ::testing::Test {};

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

  void onSessionOpenRequest(const Session & /* session */) override {}

  std::optional<EndpointId> getEndpointForService(
      const char *serviceDescriptor) override {
    if (serviceDescriptor != nullptr &&
        std::strcmp(serviceDescriptor, kServiceDescriptorForEndpoint2) == 0) {
      return kEndpointInfos[1].id;
    }
    return std::nullopt;
  }

  bool doesEndpointHaveService(EndpointId endpointId,
                               const char *serviceDescriptor) override {
    return serviceDescriptor != nullptr && endpointId == kEndpointInfos[1].id &&
           std::strcmp(serviceDescriptor, kServiceDescriptorForEndpoint2) == 0;
  }

  void onEndpointRegistered(MessageHubId /* messageHubId */,
                            EndpointId /* endpointId */) override {}

  void onEndpointUnregistered(MessageHubId /* messageHubId */,
                              EndpointId /* endpointId */) override {}
};

//! MessageHubCallback that stores the data passed to onMessageReceived and
//! onSessionClosed
class MessageHubCallbackStoreData : public MessageHubCallbackBase {
 public:
  MessageHubCallbackStoreData(Message *message, Session *session,
                              Reason *reason = nullptr,
                              Session *openedSession = nullptr)
      : mMessage(message),
        mSession(session),
        mReason(reason),
        mOpenedSession(openedSession) {}

  bool onMessageReceived(pw::UniquePtr<std::byte[]> &&data,
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
      mMessage->messageType = messageType;
      mMessage->messagePermissions = messagePermissions;
    }
    return true;
  }

  void onSessionClosed(const Session &session, Reason reason) override {
    if (mSession != nullptr) {
      *mSession = session;
    }
    if (mReason != nullptr) {
      *mReason = reason;
    }
  }

  void onSessionOpened(const Session &session) override {
    if (mOpenedSession != nullptr) {
      *mOpenedSession = session;
    }
  }

  void onEndpointRegistered(MessageHubId messageHubId,
                            EndpointId endpointId) override {
    mRegisteredEndpoints.insert(std::make_pair(messageHubId, endpointId));
  }

  void onEndpointUnregistered(MessageHubId messageHubId,
                              EndpointId endpointId) override {
    mRegisteredEndpoints.erase(std::make_pair(messageHubId, endpointId));
  }

  bool hasEndpointBeenRegistered(MessageHubId messageHubId,
                                 EndpointId endpointId) {
    return mRegisteredEndpoints.find(std::make_pair(
               messageHubId, endpointId)) != mRegisteredEndpoints.end();
  }

 private:
  Message *mMessage;
  Session *mSession;
  Reason *mReason;
  Session *mOpenedSession;
  std::set<std::pair<MessageHubId, EndpointId>> mRegisteredEndpoints;
};

//! MessageHubCallback that always fails to process messages
class MessageHubCallbackAlwaysFails : public MessageHubCallbackBase {
 public:
  MessageHubCallbackAlwaysFails(bool *wasMessageReceivedCalled,
                                bool *wasSessionClosedCalled)
      : mWasMessageReceivedCalled(wasMessageReceivedCalled),
        mWasSessionClosedCalled(wasSessionClosedCalled) {}

  bool onMessageReceived(pw::UniquePtr<std::byte[]> && /* data */,
                         uint32_t /* messageType */,
                         uint32_t /* messagePermissions */,
                         const Session & /* session */,
                         bool /* sentBySessionInitiator */) override {
    if (mWasMessageReceivedCalled != nullptr) {
      *mWasMessageReceivedCalled = true;
    }
    return false;
  }

  void onSessionClosed(const Session & /* session */,
                       Reason /* reason */) override {
    if (mWasSessionClosedCalled != nullptr) {
      *mWasSessionClosedCalled = true;
    }
  }

  void onSessionOpened(const Session & /* session */) override {}

 private:
  bool *mWasMessageReceivedCalled;
  bool *mWasSessionClosedCalled;
};

//! MessageHubCallback that tracks open session requests calls
class MessageHubCallbackOpenSessionRequest : public MessageHubCallbackBase {
 public:
  MessageHubCallbackOpenSessionRequest(bool *wasSessionOpenRequestCalled)
      : mWasSessionOpenRequestCalled(wasSessionOpenRequestCalled) {}

  void onSessionOpenRequest(const Session & /* session */) override {
    if (mWasSessionOpenRequestCalled != nullptr) {
      *mWasSessionOpenRequestCalled = true;
    }
  }

  bool onMessageReceived(pw::UniquePtr<std::byte[]> && /* data */,
                         uint32_t /* messageType */,
                         uint32_t /* messagePermissions */,
                         const Session & /* session */,
                         bool /* sentBySessionInitiator */) override {
    return true;
  }

  void onSessionClosed(const Session & /* session */,
                       Reason /* reason */) override {}

  void onSessionOpened(const Session & /* session */) override {}

 private:
  bool *mWasSessionOpenRequestCalled;
};

//! MessageHubCallback that calls MessageHub APIs during callbacks
class MessageHubCallbackCallsMessageHubApisDuringCallback
    : public MessageHubCallbackBase {
 public:
  bool onMessageReceived(pw::UniquePtr<std::byte[]> && /* data */,
                         uint32_t /* messageType */,
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

  void onSessionClosed(const Session & /* session */,
                       Reason /* reason */) override {
    if (mMessageHub != nullptr) {
      // Call a function that locks the MessageRouter mutex
      mMessageHub->openSession(kEndpointInfos[0].id, mMessageHub->getId(),
                               kEndpointInfos[1].id);
    }
  }

  void onSessionOpened(const Session & /* session */) override {
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

TEST_F(MessageRouterTest, GetEndpointForService) {
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;

  MessageHubCallbackStoreData callback(/* message= */ nullptr,
                                       /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub1 =
      router.registerMessageHub("hub1", /* id= */ 1, callback);
  EXPECT_TRUE(messageHub1.has_value());

  std::optional<Endpoint> endpoint = router.getEndpointForService(
      MESSAGE_HUB_ID_INVALID, kServiceDescriptorForEndpoint2);
  EXPECT_TRUE(endpoint.has_value());

  EXPECT_EQ(endpoint->messageHubId, messageHub1->getId());
  EXPECT_EQ(endpoint->endpointId, kEndpointInfos[1].id);
}

TEST_F(MessageRouterTest, DoesEndpointHaveService) {
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;

  MessageHubCallbackStoreData callback(/* message= */ nullptr,
                                       /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub1 =
      router.registerMessageHub("hub1", /* id= */ 1, callback);
  EXPECT_TRUE(messageHub1.has_value());

  EXPECT_TRUE(router.doesEndpointHaveService(messageHub1->getId(),
                                             kEndpointInfos[1].id,
                                             kServiceDescriptorForEndpoint2));
}

TEST_F(MessageRouterTest, GetEndpointForServiceBadServiceDescriptor) {
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;

  MessageHubCallbackStoreData callback(/* message= */ nullptr,
                                       /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub1 =
      router.registerMessageHub("hub1", /* id= */ 1, callback);
  EXPECT_TRUE(messageHub1.has_value());

  std::optional<Endpoint> endpoint = router.getEndpointForService(
      MESSAGE_HUB_ID_INVALID, "SERVICE_THAT_DOES_NOT_EXIST");
  EXPECT_FALSE(endpoint.has_value());

  std::optional<Endpoint> endpoint2 = router.getEndpointForService(
      MESSAGE_HUB_ID_INVALID, /* serviceDescriptor= */ nullptr);
  EXPECT_FALSE(endpoint2.has_value());
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
  messageHub2->onSessionOpenComplete(sessionId);

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

TEST_F(MessageRouterTest, RegisterSessionVerifyAllCallbacksAreCalled) {
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;
  Session sessionClosedFromCallback1;
  Session sessionClosedFromCallback2;
  Session sessionOpenedFromCallback1;
  Session sessionOpenedFromCallback2;
  Reason sessionCloseReason1;
  Reason sessionCloseReason2;
  MessageHubCallbackStoreData callback(
      /* message= */ nullptr, &sessionClosedFromCallback1, &sessionCloseReason1,
      &sessionOpenedFromCallback1);
  MessageHubCallbackStoreData callback2(
      /* message= */ nullptr, &sessionClosedFromCallback2, &sessionCloseReason2,
      &sessionOpenedFromCallback2);

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
  messageHub2->onSessionOpenComplete(sessionId);

  // Verify that onSessionOpened is called on both message hubs
  EXPECT_EQ(sessionOpenedFromCallback1.sessionId, sessionId);
  EXPECT_EQ(sessionOpenedFromCallback1.initiator.messageHubId,
            messageHub->getId());
  EXPECT_EQ(sessionOpenedFromCallback1.initiator.endpointId,
            kEndpointInfos[0].id);
  EXPECT_EQ(sessionOpenedFromCallback1.peer.messageHubId, messageHub2->getId());
  EXPECT_EQ(sessionOpenedFromCallback1.peer.endpointId, kEndpointInfos[1].id);

  EXPECT_EQ(sessionOpenedFromCallback2.sessionId, sessionId);
  EXPECT_EQ(sessionOpenedFromCallback2.initiator.messageHubId,
            messageHub->getId());
  EXPECT_EQ(sessionOpenedFromCallback2.initiator.endpointId,
            kEndpointInfos[0].id);
  EXPECT_EQ(sessionOpenedFromCallback2.peer.messageHubId, messageHub2->getId());
  EXPECT_EQ(sessionOpenedFromCallback2.peer.endpointId, kEndpointInfos[1].id);

  // Close the session with a reason
  Reason reason = Reason::TIMEOUT;
  EXPECT_TRUE(messageHub->closeSession(sessionId, reason));

  // Verify that onSessionClosed is called on both message hubs
  EXPECT_EQ(sessionClosedFromCallback1.sessionId, sessionId);
  EXPECT_EQ(sessionClosedFromCallback1.initiator.messageHubId,
            messageHub->getId());
  EXPECT_EQ(sessionClosedFromCallback1.initiator.endpointId,
            kEndpointInfos[0].id);
  EXPECT_EQ(sessionClosedFromCallback1.peer.messageHubId, messageHub2->getId());
  EXPECT_EQ(sessionClosedFromCallback1.peer.endpointId, kEndpointInfos[1].id);
  EXPECT_EQ(sessionCloseReason1, reason);

  EXPECT_EQ(sessionClosedFromCallback2.sessionId, sessionId);
  EXPECT_EQ(sessionClosedFromCallback2.initiator.messageHubId,
            messageHub->getId());
  EXPECT_EQ(sessionClosedFromCallback2.initiator.endpointId,
            kEndpointInfos[0].id);
  EXPECT_EQ(sessionClosedFromCallback2.peer.messageHubId, messageHub2->getId());
  EXPECT_EQ(sessionClosedFromCallback2.peer.endpointId, kEndpointInfos[1].id);
  EXPECT_EQ(sessionCloseReason2, reason);
}

TEST_F(MessageRouterTest, RegisterSessionGetsRejectedAndClosed) {
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;
  Session sessionFromCallback1;
  Session sessionFromCallback2;
  Reason sessionCloseReason;
  MessageHubCallbackStoreData callback(
      /* message= */ nullptr, &sessionFromCallback1, &sessionCloseReason);
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
  Reason reason = Reason::OPEN_ENDPOINT_SESSION_REQUEST_REJECTED;
  messageHub2->closeSession(sessionId, reason);

  // Get session from messageHub and ensure it is deleted
  std::optional<Session> sessionAfterRegistering =
      messageHub->getSessionWithId(sessionId);
  EXPECT_FALSE(sessionAfterRegistering.has_value());
  std::optional<Session> sessionAfterRegistering2 =
      messageHub2->getSessionWithId(sessionId);
  EXPECT_FALSE(sessionAfterRegistering2.has_value());

  // Close the session and verify it is closed on both message hubs
  EXPECT_EQ(sessionFromCallback1.sessionId, sessionId);
  EXPECT_EQ(sessionFromCallback1.initiator.messageHubId, messageHub->getId());
  EXPECT_EQ(sessionFromCallback1.initiator.endpointId, kEndpointInfos[0].id);
  EXPECT_EQ(sessionFromCallback1.peer.messageHubId, messageHub2->getId());
  EXPECT_EQ(sessionFromCallback1.peer.endpointId, kEndpointInfos[1].id);
  EXPECT_EQ(sessionCloseReason, reason);
}

TEST_F(MessageRouterTest, RegisterSessionSecondHubDoesNotRespond) {
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;
  bool wasOpenSessionRequestCalled = false;
  bool wasOpenSessionRequestCalled2 = false;
  MessageHubCallbackOpenSessionRequest callback(&wasOpenSessionRequestCalled);
  MessageHubCallbackOpenSessionRequest callback2(&wasOpenSessionRequestCalled2);

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

  // Message Hub 2 does not respond - verify the callback was called once
  EXPECT_FALSE(wasOpenSessionRequestCalled);
  EXPECT_TRUE(wasOpenSessionRequestCalled2);

  // Open session from messageHub:1 to messageHub2:2 - try again
  wasOpenSessionRequestCalled = false;
  SessionId sessionId2 = messageHub->openSession(
      kEndpointInfos[0].id, messageHub2->getId(), kEndpointInfos[1].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);
  EXPECT_EQ(sessionId, sessionId2);
  EXPECT_FALSE(wasOpenSessionRequestCalled);
  EXPECT_TRUE(wasOpenSessionRequestCalled2);

  // Respond then close the session
  messageHub2->onSessionOpenComplete(sessionId2);
  EXPECT_TRUE(messageHub->closeSession(sessionId));
}

TEST_F(MessageRouterTest, RegisterSessionWithServiceDescriptor) {
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
      kEndpointInfos[0].id, messageHub2->getId(), kEndpointInfos[1].id,
      kServiceDescriptorForEndpoint2);
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
  EXPECT_TRUE(sessionAfterRegistering->hasServiceDescriptor);
  EXPECT_STREQ(sessionAfterRegistering->serviceDescriptor,
               kServiceDescriptorForEndpoint2);
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

TEST_F(MessageRouterTest,
       RegisterSessionWithAndWithoutServiceDescriptorSameEndpoints) {
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

  // Open session from messageHub:1 to messageHub2:2 with service descriptor
  SessionId sessionId = messageHub->openSession(
      kEndpointInfos[0].id, messageHub2->getId(), kEndpointInfos[1].id,
      kServiceDescriptorForEndpoint2);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);

  // Open session from messageHub:1 to messageHub2:2 without service descriptor
  SessionId sessionId2 = messageHub->openSession(
      kEndpointInfos[0].id, messageHub2->getId(), kEndpointInfos[1].id);
  EXPECT_NE(sessionId2, SESSION_ID_INVALID);
  EXPECT_NE(sessionId2, sessionId);

  // Get the first session from messageHub and compare it with messageHub2
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
  EXPECT_TRUE(sessionAfterRegistering->hasServiceDescriptor);
  EXPECT_STREQ(sessionAfterRegistering->serviceDescriptor,
               kServiceDescriptorForEndpoint2);
  std::optional<Session> sessionAfterRegistering2 =
      messageHub2->getSessionWithId(sessionId);
  EXPECT_TRUE(sessionAfterRegistering2.has_value());
  EXPECT_EQ(*sessionAfterRegistering, *sessionAfterRegistering2);

  // Get the second session from messageHub and compare it with messageHub2
  std::optional<Session> sessionAfterRegistering3 =
      messageHub->getSessionWithId(sessionId2);
  EXPECT_TRUE(sessionAfterRegistering3.has_value());
  EXPECT_EQ(sessionAfterRegistering3->sessionId, sessionId2);
  EXPECT_EQ(sessionAfterRegistering3->initiator.messageHubId,
            messageHub->getId());
  EXPECT_EQ(sessionAfterRegistering3->initiator.endpointId,
            kEndpointInfos[0].id);
  EXPECT_EQ(sessionAfterRegistering3->peer.messageHubId, messageHub2->getId());
  EXPECT_EQ(sessionAfterRegistering3->peer.endpointId, kEndpointInfos[1].id);
  EXPECT_FALSE(sessionAfterRegistering3->hasServiceDescriptor);
  EXPECT_STREQ(sessionAfterRegistering3->serviceDescriptor, "");
  std::optional<Session> sessionAfterRegistering4 =
      messageHub2->getSessionWithId(sessionId2);
  EXPECT_TRUE(sessionAfterRegistering4.has_value());
  EXPECT_EQ(*sessionAfterRegistering3, *sessionAfterRegistering4);
}

TEST_F(MessageRouterTest, RegisterSessionWithBadServiceDescriptor) {
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
      kEndpointInfos[0].id, messageHub2->getId(), kEndpointInfos[2].id,
      kServiceDescriptorForEndpoint2);
  EXPECT_EQ(sessionId, SESSION_ID_INVALID);
}

TEST_F(MessageRouterTest, UnregisterMessageHubCausesSessionClosed) {
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
  messageHub2->onSessionOpenComplete(sessionId);

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

  // Close the session and verify it is closed on the other hub
  EXPECT_NE(*sessionAfterRegistering, sessionFromCallback1);
  messageHub2.reset();
  EXPECT_EQ(*sessionAfterRegistering, sessionFromCallback1);
  EXPECT_FALSE(messageHub->getSessionWithId(sessionId).has_value());
}

TEST_F(MessageRouterTest, RegisterSessionSameMessageHubIsValid) {
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
  EXPECT_NE(sessionId, SESSION_ID_INVALID);

  // Open session from messageHub:1 to messageHub:3
  sessionId = messageHub->openSession(kEndpointInfos[0].id, messageHub->getId(),
                                      kEndpointInfos[2].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);
}

TEST_F(MessageRouterTest, RegisterSessionReservedSessionIdAreRespected) {
  constexpr SessionId kReservedSessionId = 25;
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router(
      kReservedSessionId);
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

  // Open session from messageHub:1 to messageHub:2 more than the max number of
  // sessions - should wrap around
  for (size_t i = 0; i < kReservedSessionId * 2; ++i) {
    SessionId sessionId = messageHub->openSession(
        kEndpointInfos[0].id, messageHub2->getId(), kEndpointInfos[1].id);
    EXPECT_NE(sessionId, SESSION_ID_INVALID);
    messageHub2->onSessionOpenComplete(sessionId);
    EXPECT_TRUE(messageHub->closeSession(sessionId));
  }
}

TEST_F(MessageRouterTest, RegisterSessionOpenSessionNotReservedRegionRejected) {
  constexpr SessionId kReservedSessionId = 25;
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router(
      kReservedSessionId);
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

  // Open session from messageHub:1 to messageHub:2 and provide an invalid
  // session ID (not in the reserved range)
  SessionId sessionId = messageHub->openSession(
      kEndpointInfos[0].id, messageHub2->getId(), kEndpointInfos[1].id,
      /* serviceDescriptor= */ nullptr, kReservedSessionId / 2);
  EXPECT_EQ(sessionId, SESSION_ID_INVALID);
}

TEST_F(MessageRouterTest, RegisterSessionOpenSessionWithReservedSessionId) {
  constexpr SessionId kReservedSessionId = 25;
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router(
      kReservedSessionId);
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

  // Open session from messageHub:1 to messageHub:2 and provide a reserved
  // session ID
  SessionId sessionId = messageHub->openSession(
      kEndpointInfos[0].id, messageHub2->getId(), kEndpointInfos[1].id,
      /* serviceDescriptor= */ nullptr, kReservedSessionId);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);
  messageHub2->onSessionOpenComplete(sessionId);
  EXPECT_TRUE(messageHub->closeSession(sessionId));
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
  messageHub2->onSessionOpenComplete(sessionId);
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
  messageHub2->onSessionOpenComplete(sessionId);

  // Open session from messageHub2:2 to messageHub3:3
  SessionId sessionId2 = messageHub2->openSession(
      kEndpointInfos[1].id, messageHub3->getId(), kEndpointInfos[2].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);
  messageHub3->onSessionOpenComplete(sessionId2);

  // Open session from messageHub3:3 to messageHub1:1
  SessionId sessionId3 = messageHub3->openSession(
      kEndpointInfos[2].id, messageHub->getId(), kEndpointInfos[0].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);
  messageHub->onSessionOpenComplete(sessionId3);

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
  pw::allocator::LibCAllocator allocator = pw::allocator::GetLibCAllocator();
  pw::UniquePtr<std::byte[]> messageData =
      allocator.MakeUniqueArray<std::byte>(kMessageSize);
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
  messageHub2->onSessionOpenComplete(sessionId);

  // Open session from messageHub2:2 to messageHub3:3
  SessionId sessionId2 = messageHub2->openSession(
      kEndpointInfos[1].id, messageHub3->getId(), kEndpointInfos[2].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);
  messageHub3->onSessionOpenComplete(sessionId2);

  // Open session from messageHub3:3 to messageHub1:1
  SessionId sessionId3 = messageHub3->openSession(
      kEndpointInfos[2].id, messageHub->getId(), kEndpointInfos[0].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);
  messageHub->onSessionOpenComplete(sessionId3);

  // Send message from messageHub:1 to messageHub2:2
  ASSERT_TRUE(messageHub->sendMessage(std::move(messageData),
                                      /* messageType= */ 1,
                                      /* messagePermissions= */ 0, sessionId));
  EXPECT_EQ(messageFromCallback2.sessionId, sessionId);
  EXPECT_EQ(messageFromCallback2.sender.messageHubId, messageHub->getId());
  EXPECT_EQ(messageFromCallback2.sender.endpointId, kEndpointInfos[0].id);
  EXPECT_EQ(messageFromCallback2.recipient.messageHubId, messageHub2->getId());
  EXPECT_EQ(messageFromCallback2.recipient.endpointId, kEndpointInfos[1].id);
  EXPECT_EQ(messageFromCallback2.messageType, 1);
  EXPECT_EQ(messageFromCallback2.messagePermissions, 0);
  EXPECT_EQ(messageFromCallback2.data.size(), kMessageSize);
  for (size_t i = 0; i < kMessageSize; ++i) {
    EXPECT_EQ(messageFromCallback2.data[i], static_cast<std::byte>(i + 1));
  }

  messageData = allocator.MakeUniqueArray<std::byte>(kMessageSize);
  for (size_t i = 0; i < 5; ++i) {
    messageData[i] = static_cast<std::byte>(i + 1);
  }

  // Send message from messageHub2:2 to messageHub:1
  ASSERT_TRUE(messageHub2->sendMessage(std::move(messageData),
                                       /* messageType= */ 2,
                                       /* messagePermissions= */ 3, sessionId));
  EXPECT_EQ(messageFromCallback1.sessionId, sessionId);
  EXPECT_EQ(messageFromCallback1.sender.messageHubId, messageHub2->getId());
  EXPECT_EQ(messageFromCallback1.sender.endpointId, kEndpointInfos[1].id);
  EXPECT_EQ(messageFromCallback1.recipient.messageHubId, messageHub->getId());
  EXPECT_EQ(messageFromCallback1.recipient.endpointId, kEndpointInfos[0].id);
  EXPECT_EQ(messageFromCallback1.messageType, 2);
  EXPECT_EQ(messageFromCallback1.messagePermissions, 3);
  EXPECT_EQ(messageFromCallback1.data.size(), kMessageSize);
  for (size_t i = 0; i < kMessageSize; ++i) {
    EXPECT_EQ(messageFromCallback1.data[i], static_cast<std::byte>(i + 1));
  }
}

TEST_F(MessageRouterTest, SendMessageOnHalfOpenSessionIsRejected) {
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;
  constexpr size_t kMessageSize = 5;
  pw::allocator::LibCAllocator allocator = pw::allocator::GetLibCAllocator();
  pw::UniquePtr<std::byte[]> messageData =
      allocator.MakeUniqueArray<std::byte>(kMessageSize);
  for (size_t i = 0; i < 5; ++i) {
    messageData[i] = static_cast<std::byte>(i + 1);
  }

  Message messageFromCallback1;
  Message messageFromCallback2;
  Session sessionFromCallback1;
  Session sessionFromCallback2;
  MessageHubCallbackStoreData callback(&messageFromCallback1,
                                       &sessionFromCallback1);
  MessageHubCallbackStoreData callback2(&messageFromCallback2,
                                        &sessionFromCallback2);

  std::optional<MessageRouter::MessageHub> messageHub =
      router.registerMessageHub("hub1", /* id= */ 1, callback);
  EXPECT_TRUE(messageHub.has_value());
  std::optional<MessageRouter::MessageHub> messageHub2 =
      router.registerMessageHub("hub2", /* id= */ 2, callback2);
  EXPECT_TRUE(messageHub2.has_value());

  // Open session from messageHub:1 to messageHub2:2 but do not complete it
  SessionId sessionId = messageHub->openSession(
      kEndpointInfos[0].id, messageHub2->getId(), kEndpointInfos[1].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);

  // Try to send a message from messageHub:1 to messageHub2:2 - should fail
  EXPECT_FALSE(messageHub->sendMessage(std::move(messageData),
                                       /* messageType= */ 1,
                                       /* messagePermissions= */ 0, sessionId));

  // Now complete the session
  messageHub2->onSessionOpenComplete(sessionId);

  // Send message from messageHub:1 to messageHub2:2
  messageData = allocator.MakeUniqueArray<std::byte>(kMessageSize);
  for (size_t i = 0; i < 5; ++i) {
    messageData[i] = static_cast<std::byte>(i + 1);
  }

  ASSERT_TRUE(messageHub->sendMessage(std::move(messageData),
                                      /* messageType= */ 1,
                                      /* messagePermissions= */ 0, sessionId));
  EXPECT_EQ(messageFromCallback2.sessionId, sessionId);
  EXPECT_EQ(messageFromCallback2.sender.messageHubId, messageHub->getId());
  EXPECT_EQ(messageFromCallback2.sender.endpointId, kEndpointInfos[0].id);
  EXPECT_EQ(messageFromCallback2.recipient.messageHubId, messageHub2->getId());
  EXPECT_EQ(messageFromCallback2.recipient.endpointId, kEndpointInfos[1].id);
  EXPECT_EQ(messageFromCallback2.messageType, 1);
  EXPECT_EQ(messageFromCallback2.messagePermissions, 0);
  EXPECT_EQ(messageFromCallback2.data.size(), kMessageSize);
  for (size_t i = 0; i < kMessageSize; ++i) {
    EXPECT_EQ(messageFromCallback2.data[i], static_cast<std::byte>(i + 1));
  }

  messageData = allocator.MakeUniqueArray<std::byte>(kMessageSize);
  for (size_t i = 0; i < 5; ++i) {
    messageData[i] = static_cast<std::byte>(i + 1);
  }

  // Send message from messageHub2:2 to messageHub:1
  ASSERT_TRUE(messageHub2->sendMessage(std::move(messageData),
                                       /* messageType= */ 2,
                                       /* messagePermissions= */ 3, sessionId));
  EXPECT_EQ(messageFromCallback1.sessionId, sessionId);
  EXPECT_EQ(messageFromCallback1.sender.messageHubId, messageHub2->getId());
  EXPECT_EQ(messageFromCallback1.sender.endpointId, kEndpointInfos[1].id);
  EXPECT_EQ(messageFromCallback1.recipient.messageHubId, messageHub->getId());
  EXPECT_EQ(messageFromCallback1.recipient.endpointId, kEndpointInfos[0].id);
  EXPECT_EQ(messageFromCallback1.messageType, 2);
  EXPECT_EQ(messageFromCallback1.messagePermissions, 3);
  EXPECT_EQ(messageFromCallback1.data.size(), kMessageSize);
  for (size_t i = 0; i < kMessageSize; ++i) {
    EXPECT_EQ(messageFromCallback1.data[i], static_cast<std::byte>(i + 1));
  }
}

TEST_F(MessageRouterTest, SendMessageToSessionUsingPointerAndFreeCallback) {
  struct FreeCallbackContext {
    bool *freeCallbackCalled;
    std::byte *message;
    size_t length;
  };

  pw::Vector<CallbackAllocator<FreeCallbackContext>::CallbackRecord, 10>
      freeCallbackRecords;
  CallbackAllocator<FreeCallbackContext> allocator(
      [](std::byte *message, size_t length, FreeCallbackContext &&context) {
        *context.freeCallbackCalled =
            message == context.message && length == context.length;
      },
      freeCallbackRecords);

  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;
  constexpr size_t kMessageSize = 5;
  std::byte messageData[kMessageSize];
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
  messageHub2->onSessionOpenComplete(sessionId);

  // Open session from messageHub2:2 to messageHub3:3
  SessionId sessionId2 = messageHub2->openSession(
      kEndpointInfos[1].id, messageHub3->getId(), kEndpointInfos[2].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);
  messageHub3->onSessionOpenComplete(sessionId2);

  // Open session from messageHub3:3 to messageHub1:1
  SessionId sessionId3 = messageHub3->openSession(
      kEndpointInfos[2].id, messageHub->getId(), kEndpointInfos[0].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);
  messageHub->onSessionOpenComplete(sessionId3);

  // Send message from messageHub:1 to messageHub2:2
  bool freeCallbackCalled = false;
  FreeCallbackContext freeCallbackContext = {
      .freeCallbackCalled = &freeCallbackCalled,
      .message = messageData,
      .length = kMessageSize,
  };
  pw::UniquePtr<std::byte[]> data = allocator.MakeUniqueArrayWithCallback(
      messageData, kMessageSize, std::move(freeCallbackContext));
  ASSERT_NE(data.get(), nullptr);

  ASSERT_TRUE(messageHub->sendMessage(std::move(data),
                                      /* messageType= */ 1,
                                      /* messagePermissions= */ 0, sessionId));
  EXPECT_EQ(messageFromCallback2.sessionId, sessionId);
  EXPECT_EQ(messageFromCallback2.sender.messageHubId, messageHub->getId());
  EXPECT_EQ(messageFromCallback2.sender.endpointId, kEndpointInfos[0].id);
  EXPECT_EQ(messageFromCallback2.recipient.messageHubId, messageHub2->getId());
  EXPECT_EQ(messageFromCallback2.recipient.endpointId, kEndpointInfos[1].id);
  EXPECT_EQ(messageFromCallback2.messageType, 1);
  EXPECT_EQ(messageFromCallback2.messagePermissions, 0);
  EXPECT_EQ(messageFromCallback2.data.size(), kMessageSize);
  for (size_t i = 0; i < kMessageSize; ++i) {
    EXPECT_EQ(messageFromCallback2.data[i], static_cast<std::byte>(i + 1));
  }

  // Check if free callback was called
  EXPECT_FALSE(freeCallbackCalled);
  EXPECT_EQ(messageFromCallback2.data.get(), messageData);
  messageFromCallback2.data.Reset();
  EXPECT_TRUE(freeCallbackCalled);

  // Send message from messageHub2:2 to messageHub:1
  freeCallbackCalled = false;
  FreeCallbackContext freeCallbackContext2 = {
      .freeCallbackCalled = &freeCallbackCalled,
      .message = messageData,
      .length = kMessageSize,
  };
  data = allocator.MakeUniqueArrayWithCallback(messageData, kMessageSize,
                                               std::move(freeCallbackContext2));
  ASSERT_NE(data.get(), nullptr);

  ASSERT_TRUE(messageHub2->sendMessage(std::move(data),
                                       /* messageType= */ 2,
                                       /* messagePermissions= */ 3, sessionId));
  EXPECT_EQ(messageFromCallback1.sessionId, sessionId);
  EXPECT_EQ(messageFromCallback1.sender.messageHubId, messageHub2->getId());
  EXPECT_EQ(messageFromCallback1.sender.endpointId, kEndpointInfos[1].id);
  EXPECT_EQ(messageFromCallback1.recipient.messageHubId, messageHub->getId());
  EXPECT_EQ(messageFromCallback1.recipient.endpointId, kEndpointInfos[0].id);
  EXPECT_EQ(messageFromCallback1.messageType, 2);
  EXPECT_EQ(messageFromCallback1.messagePermissions, 3);
  EXPECT_EQ(messageFromCallback1.data.size(), kMessageSize);
  for (size_t i = 0; i < kMessageSize; ++i) {
    EXPECT_EQ(messageFromCallback1.data[i], static_cast<std::byte>(i + 1));
  }

  // Check if free callback was called
  EXPECT_FALSE(freeCallbackCalled);
  EXPECT_EQ(messageFromCallback1.data.get(), messageData);
  messageFromCallback1.data.Reset();
  EXPECT_TRUE(freeCallbackCalled);
}

TEST_F(MessageRouterTest, SendMessageToSessionInvalidHubAndSession) {
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;
  constexpr size_t kMessageSize = 5;
  pw::allocator::LibCAllocator allocator = pw::allocator::GetLibCAllocator();
  pw::UniquePtr<std::byte[]> messageData =
      allocator.MakeUniqueArray<std::byte>(kMessageSize);
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
  messageHub2->onSessionOpenComplete(sessionId);

  // Open session from messageHub2:2 to messageHub3:3
  SessionId sessionId2 = messageHub2->openSession(
      kEndpointInfos[1].id, messageHub3->getId(), kEndpointInfos[2].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);
  messageHub3->onSessionOpenComplete(sessionId2);

  // Open session from messageHub3:3 to messageHub1:1
  SessionId sessionId3 = messageHub3->openSession(
      kEndpointInfos[2].id, messageHub->getId(), kEndpointInfos[0].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);
  messageHub->onSessionOpenComplete(sessionId3);

  // Send message from messageHub:1 to messageHub2:2
  EXPECT_FALSE(messageHub->sendMessage(std::move(messageData),
                                       /* messageType= */ 1,
                                       /* messagePermissions= */ 0,
                                       sessionId2));
  EXPECT_FALSE(messageHub2->sendMessage(std::move(messageData),
                                        /* messageType= */ 2,
                                        /* messagePermissions= */ 3,
                                        sessionId3));
  EXPECT_FALSE(messageHub3->sendMessage(std::move(messageData),
                                        /* messageType= */ 2,
                                        /* messagePermissions= */ 3,
                                        sessionId));
}

TEST_F(MessageRouterTest, SendMessageToSessionCallbackFailureClosesSession) {
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;
  constexpr size_t kMessageSize = 5;
  pw::allocator::LibCAllocator allocator = pw::allocator::GetLibCAllocator();
  pw::UniquePtr<std::byte[]> messageData =
      allocator.MakeUniqueArray<std::byte>(kMessageSize);
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
  messageHub2->onSessionOpenComplete(sessionId);

  // Open session from messageHub2:2 to messageHub3:3
  SessionId sessionId2 = messageHub2->openSession(
      kEndpointInfos[1].id, messageHub3->getId(), kEndpointInfos[2].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);
  messageHub3->onSessionOpenComplete(sessionId2);

  // Open session from messageHub3:3 to messageHub1:1
  SessionId sessionId3 = messageHub3->openSession(
      kEndpointInfos[2].id, messageHub->getId(), kEndpointInfos[0].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);
  messageHub->onSessionOpenComplete(sessionId3);

  // Send message from messageHub2:2 to messageHub3:3
  EXPECT_FALSE(wasMessageReceivedCalled1);
  EXPECT_FALSE(wasMessageReceivedCalled2);
  EXPECT_FALSE(wasMessageReceivedCalled3);
  EXPECT_FALSE(messageHub->getSessionWithId(sessionId2).has_value());
  EXPECT_TRUE(messageHub2->getSessionWithId(sessionId2).has_value());
  EXPECT_TRUE(messageHub3->getSessionWithId(sessionId2).has_value());

  EXPECT_FALSE(messageHub2->sendMessage(std::move(messageData),
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
  messageData = allocator.MakeUniqueArray<std::byte>(kMessageSize);
  for (size_t i = 0; i < 5; ++i) {
    messageData[i] = static_cast<std::byte>(i + 1);
  }
  EXPECT_FALSE(messageHub2->sendMessage(std::move(messageData),
                                        /* messageType= */ 1,
                                        /* messagePermissions= */ 0,
                                        sessionId2));
  messageData = allocator.MakeUniqueArray<std::byte>(kMessageSize);
  for (size_t i = 0; i < 5; ++i) {
    messageData[i] = static_cast<std::byte>(i + 1);
  }
  EXPECT_FALSE(messageHub3->sendMessage(std::move(messageData),
                                        /* messageType= */ 1,
                                        /* messagePermissions= */ 0,
                                        sessionId2));
  EXPECT_FALSE(wasMessageReceivedCalled1);
  EXPECT_FALSE(wasMessageReceivedCalled2);
  EXPECT_FALSE(wasMessageReceivedCalled3);
}

TEST_F(MessageRouterTest, MessageHubCallbackCanCallOtherMessageHubAPIs) {
  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;
  constexpr size_t kMessageSize = 5;
  pw::allocator::LibCAllocator allocator = pw::allocator::GetLibCAllocator();
  pw::UniquePtr<std::byte[]> messageData =
      allocator.MakeUniqueArray<std::byte>(kMessageSize);
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
  messageHub2->onSessionOpenComplete(sessionId);

  // Open session from messageHub2:2 to messageHub3:3
  SessionId sessionId2 = messageHub2->openSession(
      kEndpointInfos[1].id, messageHub3->getId(), kEndpointInfos[2].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);
  messageHub3->onSessionOpenComplete(sessionId2);

  // Open session from messageHub3:3 to messageHub1:1
  SessionId sessionId3 = messageHub3->openSession(
      kEndpointInfos[2].id, messageHub->getId(), kEndpointInfos[0].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);
  messageHub->onSessionOpenComplete(sessionId3);

  // Send message from messageHub:1 to messageHub2:2
  EXPECT_TRUE(messageHub->sendMessage(std::move(messageData),
                                      /* messageType= */ 1,
                                      /* messagePermissions= */ 0, sessionId));

  // Send message from messageHub2:2 to messageHub:1
  messageData = allocator.MakeUniqueArray<std::byte>(kMessageSize);
  for (size_t i = 0; i < 5; ++i) {
    messageData[i] = static_cast<std::byte>(i + 1);
  }
  EXPECT_TRUE(messageHub2->sendMessage(std::move(messageData),
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
  EXPECT_EQ(endpoints.size(), kNumEndpoints);
  for (size_t i = 0; i < endpoints.size(); ++i) {
    EXPECT_EQ(endpoints[i].id, kEndpointInfos[i].id);
    EXPECT_STREQ(endpoints[i].name, kEndpointInfos[i].name);
    EXPECT_EQ(endpoints[i].version, kEndpointInfos[i].version);
    EXPECT_EQ(endpoints[i].type, kEndpointInfos[i].type);
    EXPECT_EQ(endpoints[i].requiredPermissions,
              kEndpointInfos[i].requiredPermissions);
  }
}

TEST_F(MessageRouterTest, ForEachEndpoint) {
  const char *kHubName = "hub1";
  constexpr MessageHubId kHubId = 1;

  MessageRouterWithStorage<kMaxMessageHubs, kMaxSessions> router;
  MessageHubCallbackStoreData callback(/* message= */ nullptr,
                                       /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      router.registerMessageHub(kHubName, kHubId, callback);
  EXPECT_TRUE(messageHub.has_value());

  DynamicVector<std::pair<MessageHubInfo, EndpointInfo>> endpoints;
  router.forEachEndpoint(
      [&endpoints](const MessageHubInfo &hubInfo, const EndpointInfo &info) {
        endpoints.push_back(std::make_pair(hubInfo, info));
      });
  EXPECT_EQ(endpoints.size(), kNumEndpoints);
  for (size_t i = 0; i < endpoints.size(); ++i) {
    EXPECT_EQ(endpoints[i].first.id, kHubId);
    EXPECT_STREQ(endpoints[i].first.name, kHubName);

    EXPECT_EQ(endpoints[i].second.id, kEndpointInfos[i].id);
    EXPECT_STREQ(endpoints[i].second.name, kEndpointInfos[i].name);
    EXPECT_EQ(endpoints[i].second.version, kEndpointInfos[i].version);
    EXPECT_EQ(endpoints[i].second.type, kEndpointInfos[i].type);
    EXPECT_EQ(endpoints[i].second.requiredPermissions,
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

TEST_F(MessageRouterTest, RegisterEndpointCallbacksAreCalled) {
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
  EXPECT_TRUE(messageHub.has_value());

  // Register the endpoint and verify that the callbacks were called
  EXPECT_TRUE(messageHub->registerEndpoint(kEndpointInfos[0].id));
  EXPECT_TRUE(callback2.hasEndpointBeenRegistered(messageHub->getId(),
                                                  kEndpointInfos[0].id));
}

TEST_F(MessageRouterTest, UnregisterEndpointCallbacksAreCalled) {
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
  EXPECT_TRUE(messageHub.has_value());

  // Register the endpoint and verify that the callbacks were called
  // only on the other hub
  EXPECT_TRUE(messageHub->registerEndpoint(kEndpointInfos[0].id));
  EXPECT_FALSE(callback.hasEndpointBeenRegistered(messageHub->getId(),
                                                  kEndpointInfos[0].id));
  EXPECT_TRUE(callback2.hasEndpointBeenRegistered(messageHub->getId(),
                                                  kEndpointInfos[0].id));

  // Unregister the endpoint and verify that the callbacks were called
  // only on the other hub
  EXPECT_TRUE(messageHub->unregisterEndpoint(kEndpointInfos[0].id));
  EXPECT_FALSE(callback.hasEndpointBeenRegistered(messageHub->getId(),
                                                  kEndpointInfos[0].id));
  EXPECT_FALSE(callback2.hasEndpointBeenRegistered(messageHub->getId(),
                                                   kEndpointInfos[0].id));
}

}  // namespace
}  // namespace chre::message
