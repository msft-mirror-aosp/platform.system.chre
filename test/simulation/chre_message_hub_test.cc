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

#include <cstdint>
#include <cstring>
#include <optional>

#include "chre/core/event_loop_manager.h"
#include "chre/util/dynamic_vector.h"
#include "chre/util/system/message_common.h"
#include "chre/util/system/message_router.h"
#include "chre/util/system/napp_permissions.h"
#include "chre_api/chre/event.h"

#include "pw_allocator/allocator.h"
#include "pw_allocator/libc_allocator.h"
#include "pw_allocator/unique_ptr.h"
#include "pw_function/function.h"

#include "gtest/gtest.h"
#include "inc/test_util.h"
#include "test_base.h"
#include "test_util.h"

namespace chre::message {
namespace {

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

  void onSessionClosed(const Session &session) override {
    if (mSession != nullptr) {
      *mSession = session;
    }
  }

 private:
  Message *mMessage;
  Session *mSession;
};

class ChreMessageHubTest : public TestBase {};

TEST_F(ChreMessageHubTest, MessageRouterNanoappsAreEndpointsToChreMessageHub) {
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

TEST_F(ChreMessageHubTest,
       MessageRouterMultipleNanoappsAreEndpointsToChreMessageHub) {
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

TEST_F(ChreMessageHubTest, MessageRouterSendMessageToNanoapp) {
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

  // Open the session from the other hub:1 to the nanoapp
  SessionId sessionId =
      messageHub->openSession(kEndpointInfos[0].id,
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

class MessagePermissionTestApp : public MessageTestApp {
 public:
  MessagePermissionTestApp(std::mutex &mutex, std::condition_variable &condVar,
                           bool &messageReceivedAndValidated,
                           bool &sessionClosed, const TestNanoappInfo &info)
      : MessageTestApp(mutex, condVar, messageReceivedAndValidated,
                       sessionClosed, info) {}
};

TEST_F(ChreMessageHubTest, MessageRouterSendMessageToNanoappPermissionFailure) {
  CREATE_CHRE_TEST_EVENT(TRIGGER_COND_VAR, 0);

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

  // Open the session from the other hub:1 to the nanoapp
  SessionId sessionId =
      messageHub->openSession(kEndpointInfos[0].id,
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

}  // namespace
}  // namespace chre::message
