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

#pragma once

#include "gmock/gmock.h"

#include "chre/util/system/message_common.h"
#include "chre/util/system/message_router.h"

#include "pw_allocator/unique_ptr.h"
#include "pw_function/function.h"

namespace chre::message {

class MockMessageHubCallback : public MessageRouter::MessageHubCallback {
 public:
  MOCK_METHOD(bool, onMessageReceived,
              (pw::UniquePtr<std::byte[]> && data, uint32_t messageType,
               uint32_t messagePermissions, const Session &session,
               bool sentBySessionInitiator),
              (override));
  MOCK_METHOD(void, onSessionOpenRequest, (const Session &session), (override));
  MOCK_METHOD(void, onSessionOpened, (const Session &session), (override));
  MOCK_METHOD(void, onSessionClosed, (const Session &session, Reason reason),
              (override));
  MOCK_METHOD(void, forEachEndpoint,
              (const pw::Function<bool(const EndpointInfo &)> &function),
              (override));
  MOCK_METHOD(std::optional<EndpointInfo>, getEndpointInfo,
              (EndpointId endpointId), (override));
  MOCK_METHOD(std::optional<EndpointId>, getEndpointForService,
              (const char *serviceDescriptor), (override));
  MOCK_METHOD(bool, doesEndpointHaveService,
              (EndpointId endpointId, const char *serviceDescriptor),
              (override));
  MOCK_METHOD(
      void, forEachService,
      (const pw::Function<bool(const EndpointInfo &,
                               const message::ServiceInfo &)> &function),
      (override));
  MOCK_METHOD(void, onHubRegistered, (const MessageHubInfo &), (override));
  MOCK_METHOD(void, onHubUnregistered, (MessageHubId), (override));
  MOCK_METHOD(void, onEndpointRegistered,
              (MessageHubId messageHubId, EndpointId endpointId), (override));
  MOCK_METHOD(void, onEndpointUnregistered,
              (MessageHubId messageHubId, EndpointId endpointId), (override));

  void pw_recycle() override {
    delete this;
  }
};

}  // namespace chre::message
