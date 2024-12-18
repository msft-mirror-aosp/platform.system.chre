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

#ifdef CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED

#include "chre/core/chre_message_hub_manager.h"
#include "chre/core/event_loop_common.h"
#include "chre/core/event_loop_manager.h"
#include "chre/core/nanoapp.h"
#include "chre/util/system/message_common.h"
#include "chre/util/system/message_router.h"
#include "chre/util/unique_ptr.h"

#include <cinttypes>
#include <optional>

using ::chre::message::Endpoint;
using ::chre::message::EndpointId;
using ::chre::message::EndpointInfo;
using ::chre::message::EndpointType;
using ::chre::message::Message;
using ::chre::message::MessageRouter;
using ::chre::message::MessageRouterSingleton;
using ::chre::message::Session;

namespace chre {

void ChreMessageHubManager::init() {
  std::optional<MessageRouter::MessageHub> chreMessageHub =
      MessageRouterSingleton::get()->registerMessageHub(
          "CHRE", kChreMessageHubId, *this);
  if (chreMessageHub.has_value()) {
    mChreMessageHub = std::move(*chreMessageHub);
  } else {
    LOGE("Failed to register the CHRE MessageHub");
  }
}

void ChreMessageHubManager::onMessageToNanoappCallback(
    SystemCallbackType /* type */, UniquePtr<MessageCallbackData> &&data) {
  bool success = false;
  Nanoapp *nanoapp =
      EventLoopManagerSingleton::get()->getEventLoop().findNanoappByAppId(
          data->nanoappId);
  uint32_t messagePermissions = data->messageToNanoapp.messagePermissions;
  if (nanoapp == nullptr) {
    LOGE("Unable to find nanoapp with ID 0x%" PRIx64
         " to receive message with type %" PRIu32 " and permissions %" PRIu32
         " with session ID %" PRIu16,
         data->nanoappId, data->messageToNanoapp.messageType,
         data->messageToNanoapp.messagePermissions,
         data->messageToNanoapp.sessionId);
  } else if (!nanoapp->hasPermissions(messagePermissions)) {
    LOGE("nanoapp with ID 0x%" PRIx64
         " does not have permissions to receive "
         "message with type %" PRIu32 " and permissions 0x%" PRIx32,
         nanoapp->getAppId(), data->messageToNanoapp.messageType,
         data->messageToNanoapp.messagePermissions);
  } else if (!EventLoopManagerSingleton::get()
                  ->getEventLoop()
                  .distributeEventSync(CHRE_EVENT_MSG_FROM_ENDPOINT,
                                       &data->messageToNanoapp,
                                       nanoapp->getInstanceId())) {
    LOGE("Unable to distribute message to nanoapp with ID 0x%" PRIx64,
         nanoapp->getAppId());
  } else {
    success = true;
  }

  // Close session on failure so sender knows there was an issue
  if (!success) {
    EventLoopManagerSingleton::get()
        ->getChreMessageHubManager()
        .getMessageHub()
        .closeSession(data->messageToNanoapp.sessionId);
  }
}

void ChreMessageHubManager::onSessionClosedCallback(
    SystemCallbackType /* type */,
    UniquePtr<SessionClosedCallbackData> &&data) {
  Nanoapp *nanoapp =
      EventLoopManagerSingleton::get()->getEventLoop().findNanoappByAppId(
          data->nanoappId);
  if (nanoapp == nullptr) {
    LOGE("Unable to find nanoapp with ID 0x%" PRIx64
         " to close the session with ID %" PRIu16,
         data->nanoappId, data->sessionClosedData.sessionId);
    return;
  }

  bool success =
      EventLoopManagerSingleton::get()->getEventLoop().distributeEventSync(
          CHRE_EVENT_MSG_SESSION_CLOSED, &data->sessionClosedData,
          nanoapp->getInstanceId());
  if (!success) {
    LOGE("Unable to process session closed event to nanoapp with ID 0x%" PRIx64,
         nanoapp->getAppId());
  }
}

bool ChreMessageHubManager::onMessageReceived(pw::UniquePtr<std::byte[]> &&data,
                                              uint32_t messageType,
                                              uint32_t messagePermissions,
                                              const Session &session,
                                              bool sentBySessionInitiator) {
  Endpoint receiver = sentBySessionInitiator ? session.peer : session.initiator;
  auto messageCallbackData = MakeUnique<MessageCallbackData>();
  if (messageCallbackData.isNull()) {
    LOG_OOM();
    return false;
  }

  messageCallbackData->messageToNanoapp = {
      .messageType = messageType,
      .messagePermissions = messagePermissions,
      .message = data.get(),
      .messageSize = data.size(),
      .sessionId = session.sessionId,
  };
  messageCallbackData->data = std::move(data);
  messageCallbackData->nanoappId = receiver.endpointId;

  EventLoopManagerSingleton::get()->deferCallback(
      SystemCallbackType::EndpointMessageToNanoappEvent,
      std::move(messageCallbackData),
      ChreMessageHubManager::onMessageToNanoappCallback);
  return true;
}

void ChreMessageHubManager::onSessionClosed(const Session &session) {
  auto sessionClosedCallbackData = MakeUnique<SessionClosedCallbackData>();
  if (sessionClosedCallbackData.isNull()) {
    LOG_OOM();
    return;
  }

  Endpoint otherParty;
  uint64_t nanoappId;
  if (session.initiator.messageHubId == kChreMessageHubId) {
    otherParty = session.peer;
    nanoappId = session.initiator.endpointId;
  } else {
    otherParty = session.initiator;
    nanoappId = session.peer.endpointId;
  }

  sessionClosedCallbackData->sessionClosedData = {
      .hubId = otherParty.messageHubId,
      .endpointId = otherParty.endpointId,
      .sessionId = session.sessionId,
  };
  sessionClosedCallbackData->nanoappId = nanoappId;

  EventLoopManagerSingleton::get()->deferCallback(
      SystemCallbackType::EndpointSessionClosedEvent,
      std::move(sessionClosedCallbackData),
      ChreMessageHubManager::onSessionClosedCallback);
}

void ChreMessageHubManager::forEachEndpoint(
    const pw::Function<bool(const EndpointInfo &)> &function) {
  EventLoopManagerSingleton::get()->getEventLoop().onMatchingNanoappEndpoint(
      function);
}

std::optional<EndpointInfo> ChreMessageHubManager::getEndpointInfo(
    EndpointId endpointId) {
  return EventLoopManagerSingleton::get()->getEventLoop().getEndpointInfo(
      endpointId);
}

}  // namespace chre

#endif  // CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED
