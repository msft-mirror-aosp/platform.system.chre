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

#include "chre/core/event_loop_manager.h"
#include "chre/util/dynamic_vector.h"
#include "chre/util/system/message_common.h"
#include "chre/util/system/message_router.h"
#include "chre/util/system/napp_permissions.h"
#include "chre_api/chre.h"
#include "chre_api/chre/event.h"

#include "pw_allocator/allocator.h"
#include "pw_allocator/libc_allocator.h"
#include "pw_allocator/unique_ptr.h"
#include "pw_function/function.h"
#include "pw_intrusive_ptr/intrusive_ptr.h"

#include "gtest/gtest.h"
#include "inc/test_util.h"
#include "test_base.h"
#include "test_event.h"
#include "test_util.h"

#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>

namespace chre::message {
namespace {

CREATE_CHRE_TEST_EVENT(TEST_GET_EVENT_INFO, 0);
CREATE_CHRE_TEST_EVENT(TEST_OPEN_SESSION, 1);
CREATE_CHRE_TEST_EVENT(TEST_OPEN_DEFAULT_SESSION, 2);
CREATE_CHRE_TEST_EVENT(TEST_OPEN_SESSION_NANOAPP_TO_NANOAPP, 3);
CREATE_CHRE_TEST_EVENT(TEST_CLOSE_SESSION, 4);
CREATE_CHRE_TEST_EVENT(TEST_CLOSE_SESSION_NON_PARTY, 5);
CREATE_CHRE_TEST_EVENT(TEST_GET_SESSION_INFO_INVALID_SESSION, 6);
CREATE_CHRE_TEST_EVENT(TEST_SEND_MESSAGE, 7);
CREATE_CHRE_TEST_EVENT(TEST_SEND_MESSAGE_NO_FREE_CALLBACK, 8);
CREATE_CHRE_TEST_EVENT(TEST_SEND_MESSAGE_NANOAPP_TO_NANOAPP, 9);
CREATE_CHRE_TEST_EVENT(TEST_PUBLISH_SERVICE, 10);
CREATE_CHRE_TEST_EVENT(TEST_BAD_LEGACY_SERVICE_NAME, 11);
CREATE_CHRE_TEST_EVENT(TEST_OPEN_SESSION_WITH_SERVICE, 12);
CREATE_CHRE_TEST_EVENT(TEST_SUBSCRIBE_TO_READY_EVENT, 13);
CREATE_CHRE_TEST_EVENT(TEST_SUBSCRIBE_TO_READY_EVENT_ALREADY_EXISTS, 14);
CREATE_CHRE_TEST_EVENT(TEST_UNSUBSCRIBE_FROM_READY_EVENT, 15);
CREATE_CHRE_TEST_EVENT(TEST_SUBSCRIBE_TO_SERVICE_READY_EVENT, 16);
CREATE_CHRE_TEST_EVENT(TEST_UNSUBSCRIBE_FROM_SERVICE_READY_EVENT, 17);

constexpr size_t kNumEndpoints = 3;
constexpr size_t kMessageSize = 5;
constexpr MessageHubId kOtherMessageHubId = 0xDEADBEEFBEEFDEAD;

EndpointInfo kEndpointInfos[kNumEndpoints] = {
    EndpointInfo(/* id= */ 1, /* name= */ "endpoint1", /* version= */ 1,
                 EndpointType::NANOAPP, CHRE_MESSAGE_PERMISSION_NONE),
    EndpointInfo(/* id= */ 2, /* name= */ "endpoint2", /* version= */ 10,
                 EndpointType::HOST_NATIVE, CHRE_MESSAGE_PERMISSION_BLE),
    EndpointInfo(/* id= */ 3, /* name= */ "endpoint3", /* version= */ 100,
                 EndpointType::GENERIC, CHRE_MESSAGE_PERMISSION_AUDIO)};
EndpointInfo kDynamicEndpointInfo = EndpointInfo(
    /* id= */ 4, /* name= */ "DynamicallyRegisteredEndpoint",
    /* version= */ 1, EndpointType::NANOAPP, CHRE_MESSAGE_PERMISSION_NONE);

const char kServiceDescriptorForEndpoint2[] = "TEST_SERVICE.TEST";
const char kServiceDescriptorForDynamicEndpoint[] = "TEST_DYNAMIC_SERVICE";
const char kServiceDescriptorForNanoapp[] = "TEST_NANOAPP.TEST_SERVICE";
const uint64_t kLegacyServiceId = 0xDEADBEEFDEADBEEF;
const uint32_t kLegacyServiceVersion = 1;
const uint64_t kLegacyServiceNanoappId = 0xCAFECAFECAFECAFE;
const char kLegacyServiceName[] =
    "chre.nanoapp_0xCAFECAFECAFECAFE.service_0xDEADBEEFDEADBEEF";
const char kBadLegacyServiceName[] =
    "chre.nanoapp_0xCAFECAFECAFECAFE.service_0x0123456789ABCDEF";

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

  void onSessionOpened(const Session &session) override {
    bool shouldNotify = false;
    {
      std::unique_lock<std::mutex> lock(mSessionOpenedMutex);
      if (mSessionId == SESSION_ID_INVALID) {
        return;
      }
      if (mSessionId == session.sessionId) {
        shouldNotify = true;
        mSessionId = SESSION_ID_INVALID;
      }
    }
    if (shouldNotify) {
      mSessionOpenedCondVar.notify_one();
    }
  }

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
    if (serviceDescriptor == nullptr) {
      return false;
    }
    if (endpointId == kEndpointInfos[1].id) {
      return std::strcmp(serviceDescriptor, kServiceDescriptorForEndpoint2) ==
             0;
    }
    if (endpointId == kDynamicEndpointInfo.id) {
      return std::strcmp(serviceDescriptor,
                         kServiceDescriptorForDynamicEndpoint) == 0;
    }
    return false;
  }

  void forEachService(
      const pw::Function<bool(const EndpointInfo &, const ServiceInfo &)>
          &function) override {
    if (function(
            kEndpointInfos[1],
            ServiceInfo(kServiceDescriptorForEndpoint2, /* majorVersion= */ 1,
                        /* minorVersion= */ 0, RpcFormat::CUSTOM))) {
      return;
    }

    function(kDynamicEndpointInfo,
             ServiceInfo(kServiceDescriptorForDynamicEndpoint,
                         /* majorVersion= */ 1,
                         /* minorVersion= */ 0, RpcFormat::CUSTOM));
  }

  void onHubRegistered(const MessageHubInfo & /*info*/) override {}

  void onHubUnregistered(MessageHubId /*id*/) override {}

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

  void pw_recycle() override {
    delete this;
  }

  void openSessionAndWaitForOpen(
      const std::function<SessionId()> &openSession) {
    std::unique_lock<std::mutex> lock(mSessionOpenedMutex);
    mSessionId = openSession();
    mSessionOpenedCondVar.wait(
        lock, [this]() { return mSessionId == SESSION_ID_INVALID; });
  }

 private:
  std::set<std::pair<MessageHubId, EndpointId>> mRegisteredEndpoints;

  std::mutex mSessionOpenedMutex;
  std::condition_variable mSessionOpenedCondVar;
  SessionId mSessionId = SESSION_ID_INVALID;
};

//! MessageHubCallback that stores the data passed to onMessageReceived and
//! onSessionClosed
class MessageHubCallbackStoreData : public MessageHubCallbackBase {
 public:
  MessageHubCallbackStoreData(Message *message, Session *session)
      : mMessage(message), mSession(session), mMessageHub(nullptr) {}

  bool onMessageReceived(pw::UniquePtr<std::byte[]> &&data,
                         uint32_t messageType, uint32_t messagePermissions,
                         const Session &session,
                         bool sentBySessionInitiator) override {
    if (mMessage != nullptr) {
      mMessage->sender =
          sentBySessionInitiator ? session.initiator : session.peer;
      mMessage->recipient =
          sentBySessionInitiator ? session.peer : session.initiator;
      mMessage->sessionId = session.sessionId;
      mMessage->data = std::move(data);
      mMessage->messageType = messageType;
      mMessage->messagePermissions = messagePermissions;
    }
    return true;
  }

  void onSessionClosed(const Session &session, Reason /* reason */) override {
    if (mSession != nullptr) {
      *mSession = session;
    }
  }

  void onSessionOpenRequest(const Session &session) override {
    if (mMessageHub != nullptr) {
      mMessageHub->onSessionOpenComplete(session.sessionId);
    }
  }

