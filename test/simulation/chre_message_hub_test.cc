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

#include "gtest/gtest.h"
#include "inc/test_util.h"
#include "test_base.h"
#include "test_event.h"
#include "test_util.h"

#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>

namespace chre::message {
namespace {

CREATE_CHRE_TEST_EVENT(TRIGGER_COND_VAR, 0);
CREATE_CHRE_TEST_EVENT(TEST_GET_EVENT_INFO, 1);
CREATE_CHRE_TEST_EVENT(TEST_OPEN_SESSION, 2);
CREATE_CHRE_TEST_EVENT(TEST_OPEN_DEFAULT_SESSION, 3);
CREATE_CHRE_TEST_EVENT(TEST_CLOSE_SESSION, 4);
CREATE_CHRE_TEST_EVENT(TEST_GET_SESSION_INFO_INVALID_SESSION, 5);
CREATE_CHRE_TEST_EVENT(TEST_SEND_MESSAGE, 6);
CREATE_CHRE_TEST_EVENT(TEST_SEND_MESSAGE_NO_FREE_CALLBACK, 7);
CREATE_CHRE_TEST_EVENT(TEST_PUBLISH_SERVICE, 8);
CREATE_CHRE_TEST_EVENT(TEST_BAD_LEGACY_SERVICE_NAME, 9);
CREATE_CHRE_TEST_EVENT(TEST_OPEN_SESSION_WITH_SERVICE, 10);

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
const char kServiceDescriptorForEndpoint2[] = "TEST_SERVICE.TEST";
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

  void onSessionOpened(const Session & /* session */) override {}

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
  EndpointInfoTestApp(std::mutex &mutex, std::condition_variable &condVar,
                      const TestNanoappInfo &info)
      : TestNanoapp(info), mMutex(mutex), mCondVar(condVar) {}

