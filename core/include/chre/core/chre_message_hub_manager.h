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

#ifndef CHRE_CORE_CHRE_MESSAGE_HUB_MANAGER_H_
#define CHRE_CORE_CHRE_MESSAGE_HUB_MANAGER_H_

#ifdef CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED

#include "chre/core/event_loop_common.h"
#include "chre/util/non_copyable.h"
#include "chre/util/system/message_common.h"
#include "chre/util/system/message_router.h"
#include "chre/util/unique_ptr.h"

#include <cinttypes>
#include <optional>

namespace chre {

//! Manager class for the CHRE Message Hub.
class ChreMessageHubManager
    : public NonCopyable,
      public message::MessageRouter::MessageHubCallback {
 public:
  //! The ID of the CHRE MessageHub
  constexpr static message::MessageHubId kChreMessageHubId = CHRE_PLATFORM_ID;

  //! Initializes the ChreMessageHubManager
  void init();

  //! Returns the MessageHub for the CHRE Message Hub
  message::MessageRouter::MessageHub &getMessageHub() {
    return mChreMessageHub;
  }

 private:
  //! Data to be passed to the message callback
  struct MessageCallbackData {
    chreMessageFromEndpointData messageToNanoapp;
    pw::UniquePtr<std::byte[]> data;
    uint64_t nanoappId;
  };

  //! Data to be passed to the session closed callback
  struct SessionClosedCallbackData {
    chreEndpointSessionClosedData sessionClosedData;
    uint64_t nanoappId;
  };

  //! Callback to process message sent to a nanoapp - used by the event loop
  static void onMessageToNanoappCallback(
      SystemCallbackType /* type */,
      UniquePtr<ChreMessageHubManager::MessageCallbackData> &&data);

  //! Callback to process session closed event for a nanoapp - used by the event
  //! loop
  static void onSessionClosedCallback(
      SystemCallbackType /* type */,
      UniquePtr<ChreMessageHubManager::SessionClosedCallbackData> &&data);

  //! Definitions for MessageHubCallback
  //! @see MessageRouter::MessageHubCallback
  bool onMessageReceived(pw::UniquePtr<std::byte[]> &&data, size_t length,
                         uint32_t messageType, uint32_t messagePermissions,
                         const message::Session &session,
                         bool sentBySessionInitiator) override;
  void onSessionClosed(const message::Session &session) override;
  void forEachEndpoint(const pw::Function<bool(const message::EndpointInfo &)>
                           &function) override;
  std::optional<message::EndpointInfo> getEndpointInfo(
      message::EndpointId endpointId) override;

  message::MessageRouter::MessageHub mChreMessageHub;
};

}  // namespace chre

#endif  // CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED

#endif  // CHRE_CORE_CHRE_MESSAGE_HUB_MANAGER_H_
