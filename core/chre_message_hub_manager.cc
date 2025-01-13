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

#include "chre/target_platform/log.h"
#ifdef CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED

#include "chre/core/chre_message_hub_manager.h"
#include "chre/core/event_loop_common.h"
#include "chre/core/event_loop_manager.h"
#include "chre/core/nanoapp.h"
#include "chre/util/system/event_callbacks.h"
#include "chre/util/system/message_common.h"
#include "chre/util/system/message_router.h"
#include "chre/util/unique_ptr.h"
#include "chre_api/chre.h"
#include "pw_allocator/unique_ptr.h"

#include <cinttypes>
#include <cstring>
#include <optional>

using ::chre::message::Endpoint;
using ::chre::message::EndpointId;
using ::chre::message::EndpointInfo;
using ::chre::message::EndpointType;
using ::chre::message::Message;
using ::chre::message::MESSAGE_HUB_ID_INVALID;
using ::chre::message::MessageHubId;
using ::chre::message::MessageHubInfo;
using ::chre::message::MessageRouter;
using ::chre::message::MessageRouterSingleton;
using ::chre::message::Reason;
using ::chre::message::Session;
using ::chre::message::SESSION_ID_INVALID;
using ::chre::message::SessionId;

namespace chre {

ChreMessageHubManager::ChreMessageHubManager()
    : mAllocator(ChreMessageHubManager::onMessageFreeCallback,
                 mFreeCallbackRecords, /* doEraseRecord= */ false) {}

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

bool ChreMessageHubManager::getEndpointInfo(MessageHubId hubId,
                                            EndpointId endpointId,
                                            chreMsgEndpointInfo &info) {
  std::optional<EndpointInfo> endpointInfo =
      MessageRouterSingleton::get()->getEndpointInfo(hubId, endpointId);
  if (!endpointInfo.has_value()) {
    return false;
  }

  info.hubId = hubId;
  info.endpointId = endpointId;
  info.type = toChreEndpointType(endpointInfo->type);
  info.version = endpointInfo->version;
  info.requiredPermissions = endpointInfo->requiredPermissions;
  std::strncpy(info.name, endpointInfo->name, CHRE_MAX_ENDPOINT_NAME_LEN);
  info.name[CHRE_MAX_ENDPOINT_NAME_LEN - 1] = '\0';
  return true;
}

bool ChreMessageHubManager::getSessionInfo(EndpointId fromEndpointId,
                                           SessionId sessionId,
                                           chreMsgSessionInfo &info) {
  std::optional<Session> session = mChreMessageHub.getSessionWithId(sessionId);
  if (!session.has_value()) {
    return false;
  }

  bool initiatorIsNanoapp =
      session->initiator.messageHubId == kChreMessageHubId &&
      session->initiator.endpointId == fromEndpointId;
  bool peerIsNanoapp = session->peer.messageHubId == kChreMessageHubId &&
                       session->peer.endpointId == fromEndpointId;
  if (!initiatorIsNanoapp && !peerIsNanoapp) {
    LOGE("Nanoapp with ID 0x%" PRIx64
         " is not the initiator or peer of session with ID %" PRIu16,
         fromEndpointId, sessionId);
    return false;
  }

  info.hubId = initiatorIsNanoapp ? session->peer.messageHubId
                                  : session->initiator.messageHubId;
  info.endpointId = initiatorIsNanoapp ? session->peer.endpointId
                                       : session->initiator.endpointId;
  // TODO(b/373417024): Add service descriptor after MessageRouter changes
  info.serviceDescriptor[0] = '\0';
  info.sessionId = sessionId;
  info.reason = chreMsgEndpointReason::CHRE_MSG_ENDPOINT_REASON_UNSPECIFIED;
  return true;
}

bool ChreMessageHubManager::openSessionAsync(EndpointId fromEndpointId,
                                             MessageHubId toHubId,
                                             EndpointId toEndpointId) {
  SessionId sessionId =
      EventLoopManagerSingleton::get()
          ->getChreMessageHubManager()
          .getMessageHub()
          .openSession(fromEndpointId, toHubId,
                       toEndpointId);
  return sessionId != SESSION_ID_INVALID;
}

bool ChreMessageHubManager::openDefaultSessionAsync(EndpointId fromEndpointId,
                                                    EndpointId toEndpointId) {
  struct SearchContext {
    MessageHubId toMessageHubId = MESSAGE_HUB_ID_INVALID;
    EndpointId toEndpointId;
  };
  SearchContext context = {
      .toEndpointId = toEndpointId,
  };

  MessageRouterSingleton::get()->forEachEndpoint(
      [&context](const MessageHubInfo &hubInfo,
                 const EndpointInfo &endpointInfo) {
        if (context.toMessageHubId == MESSAGE_HUB_ID_INVALID &&
            endpointInfo.id == context.toEndpointId) {
          context.toMessageHubId = hubInfo.id;
        }
      });

  return context.toMessageHubId != MESSAGE_HUB_ID_INVALID &&
         openSessionAsync(fromEndpointId, context.toMessageHubId, toEndpointId);
}

bool ChreMessageHubManager::sendMessage(
    void *message, size_t messageSize, uint32_t messageType, uint16_t sessionId,
    uint32_t messagePermissions, chreMessageFreeFunction *freeCallback,
    message::EndpointId fromEndpointId) {
  bool success = false;
  pw::UniquePtr<std::byte[]> messageData =
      mAllocator.MakeUniqueArrayWithCallback(
          reinterpret_cast<std::byte *>(message), messageSize,
          MessageFreeCallbackData{
              .freeCallback = freeCallback,
              .nanoappId = fromEndpointId});
  if (messageData == nullptr) {
    LOG_OOM();
  } else {
    success = mChreMessageHub.sendMessage(std::move(messageData), messageType,
                                          messagePermissions, sessionId);
  }

  if (!success && freeCallback != nullptr) {
    freeCallback(message, messageSize);
  }
  return success;
}

chreMsgEndpointType ChreMessageHubManager::toChreEndpointType(
    message::EndpointType type) {
  switch (type) {
    case message::EndpointType::HOST_FRAMEWORK:
      return chreMsgEndpointType::CHRE_MSG_ENDPOINT_TYPE_HOST_FRAMEWORK;
    case message::EndpointType::HOST_APP:
      return chreMsgEndpointType::CHRE_MSG_ENDPOINT_TYPE_HOST_APP;
    case message::EndpointType::HOST_NATIVE:
      return chreMsgEndpointType::CHRE_MSG_ENDPOINT_TYPE_HOST_NATIVE;
    case message::EndpointType::NANOAPP:
      return chreMsgEndpointType::CHRE_MSG_ENDPOINT_TYPE_NANOAPP;
    case message::EndpointType::GENERIC:
      return chreMsgEndpointType::CHRE_MSG_ENDPOINT_TYPE_GENERIC;
    default:
      LOGE("Unknown endpoint type: %" PRIu8, type);
      return chreMsgEndpointType::CHRE_MSG_ENDPOINT_TYPE_INVALID;
  }
}

chreMsgEndpointReason ChreMessageHubManager::toChreEndpointReason(
    message::Reason reason) {
  switch (reason) {
    case message::Reason::UNSPECIFIED:
      return chreMsgEndpointReason::CHRE_MSG_ENDPOINT_REASON_UNSPECIFIED;
    case message::Reason::OUT_OF_MEMORY:
      return chreMsgEndpointReason::CHRE_MSG_ENDPOINT_REASON_OUT_OF_MEMORY;
    case message::Reason::TIMEOUT:
      return chreMsgEndpointReason::CHRE_MSG_ENDPOINT_REASON_TIMEOUT;
    case message::Reason::OPEN_ENDPOINT_SESSION_REQUEST_REJECTED:
      return chreMsgEndpointReason::
          CHRE_MSG_ENDPOINT_REASON_OPEN_ENDPOINT_SESSION_REQUEST_REJECTED;
    case message::Reason::CLOSE_ENDPOINT_SESSION_REQUESTED:
      return chreMsgEndpointReason::
          CHRE_MSG_ENDPOINT_REASON_CLOSE_ENDPOINT_SESSION_REQUESTED;
    case message::Reason::ENDPOINT_INVALID:
      return chreMsgEndpointReason::CHRE_MSG_ENDPOINT_REASON_ENDPOINT_INVALID;
    case message::Reason::ENDPOINT_GONE:
      return chreMsgEndpointReason::CHRE_MSG_ENDPOINT_REASON_ENDPOINT_GONE;
    case message::Reason::ENDPOINT_CRASHED:
      return chreMsgEndpointReason::CHRE_MSG_ENDPOINT_REASON_ENDPOINT_CRASHED;
    case message::Reason::HUB_RESET:
      return chreMsgEndpointReason::CHRE_MSG_ENDPOINT_REASON_HUB_RESET;
    case message::Reason::PERMISSION_DENIED:
      return chreMsgEndpointReason::CHRE_MSG_ENDPOINT_REASON_PERMISSION_DENIED;
    default:
      LOGE("Unknown endpoint reason: %" PRIu8, reason);
      return chreMsgEndpointReason::CHRE_MSG_ENDPOINT_REASON_UNSPECIFIED;
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

void ChreMessageHubManager::onSessionStateChangedCallback(
    SystemCallbackType /* type */, UniquePtr<SessionCallbackData> &&data) {
  Nanoapp *nanoapp =
      EventLoopManagerSingleton::get()->getEventLoop().findNanoappByAppId(
          data->nanoappId);
  if (nanoapp == nullptr) {
    LOGE("Unable to find nanoapp with ID 0x%" PRIx64
         " to close the session with ID %" PRIu16,
         data->nanoappId, data->sessionData.sessionId);
    return;
  }

  bool success =
      EventLoopManagerSingleton::get()->getEventLoop().distributeEventSync(
          data->isClosed ? CHRE_EVENT_MSG_SESSION_CLOSED
                         : CHRE_EVENT_MSG_SESSION_OPENED,
          &data->sessionData, nanoapp->getInstanceId());
  if (!success) {
    LOGE("Unable to process session closed event to nanoapp with ID 0x%" PRIx64,
         nanoapp->getAppId());
  }
}

void ChreMessageHubManager::onMessageFreeCallback(
    std::byte *message, size_t /* length */,
    MessageFreeCallbackData && /* callbackData */) {
  EventLoopManagerSingleton::get()->deferCallback(
      SystemCallbackType::EndpointMessageFreeEvent,
      message,
      ChreMessageHubManager::handleMessageFreeCallback);
}

void ChreMessageHubManager::handleMessageFreeCallback(uint16_t /* type */,
                                                      void *data,
                                                      void* /* extraData */) {
  std::optional<message::MessageRouterCallbackAllocator<
      MessageFreeCallbackData>::FreeCallbackRecord>
      record = EventLoopManagerSingleton::get()
                    ->getChreMessageHubManager()
                    .getAndRemoveFreeCallbackRecord(data);
  if (!record.has_value()) {
    LOGE("Unable to find free callback record for message with message: %p",
         data);
    return;
  }

  if (record->metadata.freeCallback == nullptr) {
    return;
  }

  EventLoopManagerSingleton::get()->getEventLoop().invokeMessageFreeFunction(
      record->metadata.nanoappId, record->metadata.freeCallback,
      record->message, record->messageSize);
}

void ChreMessageHubManager::onSessionStateChanged(
    const message::Session &session, std::optional<message::Reason> reason) {
  auto sessionCallbackData = MakeUnique<SessionCallbackData>();
  if (sessionCallbackData.isNull()) {
    FATAL_ERROR_OOM();
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

  sessionCallbackData->nanoappId = nanoappId;
  sessionCallbackData->isClosed = reason.has_value();
  sessionCallbackData->sessionData = {
      .hubId = otherParty.messageHubId,
      .endpointId = otherParty.endpointId,
      .sessionId = session.sessionId,
  };
  sessionCallbackData->sessionData.reason =
      reason.has_value()
          ? toChreEndpointReason(*reason)
          : chreMsgEndpointReason::CHRE_MSG_ENDPOINT_REASON_UNSPECIFIED;
  // TODO(b/373417024): Add service descriptor after MessageRouter changes
  sessionCallbackData->sessionData.serviceDescriptor[0] = '\0';

  EventLoopManagerSingleton::get()->deferCallback(
      SystemCallbackType::EndpointSessionStateChangedEvent,
      std::move(sessionCallbackData),
      ChreMessageHubManager::onSessionStateChangedCallback);
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

  return EventLoopManagerSingleton::get()->deferCallback(
      SystemCallbackType::EndpointMessageToNanoappEvent,
      std::move(messageCallbackData),
      ChreMessageHubManager::onMessageToNanoappCallback);
}

void ChreMessageHubManager::onSessionOpenRequest(const Session &session) {
  mChreMessageHub.onSessionOpenComplete(session.sessionId);
}

void ChreMessageHubManager::onSessionOpened(const Session &session) {
  onSessionStateChanged(session, /* reason= */ std::nullopt);
}

void ChreMessageHubManager::onSessionClosed(const Session &session,
                                            Reason reason) {
  onSessionStateChanged(session, reason);
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