  void handleEvent(uint32_t, uint16_t eventType,
                   const void *eventData) override {
    switch (eventType) {
      case CHRE_EVENT_TEST_EVENT: {
        auto event = static_cast<const TestEvent *>(eventData);
        switch (event->type) {
          case TEST_GET_EVENT_INFO: {
            {
              std::unique_lock<std::mutex> lock(mMutex);

              for (size_t i = 0; i < kNumEndpoints; ++i) {
                chreMsgEndpointInfo info = {};
                EXPECT_TRUE(chreMsgGetEndpointInfo(
                    kOtherMessageHubId, kEndpointInfos[i].id, &info));

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
            }
            mCondVar.notify_one();
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

  std::mutex &mMutex;
  std::condition_variable &mCondVar;
};

TEST_F(ChreMessageHubTest, NanoappGetsEndpointInfo) {
  std::mutex mutex;
  std::condition_variable condVar;

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<EndpointInfoTestApp>(
      mutex, condVar,
      TestNanoappInfo{.name = "TEST_GET_ENDPOINT_INFO", .id = 0x1234}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);

  // Create the other hub
  MessageHubCallbackStoreData callback(/* message= */ nullptr,
                                       /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback.setMessageHub(&(*messageHub));

  // Test getting endpoint info
  std::unique_lock<std::mutex> lock(mutex);
  sendEventToNanoapp(appId, TEST_GET_EVENT_INFO);
  condVar.wait(lock);
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
  MessageTestApp(std::mutex &mutex, std::condition_variable &condVar,
                 bool &messageReceivedAndValidated, bool &sessionClosed,
                 const TestNanoappInfo &info)
      : TestNanoapp(info),
        mMutex(mutex),
        mCondVar(condVar),
        mMessageReceivedAndValidated(messageReceivedAndValidated),
        mSessionClosed(sessionClosed) {}

  void handleEvent(uint32_t, uint16_t eventType,
                   const void *eventData) override {
    switch (eventType) {
      case CHRE_EVENT_MSG_FROM_ENDPOINT: {
        {
          std::unique_lock<std::mutex> lock(mMutex);
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
        }
        mCondVar.notify_one();
        break;
      }
      case CHRE_EVENT_MSG_SESSION_CLOSED: {
        {
          std::unique_lock<std::mutex> lock(mMutex);
          auto *session =
              static_cast<const struct chreMsgSessionInfo *>(eventData);
          EXPECT_EQ(session->hubId, kOtherMessageHubId);
          EXPECT_EQ(session->endpointId, kEndpointInfos[0].id);
          mSessionClosed = true;
        }
        mCondVar.notify_one();
        break;
      }
      case CHRE_EVENT_TEST_EVENT: {
        auto event = static_cast<const TestEvent *>(eventData);
        switch (event->type) {
          case TRIGGER_COND_VAR: {
            {
              std::unique_lock<std::mutex> lock(mMutex);
            }
            mCondVar.notify_one();
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

  std::mutex &mMutex;
  std::condition_variable &mCondVar;
  bool &mMessageReceivedAndValidated;
  bool &mSessionClosed;
};

TEST_F(ChreMessageHubTest, SendMessageToNanoapp) {
  constexpr uint64_t kNanoappId = 0x1234;
  std::mutex mutex;
  std::condition_variable condVar;
  bool messageReceivedAndValidated = false;
  bool sessionClosed = false;

  // Create the message
  pw::allocator::LibCAllocator allocator = pw::allocator::GetLibCAllocator();
  pw::UniquePtr<std::byte[]> messageData =
      createMessageData(allocator, kMessageSize);

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<MessageTestApp>(
      mutex, condVar, messageReceivedAndValidated, sessionClosed,
      TestNanoappInfo{.name = "TEST1", .id = kNanoappId}));

  // Create the other hub
  MessageHubCallbackStoreData callback(/* message= */ nullptr,
                                       /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback.setMessageHub(&(*messageHub));

  // Open the session from the other hub:1 to the nanoapp
  SessionId sessionId = messageHub->openSession(kEndpointInfos[0].id,
                                                EventLoopManagerSingleton::get()
                                                    ->getChreMessageHubManager()
                                                    .kChreMessageHubId,
                                                kNanoappId);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);

  // Send the message to the nanoapp
  std::unique_lock<std::mutex> lock(mutex);
  ASSERT_TRUE(messageHub->sendMessage(std::move(messageData),
                                      /* messageType= */ 1,
                                      /* messagePermissions= */ 0, sessionId));
  condVar.wait(lock);
  EXPECT_TRUE(messageReceivedAndValidated);

  // Close the session
  EXPECT_TRUE(messageHub->closeSession(sessionId));
  condVar.wait(lock);
  EXPECT_TRUE(sessionClosed);
}

//! Nanoapp used to test sending messages from a generic endpoint to a nanoapp
//! with a different permissions set
class MessagePermissionTestApp : public MessageTestApp {
 public:
  MessagePermissionTestApp(std::mutex &mutex, std::condition_variable &condVar,
                           bool &messageReceivedAndValidated,
                           bool &sessionClosed, const TestNanoappInfo &info)
      : MessageTestApp(mutex, condVar, messageReceivedAndValidated,
                       sessionClosed, info) {}
};

TEST_F(ChreMessageHubTest, SendMessageToNanoappPermissionFailure) {
  constexpr uint64_t kNanoappId = 0x1234;
  std::mutex mutex;
  std::condition_variable condVar;
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
      mutex, condVar, messageReceivedAndValidated, sessionClosed,
      TestNanoappInfo{
          .name = "TEST1", .id = kNanoappId, .perms = CHRE_PERMS_BLE}));

  // Create the other hub
  MessageHubCallbackStoreData callback(/* message= */ nullptr,
                                       /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback.setMessageHub(&(*messageHub));

  // Open the session from the other hub:1 to the nanoapp
  SessionId sessionId = messageHub->openSession(kEndpointInfos[0].id,
                                                EventLoopManagerSingleton::get()
                                                    ->getChreMessageHubManager()
                                                    .kChreMessageHubId,
                                                kNanoappId);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);

  // Send the message to the nanoapp
  std::unique_lock<std::mutex> lock(mutex);
  ASSERT_TRUE(messageHub->sendMessage(
      std::move(messageData),
      /* messageType= */ 1,
      /* messagePermissions= */ CHRE_PERMS_AUDIO | CHRE_PERMS_GNSS, sessionId));

  // Send the trigger cond var event, which will be handled after the
  // CHRE message from endpoint event (if it is sent erroneously). If the
  // message event is not sent, this event will unlock the condition variable.
  // If the message event is sent, the condition variable will be unlocked
  // after the message event is processed, setting the
  // messageReceivedAndValidated variable to true, which will fail the test.
  sendEventToNanoapp(appId, TRIGGER_COND_VAR);
  condVar.wait(lock);
  EXPECT_FALSE(messageReceivedAndValidated);
  EXPECT_TRUE(sessionClosed);
}

//! Nanoapp used to test opening sessions and sending messages from a nanoapp
//! to a generic endpoint
class SessionAndMessageTestApp : public TestNanoapp {
 public:
  SessionAndMessageTestApp(std::mutex &mutex, std::condition_variable &condVar,
                           SessionId &sessionId, const TestNanoappInfo &info)
      : TestNanoapp(info),
        mMutex(mutex),
        mCondVar(condVar),
        mSessionId(sessionId) {}

  void handleEvent(uint32_t, uint16_t eventType,
                   const void *eventData) override {
    switch (eventType) {
      case CHRE_EVENT_MSG_SESSION_OPENED: {
        {
          std::unique_lock<std::mutex> lock(mMutex);

          // Verify the session info from the event is correct
          auto sessionInfo = static_cast<const chreMsgSessionInfo *>(eventData);
          EXPECT_EQ(sessionInfo->hubId, kOtherMessageHubId);
          EXPECT_EQ(sessionInfo->endpointId, mToEndpointId);
          EXPECT_STREQ(sessionInfo->serviceDescriptor, "");
          EXPECT_NE(sessionInfo->sessionId, UINT16_MAX);
          EXPECT_EQ(
              sessionInfo->reason,
              chreMsgEndpointReason::CHRE_MSG_ENDPOINT_REASON_UNSPECIFIED);
          mSessionId = sessionInfo->sessionId;

          // Get the session info and verify it is correct
          struct chreMsgSessionInfo sessionInfo2;
          EXPECT_TRUE(chreMsgSessionGetInfo(mSessionId, &sessionInfo2));
          EXPECT_EQ(sessionInfo2.hubId, kOtherMessageHubId);
          EXPECT_EQ(sessionInfo2.endpointId, mToEndpointId);
          EXPECT_STREQ(sessionInfo2.serviceDescriptor, "");
          EXPECT_EQ(sessionInfo2.sessionId, mSessionId);
          EXPECT_EQ(
              sessionInfo2.reason,
              chreMsgEndpointReason::CHRE_MSG_ENDPOINT_REASON_UNSPECIFIED);
        }
        mCondVar.notify_one();
        break;
      }
      case CHRE_EVENT_MSG_SESSION_CLOSED: {
        {
          std::unique_lock<std::mutex> lock(mMutex);

          // Verify the session info from the event is correct
          auto sessionInfo = static_cast<const chreMsgSessionInfo *>(eventData);
          EXPECT_EQ(sessionInfo->hubId, kOtherMessageHubId);
          EXPECT_EQ(sessionInfo->endpointId, mToEndpointId);
          EXPECT_STREQ(sessionInfo->serviceDescriptor, "");
          EXPECT_EQ(sessionInfo->sessionId, mSessionId);
        }
        mCondVar.notify_one();
        break;
      }
      case CHRE_EVENT_MSG_FROM_ENDPOINT: {
        {
          std::unique_lock<std::mutex> lock(mMutex);

          auto messageData =
              static_cast<const chreMsgMessageFromEndpointData *>(eventData);
          EXPECT_EQ(messageData->messageType, 1);
          EXPECT_EQ(messageData->messagePermissions,
                    CHRE_MESSAGE_PERMISSION_NONE);
          EXPECT_EQ(messageData->messageSize, kMessageSize);

          auto message =
              reinterpret_cast<const uint8_t *>(messageData->message);
          for (size_t i = 0; i < kMessageSize; ++i) {
            EXPECT_EQ(message[i], i + 1);
          }
          EXPECT_EQ(messageData->sessionId, mSessionId);
        }
        mCondVar.notify_one();
        break;
      }
      case CHRE_EVENT_TEST_EVENT: {
        auto event = static_cast<const TestEvent *>(eventData);
        switch (event->type) {
          case TEST_OPEN_SESSION: {
            {
              std::unique_lock<std::mutex> lock(mMutex);

              // Open the session from the nanoapp to the other hub:0
              mToEndpointId = kEndpointInfos[0].id;
              EXPECT_TRUE(
                  chreMsgSessionOpenAsync(kOtherMessageHubId, mToEndpointId,
                                          /* serviceDescriptor= */ nullptr));
              mSessionId = UINT16_MAX;
            }
            break;
          }
          case TEST_OPEN_DEFAULT_SESSION: {
            {
              std::unique_lock<std::mutex> lock(mMutex);

              // Open the default session from the nanoapp to the other hub:1
              mToEndpointId = kEndpointInfos[1].id;
              EXPECT_TRUE(
                  chreMsgSessionOpenAsync(CHRE_MSG_HUB_ID_ANY, mToEndpointId,
                                          /* serviceDescriptor= */ nullptr));
              mSessionId = UINT16_MAX;
            }
            break;
          }
          case TEST_CLOSE_SESSION: {
            {
              std::unique_lock<std::mutex> lock(mMutex);

              // Close the session
              EXPECT_TRUE(chreMsgSessionCloseAsync(mSessionId));
            }
            break;
          }
          case TEST_GET_SESSION_INFO_INVALID_SESSION: {
            {
              std::unique_lock<std::mutex> lock(mMutex);

              struct chreMsgSessionInfo sessionInfo;
              EXPECT_NE(mSessionId, SESSION_ID_INVALID);
              EXPECT_FALSE(chreMsgSessionGetInfo(mSessionId, &sessionInfo));
            }
            mCondVar.notify_one();
            break;
          }
          case TEST_SEND_MESSAGE: {
            {
              std::unique_lock<std::mutex> lock(mMutex);
              EXPECT_TRUE(chreMsgSend(reinterpret_cast<void *>(kMessage),
                                      kMessageSize,
                                      /* messageType= */ 1, mSessionId,
                                      CHRE_MESSAGE_PERMISSION_NONE,
                                      [](void *message, size_t length) {
                                        EXPECT_EQ(message, kMessage);
                                        EXPECT_EQ(length, kMessageSize);
                                      }));
            }
            mCondVar.notify_one();
            break;
          }
          case TEST_SEND_MESSAGE_NO_FREE_CALLBACK: {
            {
              std::unique_lock<std::mutex> lock(mMutex);
              EXPECT_TRUE(chreMsgSend(reinterpret_cast<void *>(kMessage),
                                      kMessageSize,
                                      /* messageType= */ 1, mSessionId,
                                      CHRE_MESSAGE_PERMISSION_NONE,
                                      /* freeCallback= */ nullptr));
            }
            mCondVar.notify_one();
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

  std::mutex &mMutex;
  std::condition_variable &mCondVar;
  SessionId &mSessionId;
  EndpointId mToEndpointId = ENDPOINT_ID_INVALID;
};
uint8_t SessionAndMessageTestApp::kMessage[kMessageSize] = {1, 2, 3, 4, 5};

TEST_F(ChreMessageHubTest, NanoappOpensSessionWithGenericEndpoint) {
  std::mutex mutex;
  std::condition_variable condVar;
  SessionId sessionId = SESSION_ID_INVALID;

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<SessionAndMessageTestApp>(
      mutex, condVar, sessionId,
      TestNanoappInfo{.name = "TEST_OPEN_SESSION", .id = 0x1234}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);

  // Create the other hub
  MessageHubCallbackStoreData callback(/* message= */ nullptr,
                                       /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback.setMessageHub(&(*messageHub));

  // Test opening session
  std::unique_lock<std::mutex> lock(mutex);
  sendEventToNanoapp(appId, TEST_OPEN_SESSION);
  condVar.wait(lock);

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

  // Explicitly clear the message hub and wait for the session to be closed
  messageHub.reset();
  condVar.wait(lock);
}

TEST_F(ChreMessageHubTest, NanoappOpensDefaultSessionWithGenericEndpoint) {
  std::mutex mutex;
  std::condition_variable condVar;
  SessionId sessionId = SESSION_ID_INVALID;

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<SessionAndMessageTestApp>(
      mutex, condVar, sessionId,
      TestNanoappInfo{.name = "TEST_OPEN_DEFAULT_SESSION", .id = 0x1234}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);

  // Create the other hub
  MessageHubCallbackStoreData callback(/* message= */ nullptr,
                                       /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback.setMessageHub(&(*messageHub));

  // Test opening the default session
  std::unique_lock<std::mutex> lock(mutex);
  sendEventToNanoapp(appId, TEST_OPEN_DEFAULT_SESSION);
  condVar.wait(lock);

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

  // Explicitly clear the message hub and wait for the session to be closed
  messageHub.reset();
  condVar.wait(lock);
}

TEST_F(ChreMessageHubTest, NanoappClosesSessionWithGenericEndpoint) {
  std::mutex mutex;
  std::condition_variable condVar;
  Session session;
  SessionId sessionId = SESSION_ID_INVALID;

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<SessionAndMessageTestApp>(
      mutex, condVar, sessionId,
      TestNanoappInfo{.name = "TEST_OPEN_SESSION", .id = 0x1234}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);

  // Create the other hub
  MessageHubCallbackStoreData callback(/* message= */ nullptr, &session);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback.setMessageHub(&(*messageHub));

  // Test opening session
  std::unique_lock<std::mutex> lock(mutex);
  sendEventToNanoapp(appId, TEST_OPEN_SESSION);
  condVar.wait(lock);

  // Now close the session
  sendEventToNanoapp(appId, TEST_CLOSE_SESSION);
  condVar.wait(lock);

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
  std::mutex mutex;
  std::condition_variable condVar;
  Session session;
  SessionId sessionId = SESSION_ID_INVALID;

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<SessionAndMessageTestApp>(
      mutex, condVar, sessionId,
      TestNanoappInfo{.name = "TEST_OPEN_SESSION", .id = 0x1234}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);

  // Create the other hub
  MessageHubCallbackStoreData callback(/* message= */ nullptr, &session);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback.setMessageHub(&(*messageHub));

  // Test opening session
  std::unique_lock<std::mutex> lock(mutex);
  sendEventToNanoapp(appId, TEST_OPEN_SESSION);
  condVar.wait(lock);

  // Now close the session and wait for the event to be processed
  EXPECT_TRUE(messageHub->closeSession(sessionId));
  condVar.wait(lock);

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
  std::mutex mutex;
  std::condition_variable condVar;
  Session session;
  SessionId sessionId = SESSION_ID_INVALID;

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<SessionAndMessageTestApp>(
      mutex, condVar, sessionId,
      TestNanoappInfo{.name = "TEST_OPEN_SESSION", .id = 0x1234}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);

  // Create the other hubs
  MessageHubCallbackStoreData callback(/* message= */ nullptr, &session);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback.setMessageHub(&(*messageHub));

  MessageHubCallbackStoreData callback2(/* message= */ nullptr, &session);
  std::optional<MessageRouter::MessageHub> messageHub2 =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB2", kOtherMessageHubId + 1, callback2);
  ASSERT_TRUE(messageHub2.has_value());
  callback2.setMessageHub(&(*messageHub2));

  // Open a session not involving the nanoapps
  sessionId = messageHub->openSession(
      kEndpointInfos[0].id, kOtherMessageHubId + 1, kEndpointInfos[1].id);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);

  // Tell the nanoapp to get the session info for our session
  std::unique_lock<std::mutex> lock(mutex);
  sendEventToNanoapp(appId, TEST_GET_SESSION_INFO_INVALID_SESSION);
  condVar.wait(lock);
}

TEST_F(ChreMessageHubTest, NanoappSendsMessageToGenericEndpoint) {
  std::mutex mutex;
  std::condition_variable condVar;
  SessionId sessionId = SESSION_ID_INVALID;
  Message message;

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<SessionAndMessageTestApp>(
      mutex, condVar, sessionId,
      TestNanoappInfo{.name = "TEST_OPEN_SESSION", .id = 0x1234}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);

  // Create the other hub
  MessageHubCallbackStoreData callback(&message,
                                       /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback.setMessageHub(&(*messageHub));

  // Test opening session
  std::unique_lock<std::mutex> lock(mutex);
  sendEventToNanoapp(appId, TEST_OPEN_SESSION);
  condVar.wait(lock);

  // Send the message to the other hub and verify it was received
  sendEventToNanoapp(appId, TEST_SEND_MESSAGE);
  condVar.wait(lock);

  EXPECT_EQ(message.data.size(), kMessageSize);
  for (size_t i = 0; i < kMessageSize; ++i) {
    EXPECT_EQ(message.data[i],
              static_cast<std::byte>(SessionAndMessageTestApp::kMessage[i]));
  }
  EXPECT_EQ(message.messageType, 1);
  EXPECT_EQ(message.messagePermissions, CHRE_MESSAGE_PERMISSION_NONE);

  // Explicitly clear the message hub and wait for the session to be closed
  messageHub.reset();
  condVar.wait(lock);
}

TEST_F(ChreMessageHubTest,
       NanoappSendsMessageWithNoFreeCallbackToGenericEndpoint) {
  std::mutex mutex;
  std::condition_variable condVar;
  SessionId sessionId = SESSION_ID_INVALID;
  Message message;

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<SessionAndMessageTestApp>(
      mutex, condVar, sessionId,
      TestNanoappInfo{.name = "TEST_OPEN_SESSION", .id = 0x1234}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);

  // Create the other hub
  MessageHubCallbackStoreData callback(&message,
                                       /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback.setMessageHub(&(*messageHub));

  // Test opening session
  std::unique_lock<std::mutex> lock(mutex);
  sendEventToNanoapp(appId, TEST_OPEN_SESSION);
  condVar.wait(lock);

  // Send the message to the other hub and verify it was received
  sendEventToNanoapp(appId, TEST_SEND_MESSAGE_NO_FREE_CALLBACK);
  condVar.wait(lock);

  EXPECT_EQ(message.data.size(), kMessageSize);
  for (size_t i = 0; i < kMessageSize; ++i) {
    EXPECT_EQ(message.data[i],
              static_cast<std::byte>(SessionAndMessageTestApp::kMessage[i]));
  }
  EXPECT_EQ(message.messageType, 1);
  EXPECT_EQ(message.messagePermissions, CHRE_MESSAGE_PERMISSION_NONE);

  // Explicitly clear the message hub and wait for the session to be closed
  messageHub.reset();
  condVar.wait(lock);
}

TEST_F(ChreMessageHubTest, NanoappGetsMessageFromGenericEndpoint) {
  std::mutex mutex;
  std::condition_variable condVar;
  SessionId sessionId = SESSION_ID_INVALID;
  Message message;

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<SessionAndMessageTestApp>(
      mutex, condVar, sessionId,
      TestNanoappInfo{.name = "TEST_OPEN_SESSION", .id = 0x1234}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);

  // Create the other hub
  MessageHubCallbackStoreData callback(&message,
                                       /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback.setMessageHub(&(*messageHub));

  // Test opening session
  std::unique_lock<std::mutex> lock(mutex);
  sendEventToNanoapp(appId, TEST_OPEN_SESSION);
  condVar.wait(lock);

  // Send the message to the nanoapp and verify it was received
  pw::allocator::LibCAllocator allocator = pw::allocator::GetLibCAllocator();
  pw::UniquePtr<std::byte[]> messageData =
      createMessageData(allocator, kMessageSize);
  EXPECT_TRUE(messageHub->sendMessage(std::move(messageData),
                                      /* messageType= */ 1,
                                      CHRE_MESSAGE_PERMISSION_NONE, sessionId));
  condVar.wait(lock);

  // Explicitly clear the message hub and wait for the session to be closed
  messageHub.reset();
  condVar.wait(lock);
}

//! Nanoapp used to test opening sessions with services
class ServiceSessionTestApp : public TestNanoapp {
 public:
  ServiceSessionTestApp(std::mutex &mutex, std::condition_variable &condVar,
                        const TestNanoappInfo &info)
      : TestNanoapp(info), mMutex(mutex), mCondVar(condVar) {}

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
        {
          std::unique_lock<std::mutex> lock(mMutex);

          // Verify the session info from the event is correct
          auto sessionInfo = static_cast<const chreMsgSessionInfo *>(eventData);
          EXPECT_EQ(sessionInfo->hubId, kOtherMessageHubId);
          EXPECT_EQ(
              sessionInfo->reason,
              chreMsgEndpointReason::CHRE_MSG_ENDPOINT_REASON_UNSPECIFIED);

          if (std::strcmp(sessionInfo->serviceDescriptor,
                          kServiceDescriptorForEndpoint2) == 0) {
            EXPECT_EQ(sessionInfo->endpointId, kEndpointInfos[1].id);
            EXPECT_NE(sessionInfo->sessionId, UINT16_MAX);
          }
        }
        mCondVar.notify_one();
        break;
      }
      case CHRE_EVENT_MSG_SESSION_CLOSED: {
        {
          std::unique_lock<std::mutex> lock(mMutex);
        }
        mCondVar.notify_one();
        break;
      }
      case CHRE_EVENT_TEST_EVENT: {
        auto event = static_cast<const TestEvent *>(eventData);
        switch (event->type) {
          case TEST_PUBLISH_SERVICE: {
            {
              std::unique_lock<std::mutex> lock(mMutex);
              chreMsgServiceInfo serviceInfo;
              serviceInfo.majorVersion = 1;
              serviceInfo.minorVersion = 0;
              serviceInfo.serviceDescriptor = kServiceDescriptorForNanoapp;
              serviceInfo.serviceFormat =
                  CHRE_MSG_ENDPOINT_SERVICE_FORMAT_CUSTOM;
              EXPECT_TRUE(
                  chreMsgPublishServices(&serviceInfo, /* numServices= */ 1));
            }
            mCondVar.notify_one();
            break;
          }
          case TEST_BAD_LEGACY_SERVICE_NAME: {
            {
              std::unique_lock<std::mutex> lock(mMutex);
              chreMsgServiceInfo serviceInfo;
              serviceInfo.majorVersion = 1;
              serviceInfo.minorVersion = 0;
              serviceInfo.serviceDescriptor = kBadLegacyServiceName;
              serviceInfo.serviceFormat =
                  CHRE_MSG_ENDPOINT_SERVICE_FORMAT_CUSTOM;
              EXPECT_FALSE(
                  chreMsgPublishServices(&serviceInfo, /* numServices= */ 1));
            }
            mCondVar.notify_one();
            break;
          }
          case TEST_OPEN_SESSION_WITH_SERVICE: {
            {
              std::unique_lock<std::mutex> lock(mMutex);
              EXPECT_TRUE(chreMsgSessionOpenAsync(
                  kOtherMessageHubId, kEndpointInfos[1].id,
                  kServiceDescriptorForEndpoint2));
            }
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

  std::mutex &mMutex;
  std::condition_variable &mCondVar;
};

TEST_F(ChreMessageHubTest, OpenSessionWithNanoappService) {
  constexpr uint64_t kNanoappId = 0x1234;
  std::mutex mutex;
  std::condition_variable condVar;

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<ServiceSessionTestApp>(
      mutex, condVar,
      TestNanoappInfo{.name = "TEST_OPEN_SESSION_WITH_SERVICE",
                      .id = kNanoappId}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);

  // Create the other hub
  MessageHubCallbackStoreData callback(/* message= */ nullptr,
                                       /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback.setMessageHub(&(*messageHub));

  // Nanoapp publishes the service
  std::unique_lock<std::mutex> lock(mutex);
  sendEventToNanoapp(appId, TEST_PUBLISH_SERVICE);
  condVar.wait(lock);

  // Open the session from the other hub:1 to the nanoapp with the service
  SessionId sessionId =
      messageHub->openSession(kEndpointInfos[0].id,
                              EventLoopManagerSingleton::get()
                                  ->getChreMessageHubManager()
                                  .kChreMessageHubId,
                              kNanoappId, kServiceDescriptorForNanoapp);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);

  // Explicitly clear the message hub and wait for the session to be closed
  messageHub.reset();
  condVar.wait(lock);
}

TEST_F(ChreMessageHubTest, OpenTwoSessionsWithNanoappServiceAndNoService) {
  constexpr uint64_t kNanoappId = 0x1234;
  std::mutex mutex;
  std::condition_variable condVar;

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<ServiceSessionTestApp>(
      mutex, condVar,
      TestNanoappInfo{.name = "TEST_OPEN_SESSION_WITH_SERVICE",
                      .id = kNanoappId}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);

  // Create the other hub
  MessageHubCallbackStoreData callback(/* message= */ nullptr,
                                       /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback.setMessageHub(&(*messageHub));

  // Nanoapp publishes the service
  std::unique_lock<std::mutex> lock(mutex);
  sendEventToNanoapp(appId, TEST_PUBLISH_SERVICE);
  condVar.wait(lock);

  // Open the session from the other hub:1 to the nanoapp with the service
  SessionId sessionId =
      messageHub->openSession(kEndpointInfos[0].id,
                              EventLoopManagerSingleton::get()
                                  ->getChreMessageHubManager()
                                  .kChreMessageHubId,
                              kNanoappId, kServiceDescriptorForNanoapp);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);

  // Open the other session from the other hub:1 to the nanoapp
  SessionId sessionId2 =
      messageHub->openSession(kEndpointInfos[0].id,
                              EventLoopManagerSingleton::get()
                                  ->getChreMessageHubManager()
                                  .kChreMessageHubId,
                              kNanoappId);
  EXPECT_NE(sessionId2, SESSION_ID_INVALID);
  EXPECT_NE(sessionId, sessionId2);

  // Explicitly clear the message hub and wait for the session to be closed
  messageHub.reset();
  condVar.wait(lock);
}

TEST_F(ChreMessageHubTest, OpenSessionWithNanoappLegacyService) {
  std::mutex mutex;
  std::condition_variable condVar;

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<ServiceSessionTestApp>(
      mutex, condVar,
      TestNanoappInfo{.name = "TEST_OPEN_SESSION_WITH_LEGACY_SERVICE",
                      .id = kLegacyServiceNanoappId}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);

  // Create the other hub
  MessageHubCallbackStoreData callback(/* message= */ nullptr,
                                       /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback.setMessageHub(&(*messageHub));

  // Open the session from the other hub:1 to the nanoapp with the service
  SessionId sessionId =
      messageHub->openSession(kEndpointInfos[0].id,
                              EventLoopManagerSingleton::get()
                                  ->getChreMessageHubManager()
                                  .kChreMessageHubId,
                              kLegacyServiceNanoappId, kLegacyServiceName);
  EXPECT_NE(sessionId, SESSION_ID_INVALID);

  // Explicitly clear the message hub and wait for the session to be closed
  std::unique_lock<std::mutex> lock(mutex);
  messageHub.reset();
  condVar.wait(lock);
}

TEST_F(ChreMessageHubTest, NanoappFailsToPublishLegacyServiceInNewWay) {
  constexpr uint64_t kNanoappId = 0x1234;
  std::mutex mutex;
  std::condition_variable condVar;

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<ServiceSessionTestApp>(
      mutex, condVar,
      TestNanoappInfo{.name = "TEST_BAD_LEGACY_SERVICE_NAME",
                      .id = kNanoappId}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);

  // Create the other hub
  MessageHubCallbackStoreData callback(/* message= */ nullptr,
                                       /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback.setMessageHub(&(*messageHub));

  // Nanoapp publishes the service
  std::unique_lock<std::mutex> lock(mutex);
  sendEventToNanoapp(appId, TEST_BAD_LEGACY_SERVICE_NAME);
  condVar.wait(lock);
}

TEST_F(ChreMessageHubTest, NanoappOpensSessionWithService) {
  constexpr uint64_t kNanoappId = 0x1234;
  std::mutex mutex;
  std::condition_variable condVar;

  // Load the nanoapp
  uint64_t appId = loadNanoapp(MakeUnique<ServiceSessionTestApp>(
      mutex, condVar,
      TestNanoappInfo{.name = "TEST_OPEN_SESSION_WITH_SERVICE",
                      .id = kNanoappId}));
  Nanoapp *nanoapp = getNanoappByAppId(appId);
  ASSERT_NE(nanoapp, nullptr);

  // Create the other hub
  MessageHubCallbackStoreData callback(/* message= */ nullptr,
                                       /* session= */ nullptr);
  std::optional<MessageRouter::MessageHub> messageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "OTHER_TEST_HUB", kOtherMessageHubId, callback);
  ASSERT_TRUE(messageHub.has_value());
  callback.setMessageHub(&(*messageHub));

  // Nanoapp publishes the service
  std::unique_lock<std::mutex> lock(mutex);
  sendEventToNanoapp(appId, TEST_OPEN_SESSION_WITH_SERVICE);
  condVar.wait(lock);
}

}  // namespace
}  // namespace chre::message
