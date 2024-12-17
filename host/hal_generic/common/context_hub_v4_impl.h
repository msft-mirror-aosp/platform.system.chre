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

#pragma once

#include <assert.h>

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <aidl/android/hardware/contexthub/BnContextHub.h>
#include <aidl/android/hardware/contexthub/BnEndpointCommunication.h>
#include <chre_host/generated/host_messages_generated.h>

#include "message_hub_manager.h"

namespace android::hardware::contexthub::common::implementation {

using ::aidl::android::hardware::contexthub::BnEndpointCommunication;
using ::aidl::android::hardware::contexthub::EndpointId;
using ::aidl::android::hardware::contexthub::EndpointInfo;
using ::aidl::android::hardware::contexthub::HubInfo;
using ::aidl::android::hardware::contexthub::IEndpointCallback;
using ::aidl::android::hardware::contexthub::IEndpointCommunication;
using ::aidl::android::hardware::contexthub::Message;
using ::aidl::android::hardware::contexthub::MessageDeliveryStatus;
using ::aidl::android::hardware::contexthub::Reason;
using ::ndk::ScopedAStatus;

/**
 * Common parts of the IContextHub V4+ interface which can be shared by
 * various HAL implementations.
 */
class ContextHubV4Impl {
 public:
  using SendMessageFn = std::function<bool(uint8_t *data, size_t size)>;

  explicit ContextHubV4Impl(SendMessageFn sendMessageFn)
      : mManager([this](int64_t id) { onHostHubDown(id); }),
        mSendMessageFn(std::move(sendMessageFn)) {}
  ~ContextHubV4Impl() = default;

  /**
   * Initializes the implementation.
   *
   * This should be called once a connection with CHRE has been established.
   * Requests a dump of embedded hubs and endpoints from CHRE.
   */
  void init();

  // IContextHub (V4+) API implementation.
  ScopedAStatus getHubs(std::vector<HubInfo> *hubs);
  ScopedAStatus getEndpoints(std::vector<EndpointInfo> *endpoints);
  ScopedAStatus registerEndpointHub(
      const std::shared_ptr<IEndpointCallback> &callback,
      const HubInfo &hubInfo,
      std::shared_ptr<IEndpointCommunication> *hubInterface);

  // TODO(b/385474431): Add dump().

  /**
   * Handles a CHRE message that is part of the V4 implementation.
   *
   * @param message Validated union of the various message types.
   * @return true if the message could be handled
   */
  bool handleMessageFromChre(const ::chre::fbs::ChreMessageUnion &message);

 private:
  // Callbacks for each message type from CHRE.
  void onGetMessageHubsAndEndpointsResponse(
      const ::chre::fbs::GetMessageHubsAndEndpointsResponseT &msg);
  void onRegisterMessageHub(const ::chre::fbs::RegisterMessageHubT &msg);
  void onUnregisterMessageHub(const ::chre::fbs::UnregisterMessageHubT &msg);
  void onRegisterEndpoint(const ::chre::fbs::RegisterEndpointT &msg);
  void onUnregisterEndpoint(const ::chre::fbs::UnregisterEndpointT &msg);
  void onOpenEndpointSessionRequest(
      const ::chre::fbs::OpenEndpointSessionRequestT &msg);
  void onEndpointSessionOpened(const ::chre::fbs::EndpointSessionOpenedT &msg);
  void onEndpointSessionClosed(const ::chre::fbs::EndpointSessionClosedT &msg);
  void onEndpointSessionMessage(
      const ::chre::fbs::EndpointSessionMessageT &msg);
  void onEndpointSessionMessageDeliveryStatus(
      const ::chre::fbs::EndpointSessionMessageDeliveryStatusT &msg);

  // Callback invoked when a HAL client associated with a host hub goes down.
  void onHostHubDown(int64_t id);

  MessageHubManager mManager;
  SendMessageFn mSendMessageFn;
};

/**
 * Wrapper for a MessageHubManager::HostHub instance implementing
 * IEndpointCommunication so that a client can directly make calls on its
 * associated HostHub.
 */
class HostHubInterface : public BnEndpointCommunication {
 public:
  explicit HostHubInterface(std::shared_ptr<MessageHubManager::HostHub> hub)
      : mHub(std::move(hub)) {
    assert(mHub != nullptr);
  }
  ~HostHubInterface() = default;

  // Implementation of IEndpointCommunication.
  ScopedAStatus registerEndpoint(const EndpointInfo &endpoint) override;
  ScopedAStatus unregisterEndpoint(const EndpointInfo &endpoint) override;
  ScopedAStatus requestSessionIdRange(int32_t size,
                                      std::array<int32_t, 2> *ids);
  ScopedAStatus openEndpointSession(
      int32_t sessionId, const EndpointId &destination,
      const EndpointId &initiator,
      const std::optional<std::string> &serviceDescriptor) override;
  ScopedAStatus sendMessageToEndpoint(int32_t sessionId,
                                      const Message &msg) override;
  ScopedAStatus sendMessageDeliveryStatusToEndpoint(
      int32_t sessionId, const MessageDeliveryStatus &msgStatus) override;
  ScopedAStatus closeEndpointSession(int32_t sessionId, Reason reason) override;
  ScopedAStatus endpointSessionOpenComplete(int32_t sessionId) override;
  ScopedAStatus unregister() override;

 private:
  std::shared_ptr<MessageHubManager::HostHub> mHub;
};

}  // namespace android::hardware::contexthub::common::implementation
