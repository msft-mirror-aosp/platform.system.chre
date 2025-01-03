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
#include "chre/util/system/message_router_callback_allocator.h"
#include "chre/util/unique_ptr.h"
#include "chre_api/chre.h"
#include "pw_containers/vector.h"

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

  //! Constructor for the ChreMessageHubManager
  ChreMessageHubManager();

  //! Initializes the ChreMessageHubManager
  void init();

  //! @return the MessageHub for the CHRE Message Hub
  message::MessageRouter::MessageHub &getMessageHub() {
    return mChreMessageHub;
  }

  //! Gets endpoint information for the given hub and endpoint IDs.
  //! @return whether the endpoint information was successfully populated.
  bool getEndpointInfo(message::MessageHubId hubId,
                       message::EndpointId endpointId,
                       chreMsgEndpointInfo &info);

  //! Gets session information for the given session ID.
  //! @return whether the session information was successfully populated.
  bool getSessionInfo(message::EndpointId fromEndpointId,
                      message::SessionId sessionId, chreMsgSessionInfo &info);

  //! Opens a session from the given endpoint to the other endpoint in an
  //! asynchronous manner.
  //! @return The session ID or SESSION_ID_INVALID if the session could
  //! not be opened
  bool openSessionAsync(uint16_t nanoappInstanceId,
                        message::EndpointId fromEndpointId,
                        message::MessageHubId toHubId,
                        message::EndpointId toEndpointId);

  //! Opens a session from the given endpoint to the other endpoint. This
  //! method searches for the other endpoint by endpoint ID and opens a
  //! session with the first endpoint that matches.
  //! @return The session ID or SESSION_ID_INVALID if the session could
  //! not be opened
  bool openDefaultSessionAsync(uint16_t nanoappInstanceId,
                               message::EndpointId fromEndpointId,
                               message::EndpointId toEndpointId);

  //! Sends a reliable message on the given session. If this function fails,
  //! the free callback will be called and it will return false.
  //! @return whether the message was successfully sent
  bool sendMessage(void *message, size_t messageSize, uint32_t messageType,
                   uint16_t sessionId, uint32_t messagePermissions,
                   chreMessageFreeFunction *freeCallback,
                   message::EndpointId fromEndpointId);

  //! Converts a message::EndpointType to a CHRE endpoint type
  //! @return the CHRE endpoint type
  chreMsgEndpointType toChreEndpointType(message::EndpointType type);

 private:
  //! Data to be passed to the message callback
  struct MessageCallbackData {
    chreMsgMessageFromEndpointData messageToNanoapp;
    pw::UniquePtr<std::byte[]> data;
    uint64_t nanoappId;
  };

  //! Data to be passed to the message free callback
  struct MessageFreeCallbackData {
    chreMessageFreeFunction *freeCallback;
    uint64_t nanoappId;
  };

  //! Data to be passed to the session closed callback
  struct SessionCallbackData {
    chreMsgSessionInfo sessionData;
    uint64_t nanoappId;
  };

  constexpr static size_t kMaxFreeCallbackRecords = 25;

  //! Callback to process message sent to a nanoapp - used by the event loop
  static void onMessageToNanoappCallback(
      SystemCallbackType type,
      UniquePtr<ChreMessageHubManager::MessageCallbackData> &&data);

  //! Callback to process session closed event for a nanoapp - used by the event
  //! loop
  static void onSessionClosedCallback(
      SystemCallbackType type,
      UniquePtr<ChreMessageHubManager::SessionCallbackData> &&data);

  //! Callback called when a message is freed
  static void onMessageFreeCallback(std::byte *message, size_t length,
                                    MessageFreeCallbackData &&callbackData);

  //! Callback passed to deferCallback when handling a message free callback
  static void handleMessageFreeCallback(uint16_t type, void *data,
                                        void *extraData);

  //! @return The free callback record from the callback allocator.
  std::optional<message::MessageRouterCallbackAllocator<
      MessageFreeCallbackData>::FreeCallbackRecord>
  getAndRemoveFreeCallbackRecord(void *ptr) {
    return mAllocator.GetAndRemoveFreeCallbackRecord(ptr);
  }

  //! Definitions for MessageHubCallback
  //! @see MessageRouter::MessageHubCallback
  bool onMessageReceived(pw::UniquePtr<std::byte[]> &&data,
                         uint32_t messageType, uint32_t messagePermissions,
                         const message::Session &session,
                         bool sentBySessionInitiator) override;
  void onSessionClosed(const message::Session &session) override;
  void forEachEndpoint(const pw::Function<bool(const message::EndpointInfo &)>
                           &function) override;
  std::optional<message::EndpointInfo> getEndpointInfo(
      message::EndpointId endpointId) override;

  //! The MessageHub for the CHRE
  message::MessageRouter::MessageHub mChreMessageHub;

  //! The vector of free callback records - used by the
  //! MessageRouterCallbackAllocator
  pw::Vector<message::MessageRouterCallbackAllocator<
                 MessageFreeCallbackData>::FreeCallbackRecord,
             kMaxFreeCallbackRecords>
      mFreeCallbackRecords;

  //! The allocator for message free callbacks - used when sending a message
  //! from a nanoapp with a free callback
  message::MessageRouterCallbackAllocator<MessageFreeCallbackData> mAllocator;
};

}  // namespace chre

#endif  // CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED

#endif  // CHRE_CORE_CHRE_MESSAGE_HUB_MANAGER_H_