  void setMessageHub(MessageRouter::MessageHub *messageHub) {
    mMessageHub = messageHub;
  }

 private:
  Message *mMessage;
  Session *mSession;
  MessageRouter::MessageHub *mMessageHub;
};

// Creates a message with data from 1 to messageSize
pw::UniquePtr<std::byte[]> createMessageData(
    pw::allocator::Allocator &allocator, size_t messageSize) {
  pw::UniquePtr<std::byte[]> messageData =
      allocator.MakeUniqueArray<std::byte>(messageSize);
  EXPECT_NE(messageData.get(), nullptr);
  for (size_t i = 0; i < messageSize; ++i) {
    messageData[i] = static_cast<std::byte>(i + 1);
  }
  return messageData;
}

class ChreMessageHubTest : public TestBase {};

TEST_F(ChreMessageHubTest, NanoappsAreEndpointsToChreMessageHub) {
  class App : public TestNanoapp {
   public:
    App() : TestNanoapp(TestNanoappInfo{.name = "TEST1", .id = 0x1234}) {}
  };

  uint64_t appId = loadNanoapp(MakeUnique<App>());

  std::optional<EndpointInfo> endpointInfoForApp =
      MessageRouterSingleton::get()->getEndpointInfo(
          EventLoopManagerSingleton::get()
              ->getChreMessageHubManager()
              .kChreMessageHubId,
          appId);
  ASSERT_TRUE(endpointInfoForApp.has_value());

  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);

  EXPECT_EQ(endpointInfoForApp->id, nanoapp->getAppId());
  EXPECT_STREQ(endpointInfoForApp->name, nanoapp->getAppName());
  EXPECT_EQ(endpointInfoForApp->version, nanoapp->getAppVersion());
  EXPECT_EQ(endpointInfoForApp->type, EndpointType::NANOAPP);
  EXPECT_EQ(endpointInfoForApp->requiredPermissions,
            nanoapp->getAppPermissions());
}

//! Nanoapp used to test getting endpoint info
class EndpointInfoTestApp : public TestNanoapp {
 public:
  EndpointInfoTestApp(const TestNanoappInfo &info) : TestNanoapp(info) {}

  void handleEvent(uint32_t, uint16_t eventType,
                   const void *eventData) override {
    switch (eventType) {
      case CHRE_EVENT_TEST_EVENT: {
        auto event = static_cast<const TestEvent *>(eventData);
        switch (event->type) {
          case TEST_GET_EVENT_INFO: {
            for (size_t i = 0; i < kNumEndpoints; ++i) {
              chreMsgEndpointInfo info = {};
              EXPECT_TRUE(chreMsgGetEndpointInfo(kOtherMessageHubId,
                                                 kEndpointInfos[i].id, &info));

              EXPECT_EQ(info.hubId, kOtherMessageHubId);
              EXPECT_EQ(info.endpointId, kEndpointInfos[i].id);
              EXPECT_EQ(info.version, kEndpointInfos[i].version);
              EXPECT_EQ(info.type,
                        EventLoopManagerSingleton::get()
                            ->getChreMessageHubManager()
                            .toChreEndpointType(kEndpointInfos[i].type));
              EXPECT_EQ(info.requiredPermissions,
                        kEndpointInfos[i].requiredPermissions);
              EXPECT_STREQ(info.name, kEndpointInfos[i].name);
            }
            triggerWait(TEST_GET_EVENT_INFO);
            break;
          }
        }
        break;
      }
      default: {
        break;
      }
    }
  }
};

TEST_F(ChreMessageHubTest, NanoappGetsEndpointInfo) {
  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<EndpointInfoTestApp>(

      TestNanoappInfo{.name = "TEST_GET_ENDPOINT_INFO", .id = 0x1234}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);

  // Create the other hub
  pw::IntrusivePtr<MessageHubCallbackStoreData> callback =
      pw::MakeRefCounted<MessageHubCallbackStoreData>(/* message= */ nullptr,
                                                      /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback->setMessageHub(&(*messageHub));

  // Test getting endpoint info
  sendEventToNanoappAndWait(appId, TEST_GET_EVENT_INFO, TEST_GET_EVENT_INFO);
}

TEST_F(ChreMessageHubTest, MultipleNanoappsAreEndpointsToChreMessageHub) {
  class App : public TestNanoapp {
   public:
    App() : TestNanoapp(TestNanoappInfo{.name = "TEST1", .id = 0x1234}) {}
  };

  class App2 : public TestNanoapp {
   public:
    App2() : TestNanoapp(TestNanoappInfo{.name = "TEST2", .id = 0x2}) {}
  };

  uint64_t appId = loadNanoapp(MakeUnique<App>());
  uint64_t appId2 = loadNanoapp(MakeUnique<App2>());
  constexpr size_t kNumNanoapps = 2;
  Nanoapp *nanoapps[kNumNanoapps] = {getNanoappByAppId(appId),
                                     getNanoappByAppId(appId2)};
  ASSERT_NE(nanoapps[0], nullptr);
  ASSERT_NE(nanoapps[1], nullptr);

  DynamicVector<EndpointInfo> endpointInfos;
  EXPECT_TRUE(MessageRouterSingleton::get()->forEachEndpointOfHub(
      EventLoopManagerSingleton::get()
          ->getChreMessageHubManager()
          .kChreMessageHubId,
      [&endpointInfos](const EndpointInfo &endpointInfo) {
        endpointInfos.push_back(endpointInfo);
        return false;
      }));
  EXPECT_EQ(endpointInfos.size(), 2);

  // Endpoint information should be nanoapp information
  for (size_t i = 0; i < kNumNanoapps; ++i) {
    EXPECT_EQ(endpointInfos[i].id, nanoapps[i]->getAppId());
    EXPECT_STREQ(endpointInfos[i].name, nanoapps[i]->getAppName());
    EXPECT_EQ(endpointInfos[i].version, nanoapps[i]->getAppVersion());
    EXPECT_EQ(endpointInfos[i].type, EndpointType::NANOAPP);
    EXPECT_EQ(endpointInfos[i].requiredPermissions,
              nanoapps[i]->getAppPermissions());
  }
}

//! Nanoapp used to test sending messages from a generic endpoint to a nanoapp
class MessageTestApp : public TestNanoapp {
 public:
  MessageTestApp(bool &messageReceivedAndValidated, bool &sessionClosed,
                 const TestNanoappInfo &info)
      : TestNanoapp(info),
        mMessageReceivedAndValidated(messageReceivedAndValidated),
        mSessionClosed(sessionClosed) {}

  void handleEvent(uint32_t, uint16_t eventType,
                   const void *eventData) override {
    switch (eventType) {
      case CHRE_EVENT_MSG_FROM_ENDPOINT: {
        auto *message =
            static_cast<const struct chreMsgMessageFromEndpointData *>(
                eventData);
        EXPECT_EQ(message->messageType, 1);
        EXPECT_EQ(message->messagePermissions, 0);
        EXPECT_EQ(message->messageSize, kMessageSize);

        auto *messageData = static_cast<const std::byte *>(message->message);
        for (size_t i = 0; i < kMessageSize; ++i) {
          EXPECT_EQ(messageData[i], static_cast<std::byte>(i + 1));
        }
        mMessageReceivedAndValidated = true;
        triggerWait(CHRE_EVENT_MSG_FROM_ENDPOINT);
        break;
      }
      case CHRE_EVENT_MSG_SESSION_CLOSED: {
        auto *session =
            static_cast<const struct chreMsgSessionInfo *>(eventData);
        EXPECT_EQ(session->hubId, kOtherMessageHubId);
        EXPECT_EQ(session->endpointId, kEndpointInfos[0].id);
        mSessionClosed = true;
        triggerWait(CHRE_EVENT_MSG_SESSION_CLOSED);
        break;
      }
      default: {
        break;
      }
    }
  }

  bool &mMessageReceivedAndValidated;
  bool &mSessionClosed;
};

TEST_F(ChreMessageHubTest, SendMessageToNanoapp) {
  constexpr uint64_t kNanoappId = 0x1234;

  bool messageReceivedAndValidated = false;
  bool sessionClosed = false;

  // Create the message
  pw::allocator::LibCAllocator allocator = pw::allocator::GetLibCAllocator();
  pw::UniquePtr<std::byte[]> messageData =
      createMessageData(allocator, kMessageSize);

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<MessageTestApp>(
      messageReceivedAndValidated, sessionClosed,
      TestNanoappInfo{.name = "TEST1", .id = kNanoappId}));
  TestNanoapp *testNanoapp = queryNanoapp(appId);
  ASSERT_NE(testNanoapp, nullptr);

  // Create the other hub
  pw::IntrusivePtr<MessageHubCallbackStoreData> callback =
      pw::MakeRefCounted<MessageHubCallbackStoreData>(/* message= */ nullptr,
                                                      /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback->setMessageHub(&(*messageHub));

  // Open the session from the other hub:1 to the nanoapp
  SessionId sessionId = SESSION_ID_INVALID;
  callback->openSessionAndWaitForOpen([&sessionId, &messageHub]() {
    sessionId = messageHub->openSession(kEndpointInfos[0].id,
                                        EventLoopManagerSingleton::get()
                                            ->getChreMessageHubManager()
                                            .kChreMessageHubId,
                                        kNanoappId);
    EXPECT_NE(sessionId, SESSION_ID_INVALID);
    return sessionId;
  });

  // Send the message to the nanoapp
  ASSERT_TRUE(messageHub->sendMessage(std::move(messageData),
                                      /* messageType= */ 1,
                                      /* messagePermissions= */ 0, sessionId));
  testNanoapp->wait(CHRE_EVENT_MSG_FROM_ENDPOINT);
  EXPECT_TRUE(messageReceivedAndValidated);

  // Close the session
  EXPECT_TRUE(messageHub->closeSession(sessionId));
  testNanoapp->wait(CHRE_EVENT_MSG_SESSION_CLOSED);
  EXPECT_TRUE(sessionClosed);
}

//! Nanoapp used to test sending messages from a generic endpoint to a nanoapp
//! with a different permissions set
class MessagePermissionTestApp : public MessageTestApp {
 public:
  MessagePermissionTestApp(bool &messageReceivedAndValidated,
                           bool &sessionClosed, const TestNanoappInfo &info)
      : MessageTestApp(messageReceivedAndValidated, sessionClosed, info) {}
};

TEST_F(ChreMessageHubTest, SendMessageToNanoappPermissionFailure) {
  constexpr uint64_t kNanoappId = 0x1234;

  bool messageReceivedAndValidated = false;
  bool sessionClosed = false;

  pw::allocator::LibCAllocator allocator = pw::allocator::GetLibCAllocator();
  pw::UniquePtr<std::byte[]> messageData =
      allocator.MakeUniqueArray<std::byte>(kMessageSize);
  for (size_t i = 0; i < kMessageSize; ++i) {
    messageData[i] = static_cast<std::byte>(i + 1);
  }

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<MessagePermissionTestApp>(
      messageReceivedAndValidated, sessionClosed,
      TestNanoappInfo{
          .name = "TEST1", .id = kNanoappId, .perms = CHRE_PERMS_BLE}));
  TestNanoapp *testNanoapp = queryNanoapp(appId);
  ASSERT_NE(testNanoapp, nullptr);

  // Create the other hub
  pw::IntrusivePtr<MessageHubCallbackStoreData> callback =
      pw::MakeRefCounted<MessageHubCallbackStoreData>(/* message= */ nullptr,
                                                      /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback->setMessageHub(&(*messageHub));

  // Open the session from the other hub:1 to the nanoapp
  SessionId sessionId = SESSION_ID_INVALID;
  callback->openSessionAndWaitForOpen([&sessionId, &messageHub]() {
    sessionId = messageHub->openSession(kEndpointInfos[0].id,
                                        EventLoopManagerSingleton::get()
                                            ->getChreMessageHubManager()
                                            .kChreMessageHubId,
                                        kNanoappId);
    EXPECT_NE(sessionId, SESSION_ID_INVALID);
    return sessionId;
  });

  // Send the message to the nanoapp
  ASSERT_TRUE(messageHub->sendMessage(
      std::move(messageData),
      /* messageType= */ 1,
      /* messagePermissions= */ CHRE_PERMS_AUDIO | CHRE_PERMS_GNSS, sessionId));

  // Wait for the session to close due to the permission failure
  testNanoapp->wait(CHRE_EVENT_MSG_SESSION_CLOSED);
  EXPECT_FALSE(messageReceivedAndValidated);
  EXPECT_TRUE(sessionClosed);
}

//! Nanoapp used to test opening sessions and sending messages from a nanoapp
//! to a generic endpoint
class SessionAndMessageTestApp : public TestNanoapp {
 public:
  SessionAndMessageTestApp(SessionId &sessionId, const TestNanoappInfo &info)
      : TestNanoapp(info), mSessionId(sessionId) {}

  void handleEvent(uint32_t, uint16_t eventType,
                   const void *eventData) override {
    switch (eventType) {
      case CHRE_EVENT_MSG_SESSION_OPENED: {
        // Verify the session info from the event is correct
        auto sessionInfo = static_cast<const chreMsgSessionInfo *>(eventData);
        EXPECT_EQ(sessionInfo->hubId, mToMessageHubId);
        EXPECT_EQ(sessionInfo->endpointId, mToEndpointId);
        EXPECT_STREQ(sessionInfo->serviceDescriptor, "");
        EXPECT_NE(sessionInfo->sessionId, UINT16_MAX);
        EXPECT_EQ(sessionInfo->reason,
                  chreMsgEndpointReason::CHRE_MSG_ENDPOINT_REASON_UNSPECIFIED);
        mSessionId = sessionInfo->sessionId;

        // Get the session info and verify it is correct
        struct chreMsgSessionInfo sessionInfo2;
        EXPECT_TRUE(chreMsgSessionGetInfo(mSessionId, &sessionInfo2));
        EXPECT_EQ(sessionInfo2.hubId, mToMessageHubId);
        EXPECT_EQ(sessionInfo2.endpointId, mToEndpointId);
        EXPECT_STREQ(sessionInfo2.serviceDescriptor, "");
        EXPECT_EQ(sessionInfo2.sessionId, mSessionId);
        EXPECT_EQ(sessionInfo2.reason,
                  chreMsgEndpointReason::CHRE_MSG_ENDPOINT_REASON_UNSPECIFIED);
        triggerWait(CHRE_EVENT_MSG_SESSION_OPENED);
        break;
      }
      case CHRE_EVENT_MSG_SESSION_CLOSED: {
        // Verify the session info from the event is correct
        auto sessionInfo = static_cast<const chreMsgSessionInfo *>(eventData);
        EXPECT_EQ(sessionInfo->hubId, mToMessageHubId);
        EXPECT_EQ(sessionInfo->endpointId, mToEndpointId);
        EXPECT_STREQ(sessionInfo->serviceDescriptor, "");
        EXPECT_EQ(sessionInfo->sessionId, mSessionId);
        triggerWait(CHRE_EVENT_MSG_SESSION_CLOSED);
        break;
      }
      case CHRE_EVENT_MSG_FROM_ENDPOINT: {
        auto messageData =
            static_cast<const chreMsgMessageFromEndpointData *>(eventData);
        EXPECT_EQ(messageData->messageType, 1);
        EXPECT_EQ(messageData->messagePermissions,
                  CHRE_MESSAGE_PERMISSION_NONE);
        EXPECT_EQ(messageData->messageSize, kMessageSize);

        auto message = reinterpret_cast<const uint8_t *>(messageData->message);
        for (size_t i = 0; i < kMessageSize; ++i) {
          EXPECT_EQ(message[i], i + 1);
        }
        EXPECT_EQ(messageData->sessionId, mSessionId);
        triggerWait(CHRE_EVENT_MSG_FROM_ENDPOINT);
        break;
      }
      case CHRE_EVENT_TEST_EVENT: {
        auto event = static_cast<const TestEvent *>(eventData);
        switch (event->type) {
          case TEST_OPEN_SESSION: {
            // Open the session from the nanoapp to the other hub:0
            mToMessageHubId = kOtherMessageHubId;
            mToEndpointId = kEndpointInfos[0].id;
            EXPECT_TRUE(
                chreMsgSessionOpenAsync(mToMessageHubId, mToEndpointId,
                                        /* serviceDescriptor= */ nullptr));
            mSessionId = UINT16_MAX;
            break;
          }
          case TEST_OPEN_DEFAULT_SESSION: {
            // Open the default session from the nanoapp to the other hub:1
            mToMessageHubId = kOtherMessageHubId;
            mToEndpointId = kEndpointInfos[1].id;
            EXPECT_TRUE(
                chreMsgSessionOpenAsync(CHRE_MSG_HUB_ID_ANY, mToEndpointId,
                                        /* serviceDescriptor= */ nullptr));
            mSessionId = UINT16_MAX;
            break;
          }
          case TEST_OPEN_SESSION_NANOAPP_TO_NANOAPP: {
            // Open a session from the nanoapp to itself
            mToMessageHubId = CHRE_PLATFORM_ID;
            mToEndpointId = id();
            EXPECT_TRUE(
                chreMsgSessionOpenAsync(mToMessageHubId, mToEndpointId,
                                        /* serviceDescriptor= */ nullptr));
            mSessionId = UINT16_MAX;
            break;
          }
          case TEST_CLOSE_SESSION: {
            // Close the session
            EXPECT_TRUE(chreMsgSessionCloseAsync(mSessionId));
            break;
          }
          case TEST_CLOSE_SESSION_NON_PARTY: {
            ASSERT_NE(event->data, nullptr);
            SessionId sessionId = *static_cast<SessionId *>(event->data);

            // Close the session that was opened by the other nanoapp
            EXPECT_FALSE(chreMsgSessionCloseAsync(sessionId));
            triggerWait(TEST_CLOSE_SESSION_NON_PARTY);
            break;
          }
          case TEST_GET_SESSION_INFO_INVALID_SESSION: {
            struct chreMsgSessionInfo sessionInfo;
            EXPECT_NE(mSessionId, SESSION_ID_INVALID);
            EXPECT_FALSE(chreMsgSessionGetInfo(mSessionId, &sessionInfo));
            triggerWait(TEST_GET_SESSION_INFO_INVALID_SESSION);
            break;
          }
          case TEST_SEND_MESSAGE: {
            EXPECT_TRUE(chreMsgSend(
                reinterpret_cast<void *>(kMessage), kMessageSize,
                /* messageType= */ 1, mSessionId, CHRE_MESSAGE_PERMISSION_NONE,
                [](void *message, size_t length) {
                  EXPECT_EQ(message, kMessage);
                  EXPECT_EQ(length, kMessageSize);
                }));
            triggerWait(TEST_SEND_MESSAGE);
            break;
          }
          case TEST_SEND_MESSAGE_NO_FREE_CALLBACK: {
            EXPECT_TRUE(chreMsgSend(
                reinterpret_cast<void *>(kMessage), kMessageSize,
                /* messageType= */ 1, mSessionId, CHRE_MESSAGE_PERMISSION_NONE,
                /* freeCallback= */ [](void * /*message*/, size_t /*length*/) {
                }));
            triggerWait(TEST_SEND_MESSAGE_NO_FREE_CALLBACK);
            break;
          }
          case TEST_SEND_MESSAGE_NANOAPP_TO_NANOAPP: {
            EXPECT_TRUE(chreMsgSend(
                reinterpret_cast<void *>(kMessage), kMessageSize,
                /* messageType= */ 1, mSessionId, CHRE_MESSAGE_PERMISSION_NONE,
                /* freeCallback= */ [](void * /*message*/, size_t /*length*/) {
                }));
            break;
          }
        }
        break;
      }
      default: {
        break;
      }
    }
  }

  static uint8_t kMessage[kMessageSize];

  SessionId &mSessionId;
  MessageHubId mToMessageHubId = MESSAGE_HUB_ID_INVALID;
  EndpointId mToEndpointId = ENDPOINT_ID_INVALID;
};
uint8_t SessionAndMessageTestApp::kMessage[kMessageSize] = {1, 2, 3, 4, 5};

TEST_F(ChreMessageHubTest, NanoappOpensSessionWithGenericEndpoint) {
  SessionId sessionId = SESSION_ID_INVALID;

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<SessionAndMessageTestApp>(
      sessionId, TestNanoappInfo{.name = "TEST_OPEN_SESSION", .id = 0x1234}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);
  TestNanoapp *testNanoapp = queryNanoapp(appId);
  ASSERT_NE(testNanoapp, nullptr);

  // Create the other hub
  pw::IntrusivePtr<MessageHubCallbackStoreData> callback =
      pw::MakeRefCounted<MessageHubCallbackStoreData>(/* message= */ nullptr,
                                                      /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback->setMessageHub(&(*messageHub));

  // Test opening session
  sendEventToNanoappAndWait(appId, TEST_OPEN_SESSION,
                            CHRE_EVENT_MSG_SESSION_OPENED);

  // Verify the other hub received the correct session information
  std::optional<Session> session = messageHub->getSessionWithId(sessionId);
  ASSERT_TRUE(session.has_value());
  EXPECT_EQ(session->sessionId, sessionId);
  EXPECT_EQ(session->initiator.messageHubId, EventLoopManagerSingleton::get()
                                                 ->getChreMessageHubManager()
                                                 .kChreMessageHubId);
  EXPECT_EQ(session->initiator.endpointId, nanoapp->getAppId());
  EXPECT_EQ(session->peer.messageHubId, kOtherMessageHubId);
  EXPECT_EQ(session->peer.endpointId, kEndpointInfos[0].id);

  testNanoapp->doActionAndWait(
      [&messageHub, &session]() {
        messageHub->closeSession(session->sessionId);
        return true;
      },
      CHRE_EVENT_MSG_SESSION_CLOSED);
}

TEST_F(ChreMessageHubTest, NanoappTriesToCloseNonPartySession) {
  SessionId sessionId = SESSION_ID_INVALID;

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<SessionAndMessageTestApp>(
      sessionId, TestNanoappInfo{.name = "TEST_OPEN_SESSION", .id = 0x1234}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);
  TestNanoapp *testNanoapp = queryNanoapp(appId);
  ASSERT_NE(testNanoapp, nullptr);

  // Load the nanoapp
  uint64_t appId2 = loadNanoapp(MakeUnique<SessionAndMessageTestApp>(
      sessionId,
      TestNanoappInfo{.name = "TEST_OPEN_SESSION_NON_PARTY", .id = 0x1235}));
  Nanoapp *nanoapp2 = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp2, nullptr);

  // Create the other hub
  pw::IntrusivePtr<MessageHubCallbackStoreData> callback =
      pw::MakeRefCounted<MessageHubCallbackStoreData>(/* message= */ nullptr,
                                                      /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback->setMessageHub(&(*messageHub));

  // Test opening session
  sendEventToNanoappAndWait(appId, TEST_OPEN_SESSION,
                            CHRE_EVENT_MSG_SESSION_OPENED);

  // Now close the session and expect failure
  sendEventToNanoappAndWait(appId, TEST_CLOSE_SESSION_NON_PARTY, &sessionId,
                            TEST_CLOSE_SESSION_NON_PARTY);

  testNanoapp->doActionAndWait(
      [&messageHub, &sessionId]() {
        messageHub->closeSession(sessionId);
        return true;
      },
      CHRE_EVENT_MSG_SESSION_CLOSED);
}

TEST_F(ChreMessageHubTest, NanoappOpensDefaultSessionWithGenericEndpoint) {
  SessionId sessionId = SESSION_ID_INVALID;

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<SessionAndMessageTestApp>(
      sessionId,
      TestNanoappInfo{.name = "TEST_OPEN_DEFAULT_SESSION", .id = 0x1234}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);
  TestNanoapp *testNanoapp = queryNanoapp(appId);
  ASSERT_NE(testNanoapp, nullptr);

  // Create the other hub
  pw::IntrusivePtr<MessageHubCallbackStoreData> callback =
      pw::MakeRefCounted<MessageHubCallbackStoreData>(/* message= */ nullptr,
                                                      /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback->setMessageHub(&(*messageHub));

  // Test opening the default session
  sendEventToNanoappAndWait(appId, TEST_OPEN_DEFAULT_SESSION,
                            CHRE_EVENT_MSG_SESSION_OPENED);

  // Verify the other hub received the correct session information
  std::optional<Session> session = messageHub->getSessionWithId(sessionId);
  ASSERT_TRUE(session.has_value());

  EXPECT_EQ(session->sessionId, sessionId);
  EXPECT_EQ(session->initiator.messageHubId, EventLoopManagerSingleton::get()
                                                 ->getChreMessageHubManager()
                                                 .kChreMessageHubId);
  EXPECT_EQ(session->initiator.endpointId, nanoapp->getAppId());
  EXPECT_EQ(session->peer.messageHubId, kOtherMessageHubId);
  EXPECT_EQ(session->peer.endpointId, kEndpointInfos[1].id);

  testNanoapp->doActionAndWait(
      [&messageHub, &sessionId]() {
        messageHub->closeSession(sessionId);
        return true;
      },
      CHRE_EVENT_MSG_SESSION_CLOSED);
}

TEST_F(ChreMessageHubTest, NanoappClosesSessionWithGenericEndpoint) {
  Session session;
  SessionId sessionId = SESSION_ID_INVALID;

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<SessionAndMessageTestApp>(
      sessionId, TestNanoappInfo{.name = "TEST_OPEN_SESSION", .id = 0x1234}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);

  // Create the other hub
  pw::IntrusivePtr<MessageHubCallbackStoreData> callback =
      pw::MakeRefCounted<MessageHubCallbackStoreData>(/* message= */ nullptr,
                                                      &session);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback->setMessageHub(&(*messageHub));

  // Test opening session
  sendEventToNanoappAndWait(appId, TEST_OPEN_SESSION,
                            CHRE_EVENT_MSG_SESSION_OPENED);

  // Now close the session
  sendEventToNanoappAndWait(appId, TEST_CLOSE_SESSION,
                            CHRE_EVENT_MSG_SESSION_CLOSED);

  // Verify the other hub received the correct session information
  EXPECT_EQ(session.sessionId, sessionId);
  EXPECT_EQ(session.initiator.messageHubId, EventLoopManagerSingleton::get()
                                                ->getChreMessageHubManager()
                                                .kChreMessageHubId);
  EXPECT_EQ(session.initiator.endpointId, nanoapp->getAppId());
  EXPECT_EQ(session.peer.messageHubId, kOtherMessageHubId);
  EXPECT_EQ(session.peer.endpointId, kEndpointInfos[0].id);
}

TEST_F(ChreMessageHubTest, OtherHubClosesNanoappSessionWithGenericEndpoint) {
  Session session;
  SessionId sessionId = SESSION_ID_INVALID;

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<SessionAndMessageTestApp>(
      sessionId, TestNanoappInfo{.name = "TEST_OPEN_SESSION", .id = 0x1234}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);
  TestNanoapp *testNanoapp = queryNanoapp(appId);
  ASSERT_NE(testNanoapp, nullptr);

  // Create the other hub
  pw::IntrusivePtr<MessageHubCallbackStoreData> callback =
      pw::MakeRefCounted<MessageHubCallbackStoreData>(/* message= */ nullptr,
                                                      &session);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback->setMessageHub(&(*messageHub));

  // Test opening session
  sendEventToNanoappAndWait(appId, TEST_OPEN_SESSION,
                            CHRE_EVENT_MSG_SESSION_OPENED);

  // Now close the session and wait for the event to be processed
  EXPECT_TRUE(messageHub->closeSession(sessionId));
  testNanoapp->wait(CHRE_EVENT_MSG_SESSION_CLOSED);

  // Verify the other hub received the correct session information
  EXPECT_EQ(session.sessionId, sessionId);
  EXPECT_EQ(session.initiator.messageHubId, EventLoopManagerSingleton::get()
                                                ->getChreMessageHubManager()
                                                .kChreMessageHubId);
  EXPECT_EQ(session.initiator.endpointId, nanoapp->getAppId());
  EXPECT_EQ(session.peer.messageHubId, kOtherMessageHubId);
  EXPECT_EQ(session.peer.endpointId, kEndpointInfos[0].id);
}

TEST_F(ChreMessageHubTest, NanoappGetSessionInfoForNonPartySession) {
  Session session;
  SessionId sessionId = SESSION_ID_INVALID;

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<SessionAndMessageTestApp>(
      sessionId, TestNanoappInfo{.name = "TEST_OPEN_SESSION", .id = 0x1234}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);

  // Create the other hubs
  pw::IntrusivePtr<MessageHubCallbackStoreData> callback =
      pw::MakeRefCounted<MessageHubCallbackStoreData>(/* message= */ nullptr,
                                                      &session);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback->setMessageHub(&(*messageHub));

  pw::IntrusivePtr<MessageHubCallbackStoreData> callback2 =
      pw::MakeRefCounted<MessageHubCallbackStoreData>(/* message= */ nullptr,
                                                      &session);
  std::optional<MessageRouter::MessageHub> messageHub2 =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB2", kOtherMessageHubId + 1, callback2);
  ASSERT_TRUE(messageHub2.has_value());
  callback2->setMessageHub(&(*messageHub2));

  // Open a session not involving the nanoapps
  sessionId = messageHub->openSession(
      kEndpointInfos[0].id, kOtherMessageHubId + 1, kEndpointInfos[1].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);

  // Tell the nanoapp to get the session info for our session
  sendEventToNanoappAndWait(appId, TEST_GET_SESSION_INFO_INVALID_SESSION,
                            TEST_GET_SESSION_INFO_INVALID_SESSION);
}

TEST_F(ChreMessageHubTest, NanoappSendsMessageToGenericEndpoint) {
  SessionId sessionId = SESSION_ID_INVALID;
  Message message;

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<SessionAndMessageTestApp>(
      sessionId, TestNanoappInfo{.name = "TEST_OPEN_SESSION", .id = 0x1234}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);
  TestNanoapp *testNanoapp = queryNanoapp(appId);
  ASSERT_NE(testNanoapp, nullptr);

  // Create the other hub
  pw::IntrusivePtr<MessageHubCallbackStoreData> callback =
      pw::MakeRefCounted<MessageHubCallbackStoreData>(&message,
                                                      /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback->setMessageHub(&(*messageHub));

  // Test opening session
  sendEventToNanoappAndWait(appId, TEST_OPEN_SESSION,
                            CHRE_EVENT_MSG_SESSION_OPENED);

  // Send the message to the other hub and verify it was received
  sendEventToNanoappAndWait(appId, TEST_SEND_MESSAGE,
                            TEST_SEND_MESSAGE);

  EXPECT_EQ(message.data.size(), kMessageSize);
  for (size_t i = 0; i < kMessageSize; ++i) {
    EXPECT_EQ(message.data[i],
              static_cast<std::byte>(SessionAndMessageTestApp::kMessage[i]));
  }
  EXPECT_EQ(message.messageType, 1);
  EXPECT_EQ(message.messagePermissions, CHRE_MESSAGE_PERMISSION_NONE);

  testNanoapp->doActionAndWait(
      [&messageHub, &sessionId]() {
        messageHub->closeSession(sessionId);
        return true;
      },
      CHRE_EVENT_MSG_SESSION_CLOSED);
}

TEST_F(ChreMessageHubTest,
       NanoappSendsMessageWithNoFreeCallbackToGenericEndpoint) {
  SessionId sessionId = SESSION_ID_INVALID;
  Message message;

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<SessionAndMessageTestApp>(
      sessionId, TestNanoappInfo{.name = "TEST_OPEN_SESSION", .id = 0x1234}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);
  TestNanoapp *testNanoapp = queryNanoapp(appId);
  ASSERT_NE(testNanoapp, nullptr);

  // Create the other hub
  pw::IntrusivePtr<MessageHubCallbackStoreData> callback =
      pw::MakeRefCounted<MessageHubCallbackStoreData>(&message,
                                                      /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback->setMessageHub(&(*messageHub));

  // Test opening session
  sendEventToNanoappAndWait(appId, TEST_OPEN_SESSION,
                            CHRE_EVENT_MSG_SESSION_OPENED);

  // Send the message to the other hub and verify it was received
  sendEventToNanoappAndWait(appId, TEST_SEND_MESSAGE_NO_FREE_CALLBACK,
                            TEST_SEND_MESSAGE_NO_FREE_CALLBACK);

  EXPECT_EQ(message.data.size(), kMessageSize);
  for (size_t i = 0; i < kMessageSize; ++i) {
    EXPECT_EQ(message.data[i],
              static_cast<std::byte>(SessionAndMessageTestApp::kMessage[i]));
  }
  EXPECT_EQ(message.messageType, 1);
  EXPECT_EQ(message.messagePermissions, CHRE_MESSAGE_PERMISSION_NONE);

  testNanoapp->doActionAndWait(
      [&messageHub, &sessionId]() {
        messageHub->closeSession(sessionId);
        return true;
      },
      CHRE_EVENT_MSG_SESSION_CLOSED);
}

TEST_F(ChreMessageHubTest, NanoappGetsMessageFromGenericEndpoint) {
  SessionId sessionId = SESSION_ID_INVALID;
  Message message;

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<SessionAndMessageTestApp>(
      sessionId, TestNanoappInfo{.name = "TEST_OPEN_SESSION", .id = 0x1234}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);
  TestNanoapp *testNanoapp = queryNanoapp(appId);
  ASSERT_NE(testNanoapp, nullptr);

  // Create the other hub
  pw::IntrusivePtr<MessageHubCallbackStoreData> callback =
      pw::MakeRefCounted<MessageHubCallbackStoreData>(&message,
                                                      /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback->setMessageHub(&(*messageHub));

  // Test opening session
  sendEventToNanoappAndWait(appId, TEST_OPEN_SESSION,
                            CHRE_EVENT_MSG_SESSION_OPENED);

  // Send the message to the nanoapp and verify it was received
  pw::allocator::LibCAllocator allocator = pw::allocator::GetLibCAllocator();
  pw::UniquePtr<std::byte[]> messageData =
      createMessageData(allocator, kMessageSize);
  EXPECT_TRUE(messageHub->sendMessage(std::move(messageData),
                                      /* messageType= */ 1,
                                      CHRE_MESSAGE_PERMISSION_NONE, sessionId));

  testNanoapp->wait(CHRE_EVENT_MSG_FROM_ENDPOINT);

  testNanoapp->doActionAndWait(
      [&messageHub, &sessionId]() {
        messageHub->closeSession(sessionId);
        return true;
      },
      CHRE_EVENT_MSG_SESSION_CLOSED);
}

TEST_F(ChreMessageHubTest, NanoappSendsMessageToNanoapp) {
  Session session;
  SessionId sessionId = SESSION_ID_INVALID;

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<SessionAndMessageTestApp>(
      sessionId, TestNanoappInfo{.name = "TEST_SEND_MESSAGE_NANOAPP_TO_NANOAPP",
                                 .id = 0x1234}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);

  // Test opening the session to itself
  sendEventToNanoappAndWait(appId, TEST_OPEN_SESSION_NANOAPP_TO_NANOAPP,
                            CHRE_EVENT_MSG_SESSION_OPENED);

  // Send the message to itself
  sendEventToNanoappAndWait(appId, TEST_SEND_MESSAGE_NANOAPP_TO_NANOAPP,
                            CHRE_EVENT_MSG_FROM_ENDPOINT);

  // Wait for the session to be closed
  sendEventToNanoappAndWait(appId, TEST_CLOSE_SESSION,
                            CHRE_EVENT_MSG_SESSION_CLOSED);
}

//! Nanoapp used to test opening sessions with services
class ServiceSessionTestApp : public TestNanoapp {
 public:
  ServiceSessionTestApp(const TestNanoappInfo &info) : TestNanoapp(info) {}

  bool start() override {
    chreNanoappRpcService serviceInfo;
    serviceInfo.id = kLegacyServiceId;
    serviceInfo.version = kLegacyServiceVersion;
    EXPECT_TRUE(chrePublishRpcServices(&serviceInfo,
                                       /* numServices= */ 1));
    return true;
  }

  void handleEvent(uint32_t, uint16_t eventType,
                   const void *eventData) override {
    switch (eventType) {
      case CHRE_EVENT_MSG_SESSION_OPENED: {
        // Verify the session info from the event is correct
        auto sessionInfo = static_cast<const chreMsgSessionInfo *>(eventData);
        EXPECT_EQ(sessionInfo->hubId, kOtherMessageHubId);
        EXPECT_EQ(sessionInfo->reason,
                  chreMsgEndpointReason::CHRE_MSG_ENDPOINT_REASON_UNSPECIFIED);

        if (std::strcmp(sessionInfo->serviceDescriptor,
                        kServiceDescriptorForEndpoint2) == 0) {
          EXPECT_EQ(sessionInfo->endpointId, kEndpointInfos[1].id);
          EXPECT_NE(sessionInfo->sessionId, UINT16_MAX);
        }
        triggerWait(CHRE_EVENT_MSG_SESSION_OPENED);
        break;
      }
      case CHRE_EVENT_MSG_SESSION_CLOSED: {
        triggerWait(CHRE_EVENT_MSG_SESSION_CLOSED);
        break;
      }
      case CHRE_EVENT_TEST_EVENT: {
        auto event = static_cast<const TestEvent *>(eventData);
        switch (event->type) {
          case TEST_PUBLISH_SERVICE: {
            chreMsgServiceInfo serviceInfo;
            serviceInfo.majorVersion = 1;
            serviceInfo.minorVersion = 0;
            serviceInfo.serviceDescriptor = kServiceDescriptorForNanoapp;
            serviceInfo.serviceFormat = CHRE_MSG_ENDPOINT_SERVICE_FORMAT_CUSTOM;
            EXPECT_TRUE(
                chreMsgPublishServices(&serviceInfo, /* numServices= */ 1));
            triggerWait(TEST_PUBLISH_SERVICE);
            break;
          }
          case TEST_BAD_LEGACY_SERVICE_NAME: {
            chreMsgServiceInfo serviceInfo;
            serviceInfo.majorVersion = 1;
            serviceInfo.minorVersion = 0;
            serviceInfo.serviceDescriptor = kBadLegacyServiceName;
            serviceInfo.serviceFormat = CHRE_MSG_ENDPOINT_SERVICE_FORMAT_CUSTOM;
            EXPECT_FALSE(
                chreMsgPublishServices(&serviceInfo, /* numServices= */ 1));
            triggerWait(TEST_BAD_LEGACY_SERVICE_NAME);
            break;
          }
          case TEST_OPEN_SESSION_WITH_SERVICE: {
            EXPECT_TRUE(chreMsgSessionOpenAsync(
                kOtherMessageHubId, kEndpointInfos[1].id,
                kServiceDescriptorForEndpoint2));
            break;
          }
        }
        break;
      }
      default: {
        break;
      }
    }
  }
};

TEST_F(ChreMessageHubTest, OpenSessionWithNanoappService) {
  constexpr uint64_t kNanoappId = 0x1234;

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<ServiceSessionTestApp>(

      TestNanoappInfo{.name = "TEST_OPEN_SESSION_WITH_SERVICE",
                      .id = kNanoappId}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);
  TestNanoapp *testNanoapp = queryNanoapp(appId);
  ASSERT_NE(testNanoapp, nullptr);

  // Create the other hub
  pw::IntrusivePtr<MessageHubCallbackStoreData> callback =
      pw::MakeRefCounted<MessageHubCallbackStoreData>(/* message= */ nullptr,
                                                      /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback->setMessageHub(&(*messageHub));

  // Nanoapp publishes the service
  sendEventToNanoappAndWait(appId, TEST_PUBLISH_SERVICE, TEST_PUBLISH_SERVICE);

  // Open the session from the other hub:1 to the nanoapp with the service
  SessionId sessionId = SESSION_ID_INVALID;
  callback->openSessionAndWaitForOpen([&sessionId, &messageHub]() {
    sessionId =
        messageHub->openSession(kEndpointInfos[0].id,
                                EventLoopManagerSingleton::get()
                                    ->getChreMessageHubManager()
                                    .kChreMessageHubId,
                                kNanoappId, kServiceDescriptorForNanoapp);
    EXPECT_NE(sessionId, SESSION_ID_INVALID);
    return sessionId;
  });

  // Wait for the nanoapp to receive the session open event
  testNanoapp->wait(CHRE_EVENT_MSG_SESSION_OPENED);

  testNanoapp->doActionAndWait(
      [&messageHub, &sessionId]() {
        messageHub->closeSession(sessionId);
        return true;
      },
      CHRE_EVENT_MSG_SESSION_CLOSED);
}

TEST_F(ChreMessageHubTest, OpenTwoSessionsWithNanoappServiceAndNoService) {
  constexpr uint64_t kNanoappId = 0x1234;

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<ServiceSessionTestApp>(

      TestNanoappInfo{.name = "TEST_OPEN_SESSION_WITH_SERVICE",
                      .id = kNanoappId}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);
  TestNanoapp *testNanoapp = queryNanoapp(appId);
  ASSERT_NE(testNanoapp, nullptr);

  // Create the other hub
  pw::IntrusivePtr<MessageHubCallbackStoreData> callback =
      pw::MakeRefCounted<MessageHubCallbackStoreData>(/* message= */ nullptr,
                                                      /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback->setMessageHub(&(*messageHub));

  // Nanoapp publishes the service
  sendEventToNanoappAndWait(appId, TEST_PUBLISH_SERVICE, TEST_PUBLISH_SERVICE);

  // Open the session from the other hub:1 to the nanoapp with the service
  SessionId sessionId = SESSION_ID_INVALID;
  callback->openSessionAndWaitForOpen([&sessionId, &messageHub]() {
    sessionId =
        messageHub->openSession(kEndpointInfos[0].id,
                                EventLoopManagerSingleton::get()
                                    ->getChreMessageHubManager()
                                    .kChreMessageHubId,
                                kNanoappId, kServiceDescriptorForNanoapp);
    EXPECT_NE(sessionId, SESSION_ID_INVALID);
    return sessionId;
  });

  // Wait for the nanoapp to receive the session open event
  testNanoapp->wait(CHRE_EVENT_MSG_SESSION_OPENED);

  // Open the other session from the other hub:1 to the nanoapp
  SessionId sessionId2 = SESSION_ID_INVALID;
  callback->openSessionAndWaitForOpen([&sessionId, &sessionId2, &messageHub]() {
    sessionId2 = messageHub->openSession(kEndpointInfos[0].id,
                                         EventLoopManagerSingleton::get()
                                             ->getChreMessageHubManager()
                                             .kChreMessageHubId,
                                         kNanoappId);
    EXPECT_NE(sessionId2, SESSION_ID_INVALID);
    EXPECT_NE(sessionId, sessionId2);
    return sessionId2;
  });

  // Wait for the nanoapp to receive the session open event
  testNanoapp->wait(CHRE_EVENT_MSG_SESSION_OPENED);

  testNanoapp->doActionAndWait(
      [&messageHub, &sessionId]() {
        messageHub->closeSession(sessionId);
        return true;
      },
      CHRE_EVENT_MSG_SESSION_CLOSED);

  testNanoapp->doActionAndWait(
      [&messageHub, &sessionId2]() {
        messageHub->closeSession(sessionId2);
        return true;
      },
      CHRE_EVENT_MSG_SESSION_CLOSED);
}

TEST_F(ChreMessageHubTest, OpenSessionWithNanoappLegacyService) {
  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<ServiceSessionTestApp>(

      TestNanoappInfo{.name = "TEST_OPEN_SESSION_WITH_LEGACY_SERVICE",
                      .id = kLegacyServiceNanoappId}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);
  TestNanoapp *testNanoapp = queryNanoapp(appId);
  ASSERT_NE(testNanoapp, nullptr);

  // Create the other hub
  pw::IntrusivePtr<MessageHubCallbackStoreData> callback =
      pw::MakeRefCounted<MessageHubCallbackStoreData>(/* message= */ nullptr,
                                                      /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback->setMessageHub(&(*messageHub));

  // Open the session from the other hub:1 to the nanoapp with the service
  SessionId sessionId = SESSION_ID_INVALID;
  callback->openSessionAndWaitForOpen([&sessionId, &messageHub]() {
    sessionId =
        messageHub->openSession(kEndpointInfos[0].id,
                                EventLoopManagerSingleton::get()
                                    ->getChreMessageHubManager()
                                    .kChreMessageHubId,
                                kLegacyServiceNanoappId, kLegacyServiceName);
    EXPECT_NE(sessionId, SESSION_ID_INVALID);
    return sessionId;
  });

  testNanoapp->doActionAndWait(
      [&messageHub, &sessionId]() {
        messageHub->closeSession(sessionId);
        return true;
      },
      CHRE_EVENT_MSG_SESSION_CLOSED);
}

TEST_F(ChreMessageHubTest, ForEachServiceNanoappLegacyService) {
  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<ServiceSessionTestApp>(

      TestNanoappInfo{.name = "TEST_FOR_EACH_SERVICE_LEGACY_SERVICE",
                      .id = kLegacyServiceNanoappId}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);

  // Create the other hub
  pw::IntrusivePtr<MessageHubCallbackStoreData> callback =
      pw::MakeRefCounted<MessageHubCallbackStoreData>(/* message= */ nullptr,
                                                      /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback->setMessageHub(&(*messageHub));

  // Find the service
  MessageRouterSingleton::get()->forEachService(
      [&](const MessageHubInfo &hub, const EndpointInfo &endpoint,
          const ServiceInfo &service) {
        if (hub.id == EventLoopManagerSingleton::get()
                          ->getChreMessageHubManager()
                          .kChreMessageHubId) {
          EXPECT_EQ(endpoint.id, kLegacyServiceNanoappId);
          EXPECT_STREQ(service.serviceDescriptor, kLegacyServiceName);
          EXPECT_EQ(service.majorVersion, 1);
          EXPECT_EQ(service.minorVersion, 0);
          EXPECT_EQ(service.format, RpcFormat::PW_RPC_PROTOBUF);
          return true;
        }
        return false;
      });
}

TEST_F(ChreMessageHubTest, NanoappFailsToPublishLegacyServiceInNewWay) {
  constexpr uint64_t kNanoappId = 0x1234;

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<ServiceSessionTestApp>(

      TestNanoappInfo{.name = "TEST_BAD_LEGACY_SERVICE_NAME",
                      .id = kNanoappId}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);

  // Create the other hub
  pw::IntrusivePtr<MessageHubCallbackStoreData> callback =
      pw::MakeRefCounted<MessageHubCallbackStoreData>(/* message= */ nullptr,
                                                      /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback->setMessageHub(&(*messageHub));

  // Nanoapp publishes the service
  sendEventToNanoappAndWait(appId, TEST_BAD_LEGACY_SERVICE_NAME,
                            TEST_BAD_LEGACY_SERVICE_NAME);
}

TEST_F(ChreMessageHubTest, NanoappOpensSessionWithService) {
  constexpr uint64_t kNanoappId = 0x1234;

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<ServiceSessionTestApp>(

      TestNanoappInfo{.name = "TEST_OPEN_SESSION_WITH_SERVICE",
                      .id = kNanoappId}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);

  // Create the other hub
  pw::IntrusivePtr<MessageHubCallbackStoreData> callback =
      pw::MakeRefCounted<MessageHubCallbackStoreData>(/* message= */ nullptr,
                                                      /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback->setMessageHub(&(*messageHub));

  // Nanoapp opens the session with the service
  sendEventToNanoappAndWait(appId, TEST_OPEN_SESSION_WITH_SERVICE,
                            CHRE_EVENT_MSG_SESSION_OPENED);
}

TEST_F(ChreMessageHubTest, NanoappUnloadUnregistersProvidedServices) {
  constexpr uint64_t kNanoappId = 0x1234;

  // Create the other hub
  pw::IntrusivePtr<MessageHubCallbackStoreData> callback =
      pw::MakeRefCounted<MessageHubCallbackStoreData>(/* message= */ nullptr,
                                                      /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback->setMessageHub(&(*messageHub));

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<ServiceSessionTestApp>(

      TestNanoappInfo{.name = "TEST_UNLOAD_UNREGISTERS_PROVIDED_SERVICES",
                      .id = kNanoappId}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  callback->setMessageHub(&(*messageHub));

  // Nanoapp publishes the service
  sendEventToNanoappAndWait(appId, TEST_PUBLISH_SERVICE, TEST_PUBLISH_SERVICE);

  // Get the endpoint ID for the service
  std::optional<Endpoint> endpoint =
      MessageRouterSingleton::get()->getEndpointForService(
          EventLoopManagerSingleton::get()
              ->getChreMessageHubManager()
              .kChreMessageHubId,
          kServiceDescriptorForNanoapp);
  EXPECT_TRUE(endpoint.has_value());
  EXPECT_EQ(endpoint->messageHubId, EventLoopManagerSingleton::get()
                                        ->getChreMessageHubManager()
                                        .kChreMessageHubId);
  EXPECT_EQ(endpoint->endpointId, kNanoappId);

  // Unload the nanoapp
  unloadNanoapp(appId);

  // Load another nanoapp. This forces this thread to wait for the finish
  // load nanoapp event to process, which is after the cleanup event.
  loadNanoapp(MakeUnique<TestNanoapp>());

  // The service should be gone
  endpoint = MessageRouterSingleton::get()->getEndpointForService(
      EventLoopManagerSingleton::get()
          ->getChreMessageHubManager()
          .kChreMessageHubId,
      kServiceDescriptorForNanoapp);
  EXPECT_FALSE(endpoint.has_value());
}

//! Nanoapp used to test endpoint registration and ready events
class EndpointRegistrationTestApp : public TestNanoapp {
 public:
  EndpointRegistrationTestApp(const TestNanoappInfo &info)
      : TestNanoapp(info) {}

  void handleEvent(uint32_t, uint16_t eventType,
                   const void *eventData) override {
    switch (eventType) {
      case CHRE_EVENT_MSG_ENDPOINT_READY: {
        auto event = static_cast<const chreMsgEndpointReadyEvent *>(eventData);
        EXPECT_EQ(event->hubId, kOtherMessageHubId);
        EXPECT_EQ(event->endpointId, mEndpointId);
        triggerWait(CHRE_EVENT_MSG_ENDPOINT_READY);
        break;
      }
      case CHRE_EVENT_MSG_SERVICE_READY: {
        auto event = static_cast<const chreMsgServiceReadyEvent *>(eventData);
        EXPECT_EQ(event->hubId, kOtherMessageHubId);
        EXPECT_EQ(event->endpointId, kDynamicEndpointInfo.id);
        EXPECT_STREQ(event->serviceDescriptor,
                     kServiceDescriptorForDynamicEndpoint);
        triggerWait(CHRE_EVENT_MSG_SERVICE_READY);
        break;
      }
      case CHRE_EVENT_TEST_EVENT: {
        auto event = static_cast<const TestEvent *>(eventData);
        switch (event->type) {
          case TEST_SUBSCRIBE_TO_READY_EVENT: {
            mEndpointId = kDynamicEndpointInfo.id;
            EXPECT_TRUE(chreMsgConfigureEndpointReadyEvents(
                kOtherMessageHubId, mEndpointId,
                /* enable= */ true));
            triggerWait(TEST_SUBSCRIBE_TO_READY_EVENT);
            break;
          }
          case TEST_SUBSCRIBE_TO_READY_EVENT_ALREADY_EXISTS: {
            mEndpointId = kEndpointInfos[1].id;
            EXPECT_TRUE(chreMsgConfigureEndpointReadyEvents(
                kOtherMessageHubId, mEndpointId,
                /* enable= */ true));
            break;
          }
          case TEST_UNSUBSCRIBE_FROM_READY_EVENT: {
            EXPECT_TRUE(chreMsgConfigureEndpointReadyEvents(
                kOtherMessageHubId, mEndpointId,
                /* enable= */ false));
            triggerWait(TEST_UNSUBSCRIBE_FROM_READY_EVENT);
            break;
          }
          case TEST_SUBSCRIBE_TO_SERVICE_READY_EVENT: {
            EXPECT_TRUE(chreMsgConfigureServiceReadyEvents(
                kOtherMessageHubId, kServiceDescriptorForDynamicEndpoint,
                /* enable= */ true));
            triggerWait(TEST_SUBSCRIBE_TO_SERVICE_READY_EVENT);
            break;
          }
          case TEST_UNSUBSCRIBE_FROM_SERVICE_READY_EVENT: {
            EXPECT_TRUE(chreMsgConfigureServiceReadyEvents(
                kOtherMessageHubId, kServiceDescriptorForDynamicEndpoint,
                /* enable= */ false));
            triggerWait(TEST_UNSUBSCRIBE_FROM_SERVICE_READY_EVENT);
            break;
          }
        }
        break;
      }
      default: {
        break;
      }
    }
  }

  EndpointId mEndpointId = ENDPOINT_ID_INVALID;
};

TEST_F(ChreMessageHubTest, NanoappSubscribesToEndpointReadyEvent) {
  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<EndpointRegistrationTestApp>(

      TestNanoappInfo{.name = "TEST_ENDPOINT_READY_EVENT", .id = 0x1234}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);
  TestNanoapp *testNanoapp = queryNanoapp(appId);
  ASSERT_NE(testNanoapp, nullptr);

  // Create the other hub
  pw::IntrusivePtr<MessageHubCallbackStoreData> callback =
      pw::MakeRefCounted<MessageHubCallbackStoreData>(/* message= */ nullptr,
                                                      /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback->setMessageHub(&(*messageHub));

  // Test subscribing to the ready event
  sendEventToNanoappAndWait(appId, TEST_SUBSCRIBE_TO_READY_EVENT,
                            TEST_SUBSCRIBE_TO_READY_EVENT);

  // Register the endpoint and wait for the ready event
  EXPECT_TRUE(messageHub->registerEndpoint(kDynamicEndpointInfo.id));
  testNanoapp->wait(CHRE_EVENT_MSG_ENDPOINT_READY);

  // Unsubscribe from the ready event
  sendEventToNanoappAndWait(appId, TEST_UNSUBSCRIBE_FROM_READY_EVENT,
                            TEST_UNSUBSCRIBE_FROM_READY_EVENT);
}

TEST_F(ChreMessageHubTest, NanoappSubscribesToEndpointReadyEventAlreadyExists) {
  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<EndpointRegistrationTestApp>(

      TestNanoappInfo{.name = "TEST_ENDPOINT_READY_EVENT", .id = 0x1234}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);

  // Create the other hub
  pw::IntrusivePtr<MessageHubCallbackStoreData> callback =
      pw::MakeRefCounted<MessageHubCallbackStoreData>(/* message= */ nullptr,
                                                      /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback->setMessageHub(&(*messageHub));

  // Test subscribing to the ready event - endpoint should already exist
  sendEventToNanoappAndWait(appId, TEST_SUBSCRIBE_TO_READY_EVENT_ALREADY_EXISTS,
                            CHRE_EVENT_MSG_ENDPOINT_READY);

  // Unsubscribe from the ready event
  sendEventToNanoappAndWait(appId, TEST_UNSUBSCRIBE_FROM_READY_EVENT,
                            TEST_UNSUBSCRIBE_FROM_READY_EVENT);
}

TEST_F(ChreMessageHubTest, NanoappSubscribesToServiceReadyEvent) {
  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<EndpointRegistrationTestApp>(
      TestNanoappInfo{.name = "TEST_SERVICE_READY_EVENT", .id = 0x1234}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);
  TestNanoapp *testNanoapp = queryNanoapp(appId);
  ASSERT_NE(testNanoapp, nullptr);

  // Create the other hub
  pw::IntrusivePtr<MessageHubCallbackStoreData> callback =
      pw::MakeRefCounted<MessageHubCallbackStoreData>(/* message= */ nullptr,
                                                      /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback->setMessageHub(&(*messageHub));

  // Test subscribing to the service ready event
  sendEventToNanoappAndWait(appId, TEST_SUBSCRIBE_TO_SERVICE_READY_EVENT,
                            TEST_SUBSCRIBE_TO_SERVICE_READY_EVENT);

  // Register the endpoint and wait for the service ready event
  EXPECT_TRUE(messageHub->registerEndpoint(kDynamicEndpointInfo.id));
  testNanoapp->wait(CHRE_EVENT_MSG_SERVICE_READY);

  // Unsubscribe from the service ready event
  sendEventToNanoappAndWait(appId, TEST_UNSUBSCRIBE_FROM_SERVICE_READY_EVENT,
                            TEST_UNSUBSCRIBE_FROM_SERVICE_READY_EVENT);
}

TEST_F(ChreMessageHubTest, NanoappLoadAndUnloadAreRegisteredAndUnregistered) {
  // Create the other hub
  pw::IntrusivePtr<MessageHubCallbackStoreData> callback =
      pw::MakeRefCounted<MessageHubCallbackStoreData>(/* message= */ nullptr,
                                                      /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback->setMessageHub(&(*messageHub));

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<EndpointRegistrationTestApp>(
      TestNanoappInfo{.name = "TEST_NANOAPP_REGISTRATION", .id = 0x1234}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);

  // The nanoapp should be registered as an endpoint
  EXPECT_TRUE(
      callback->hasEndpointBeenRegistered(EventLoopManagerSingleton::get()
                                              ->getChreMessageHubManager()
                                              .kChreMessageHubId,
                                          appId));

  // Unload the nanoapp
  unloadNanoapp(appId);

  // The nanoapp should be unregistered as an endpoint
  EXPECT_FALSE(
      callback->hasEndpointBeenRegistered(EventLoopManagerSingleton::get()
                                              ->getChreMessageHubManager()
                                              .kChreMessageHubId,
                                          appId));
}

}  // namespace
}  // namespace chre::message
