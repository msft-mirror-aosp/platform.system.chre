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

#include <inttypes.h>
#include <cstring>
#include <optional>
#include <utility>

#include "chre/util/dynamic_vector.h"
#include "chre/util/lock_guard.h"
#include "chre/util/system/message_common.h"
#include "chre/util/system/message_router.h"

namespace chre::message {

MessageRouter::MessageHub::MessageHub()
    : mRouter(nullptr), mHubId(MESSAGE_HUB_ID_INVALID) {}

MessageRouter::MessageHub::MessageHub(MessageRouter &router, MessageHubId id)
    : mRouter(&router), mHubId(id) {}

MessageRouter::MessageHub::MessageHub(MessageHub &&other)
    : mRouter(other.mRouter), mHubId(other.mHubId) {
  other.mRouter = nullptr;
  other.mHubId = MESSAGE_HUB_ID_INVALID;
}

MessageRouter::MessageHub &MessageRouter::MessageHub::operator=(
    MessageHub &&other) {
  mRouter = other.mRouter;
  mHubId = other.mHubId;
  other.mRouter = nullptr;
  other.mHubId = MESSAGE_HUB_ID_INVALID;
  return *this;
}

void MessageRouter::MessageHub::onSessionOpenComplete(SessionId sessionId) {
  if (mRouter != nullptr) {
    mRouter->onSessionOpenComplete(mHubId, sessionId);
  }
}

SessionId MessageRouter::MessageHub::openSession(EndpointId fromEndpointId,
                                                 MessageHubId toMessageHubId,
                                                 EndpointId toEndpointId,
                                                 const char *serviceDescriptor,
                                                 SessionId sessionId) {
  return mRouter == nullptr
             ? SESSION_ID_INVALID
             : mRouter->openSession(mHubId, fromEndpointId, toMessageHubId,
                                    toEndpointId, serviceDescriptor, sessionId);
}

bool MessageRouter::MessageHub::closeSession(SessionId sessionId,
                                             Reason reason) {
  return mRouter != nullptr && mRouter->closeSession(mHubId, sessionId, reason);
}

std::optional<Session> MessageRouter::MessageHub::getSessionWithId(
    SessionId sessionId) {
  return mRouter == nullptr ? std::nullopt
                            : mRouter->getSessionWithId(mHubId, sessionId);
}

bool MessageRouter::MessageHub::sendMessage(pw::UniquePtr<std::byte[]> &&data,
                                            uint32_t messageType,
                                            uint32_t messagePermissions,
                                            SessionId sessionId,
                                            EndpointId fromEndpointId) {
  return mRouter != nullptr &&
         mRouter->sendMessage(std::move(data), messageType, messagePermissions,
                              sessionId, fromEndpointId, mHubId);
}

bool MessageRouter::MessageHub::registerEndpoint(EndpointId endpointId) {
  return mRouter != nullptr && mRouter->registerEndpoint(mHubId, endpointId);
}

bool MessageRouter::MessageHub::unregisterEndpoint(EndpointId endpointId) {
  return mRouter != nullptr && mRouter->unregisterEndpoint(mHubId, endpointId);
}

MessageHubId MessageRouter::MessageHub::getId() {
  return mHubId;
}

std::optional<typename MessageRouter::MessageHub>
MessageRouter::registerMessageHub(
    const char *name, MessageHubId id,
    MessageRouter::MessageRouter::MessageHubCallback &callback) {
  LockGuard<Mutex> lock(mMutex);
  if (mMessageHubs.full()) {
    LOGE(
        "Message hub '%s' not registered: maximum number of message hubs "
        "reached",
        name);
    return std::nullopt;
  }

  for (MessageHubRecord &messageHub : mMessageHubs) {
    if (std::strcmp(messageHub.info.name, name) == 0 ||
        messageHub.info.id == id) {
      LOGE(
          "Message hub '%s' not registered: hub with same name or ID already "
          "exists",
          name);
      return std::nullopt;
    }
  }

  MessageHubRecord messageHubRecord = {
      .info = {.id = id, .name = name},
      .callback = &callback,
  };
  mMessageHubs.push_back(std::move(messageHubRecord));
  return MessageHub(*this, id);
}

bool MessageRouter::forEachEndpointOfHub(
    MessageHubId messageHubId,
    const pw::Function<bool(const EndpointInfo &)> &function) {
  MessageRouter::MessageHubCallback *callback =
      getCallbackFromMessageHubId(messageHubId);
  if (callback == nullptr) {
    LOGE("Failed to find message hub with ID %" PRIu64, messageHubId);
    return false;
  }

  callback->forEachEndpoint(function);
  return true;
}

bool MessageRouter::forEachEndpoint(
    const pw::Function<void(const MessageHubInfo &, const EndpointInfo &)>
        &function) {
  std::optional<DynamicVector<MessageHubRecord>> messageHubRecords =
      getMessageHubRecords();
  if (!messageHubRecords.has_value()) {
    return false;
  }

  struct Context {
    decltype(function) function;
    const MessageHubInfo &messageHubInfo;
  };
  for (const MessageHubRecord &messageHubRecord : *messageHubRecords) {
    Context context = {
        .function = function,
        .messageHubInfo = messageHubRecord.info,
    };

    messageHubRecord.callback->forEachEndpoint(
        [&context](const EndpointInfo &endpointInfo) {
          context.function(context.messageHubInfo, endpointInfo);
          return false;
        });
  }
  return true;
}

std::optional<EndpointInfo> MessageRouter::getEndpointInfo(
    MessageHubId messageHubId, EndpointId endpointId) {
  MessageRouter::MessageHubCallback *callback =
      getCallbackFromMessageHubId(messageHubId);
  if (callback == nullptr) {
    LOGE("Failed to get endpoint info for message hub with ID %" PRIu64
         " and endpoint ID %" PRIu64 ": hub not found",
         messageHubId, endpointId);
    return std::nullopt;
  }

  return callback->getEndpointInfo(endpointId);
}

std::optional<Endpoint> MessageRouter::getEndpointForService(
    MessageHubId messageHubId, const char *serviceDescriptor) {
  if (serviceDescriptor == nullptr) {
    LOGE("Failed to get endpoint for service: service descriptor is null");
    return std::nullopt;
  }

  std::optional<DynamicVector<MessageHubRecord>> messageHubRecords =
      getMessageHubRecords();
  if (!messageHubRecords.has_value()) {
    return std::nullopt;
  }

  for (const MessageHubRecord &messageHubRecord : *messageHubRecords) {
    if ((messageHubId == MESSAGE_HUB_ID_ANY ||
         messageHubId == messageHubRecord.info.id) &&
        messageHubRecord.callback != nullptr) {
      std::optional<EndpointId> endpointId =
          messageHubRecord.callback->getEndpointForService(serviceDescriptor);
      if (endpointId.has_value()) {
        return Endpoint(messageHubRecord.info.id, *endpointId);
      }

      // Only searching this message hub, so return early if not found
      if (messageHubId != MESSAGE_HUB_ID_ANY) {
        return std::nullopt;
      }
    }
  }
  return std::nullopt;
}

bool MessageRouter::doesEndpointHaveService(MessageHubId messageHubId,
                                            EndpointId endpointId,
                                            const char *serviceDescriptor) {
  if (serviceDescriptor == nullptr) {
    LOGE("Failed to check if endpoint has service: service descriptor is null");
    return false;
  }

  MessageRouter::MessageHubCallback *callback =
      getCallbackFromMessageHubId(messageHubId);
  if (callback == nullptr) {
    LOGE(
        "Failed to check if endpoint has service for message hub with ID "
        "%" PRIu64 " and endpoint ID %" PRIu64 ": hub not found",
        messageHubId, endpointId);
    return false;
  }
  return callback->doesEndpointHaveService(endpointId, serviceDescriptor);
}

bool MessageRouter::forEachMessageHub(
    const pw::Function<bool(const MessageHubInfo &)> &function) {
  std::optional<DynamicVector<MessageHubRecord>> messageHubRecords =
      getMessageHubRecords();
  if (!messageHubRecords.has_value()) {
    return false;
  }

  for (const MessageHubRecord &messageHubRecord : *messageHubRecords) {
    function(messageHubRecord.info);
  }
  return true;
}

bool MessageRouter::unregisterMessageHub(MessageHubId fromMessageHubId) {
  DynamicVector<std::pair<MessageHubCallback *, Session>> sessionsToDestroy;

  {
    LockGuard<Mutex> lock(mMutex);

    bool success = false;
    for (MessageHubRecord &messageHubRecord : mMessageHubs) {
      if (messageHubRecord.info.id == fromMessageHubId) {
        mMessageHubs.erase(&messageHubRecord);
        success = true;
        break;
      }
    }
    if (!success) {
      return false;
    }

    for (size_t i = 0; i < mSessions.size();) {
      Session &session = mSessions[i];
      bool initiatorIsFromHub =
          session.initiator.messageHubId == fromMessageHubId;
      bool peerIsFromHub = session.peer.messageHubId == fromMessageHubId;

      if (initiatorIsFromHub || peerIsFromHub) {
        MessageHubCallback *callback = getCallbackFromMessageHubIdLocked(
            initiatorIsFromHub ? session.peer.messageHubId
                               : session.initiator.messageHubId);
        sessionsToDestroy.push_back(std::make_pair(callback, session));
        mSessions.erase(&mSessions[i]);
      } else {
        ++i;
      }
    }
  }

  for (auto [callback, session] : sessionsToDestroy) {
    if (callback != nullptr) {
      callback->onSessionClosed(session, Reason::HUB_RESET);
    }
  }
  return true;
}

void MessageRouter::onSessionOpenComplete(MessageHubId fromMessageHubId,
                                          SessionId sessionId) {
  finalizeSession(fromMessageHubId, sessionId, /* reason = */ std::nullopt);
}

SessionId MessageRouter::openSession(MessageHubId fromMessageHubId,
                                     EndpointId fromEndpointId,
                                     MessageHubId toMessageHubId,
                                     EndpointId toEndpointId,
                                     const char *serviceDescriptor,
                                     SessionId sessionId) {
  if (sessionId != SESSION_ID_INVALID && sessionId < kReservedSessionId) {
    LOGE("Failed to open session: session ID %" PRIu16
         " is not in the reserved range",
         sessionId);
    return SESSION_ID_INVALID;
  }

  MessageRouter::MessageHubCallback *initiatorCallback =
      getCallbackFromMessageHubId(fromMessageHubId);
  MessageRouter::MessageHubCallback *peerCallback =
      getCallbackFromMessageHubId(toMessageHubId);
  if (initiatorCallback == nullptr || peerCallback == nullptr) {
    LOGE("Failed to open session: %s message hub not found",
         initiatorCallback == nullptr ? "initiator" : "peer");
    return SESSION_ID_INVALID;
  }

  if (!checkIfEndpointExists(initiatorCallback, fromEndpointId)) {
    LOGE("Failed to open session: endpoint with ID %" PRIu64
         " not found in message hub with ID %" PRIu64,
         fromEndpointId, fromMessageHubId);
    return SESSION_ID_INVALID;
  }

  if (!checkIfEndpointExists(peerCallback, toEndpointId)) {
    LOGE("Failed to open session: endpoint with ID %" PRIu64
         " not found in message hub with ID %" PRIu64,
         toEndpointId, toMessageHubId);
    return SESSION_ID_INVALID;
  }

  if (serviceDescriptor != nullptr &&
      !peerCallback->doesEndpointHaveService(toEndpointId, serviceDescriptor)) {
    LOGE("Failed to open session: endpoint with ID %" PRIu64
         " does not have service descriptor '%s'",
         toEndpointId, serviceDescriptor);
    return SESSION_ID_INVALID;
  }

  Session session(SESSION_ID_INVALID,
                  Endpoint(fromMessageHubId, fromEndpointId),
                  Endpoint(toMessageHubId, toEndpointId), serviceDescriptor);
  {
    LockGuard<Mutex> lock(mMutex);
    if (mSessions.full()) {
      LOGE("Failed to open session: maximum number of sessions reached");
      return SESSION_ID_INVALID;
    }

    bool foundSession = false;
    for (Session &existingSession : mSessions) {
      if (existingSession.isEquivalent(session)) {
        LOGD("Session with ID %" PRIu16 " already exists",
             existingSession.sessionId);
        session = existingSession;
        foundSession = true;
        break;
      }
    }

    if (!foundSession) {
      if (sessionId == SESSION_ID_INVALID) {
        sessionId = getNextSessionIdLocked();
        if (sessionId == SESSION_ID_INVALID) {
          LOGE("Failed to open session: no available session ID");
          return SESSION_ID_INVALID;
        }
      }

      session.sessionId = sessionId;
      mSessions.push_back(session);
    }
  }

  peerCallback->onSessionOpenRequest(session);
  return session.sessionId;
}

bool MessageRouter::closeSession(MessageHubId fromMessageHubId,
                                 SessionId sessionId, Reason reason) {
  return finalizeSession(fromMessageHubId, sessionId, reason);
}

bool MessageRouter::finalizeSession(MessageHubId fromMessageHubId,
                                    SessionId sessionId,
                                    std::optional<Reason> reason) {
  MessageRouter::MessageHubCallback *peerCallback = nullptr;
  MessageRouter::MessageHubCallback *initiatorCallback = nullptr;
  Session session;
  {
    LockGuard<Mutex> lock(mMutex);
    std::optional<size_t> index =
        findSessionIndexLocked(fromMessageHubId, sessionId);
    if (!index.has_value()) {
      LOGE("Failed to %s session with ID %" PRIu16 " not found",
           reason.has_value() ? "close" : "open", sessionId);
      return false;
    }

    session = mSessions[*index];
    if (reason.has_value()) {
      mSessions.erase(&mSessions[*index]);
    } else {
      mSessions[*index].isActive = true;
      session.isActive = true;
    }

    initiatorCallback =
        getCallbackFromMessageHubIdLocked(session.initiator.messageHubId);
    peerCallback = getCallbackFromMessageHubIdLocked(session.peer.messageHubId);

    if (initiatorCallback == nullptr || peerCallback == nullptr) {
      LOGE("Failed to finalize session: %s message hub with ID %" PRIu64
           " not found",
           initiatorCallback == nullptr ? "initiator" : "peer",
           initiatorCallback == nullptr ? session.initiator.messageHubId
                                        : session.peer.messageHubId);
      if (!reason.has_value()) {
        // Only erase if it was not erased above
        mSessions.erase(&mSessions[*index]);
      }
      return false;
    }
  }

  if (reason.has_value()) {
    initiatorCallback->onSessionClosed(session, reason.value());
    peerCallback->onSessionClosed(session, reason.value());
  } else {
    initiatorCallback->onSessionOpened(session);
    peerCallback->onSessionOpened(session);
  }
  return true;
}

std::optional<Session> MessageRouter::getSessionWithId(
    MessageHubId fromMessageHubId, SessionId sessionId) {
  LockGuard<Mutex> lock(mMutex);

  std::optional<size_t> index =
      findSessionIndexLocked(fromMessageHubId, sessionId);
  return index.has_value() ? std::optional<Session>(mSessions[*index])
                           : std::nullopt;
}

bool MessageRouter::sendMessage(pw::UniquePtr<std::byte[]> &&data,
                                uint32_t messageType,
                                uint32_t messagePermissions,
                                SessionId sessionId, EndpointId fromEndpointId,
                                MessageHubId fromMessageHubId) {
  MessageRouter::MessageHubCallback *receiverCallback = nullptr;
  Session session;
  bool sentBySessionInitiator;
  {
    LockGuard<Mutex> lock(mMutex);

    std::optional<size_t> index =
        findSessionIndexLocked(fromMessageHubId, sessionId);
    if (!index.has_value()) {
      LOGE("Failed to send message: session with ID %" PRIu16 " not found",
           sessionId);
      return false;
    }

    session = mSessions[*index];
    if (!session.isActive) {
      LOGE("Failed to send message: session with ID %" PRIu16 " is inactive",
           sessionId);
      return false;
    }

    Endpoint sender(fromMessageHubId, fromEndpointId);
    if (fromEndpointId == ENDPOINT_ID_ANY) {
      if (session.initiator.messageHubId == session.peer.messageHubId) {
        LOGE("Unable to infer sender endpoint ID: session with ID %" PRIu16
             " is between endpoints on the same message hub with ID %" PRIu64,
             sessionId, fromMessageHubId);
        return false;
      }
      sender.endpointId = session.initiator.messageHubId == fromMessageHubId
                              ? session.initiator.endpointId
                              : session.peer.endpointId;
    }

    if (sender != session.initiator && sender != session.peer) {
      LOGE("Failed to send message: session with ID %" PRIu16
           " does not contain endpoint with hub ID %" PRIu64
           " and endpoint ID %" PRIu64,
           sessionId, fromMessageHubId, fromEndpointId);
      return false;
    }
    sentBySessionInitiator = sender == session.initiator;
    receiverCallback = getCallbackFromMessageHubIdLocked(
        sentBySessionInitiator ? session.peer.messageHubId
                               : session.initiator.messageHubId);
  }

  bool success = false;
  if (receiverCallback != nullptr) {
    success = receiverCallback->onMessageReceived(std::move(data), messageType,
                                                  messagePermissions, session,
                                                  sentBySessionInitiator);
  }

  if (!success) {
    closeSession(fromMessageHubId, sessionId, Reason::UNSPECIFIED);
  }
  return success;
}

bool MessageRouter::registerEndpoint(MessageHubId messageHubId,
                                     EndpointId endpointId) {
  return onEndpointRegistrationStateChanged(messageHubId, endpointId,
                                            /* isRegistered = */ true);
}

bool MessageRouter::unregisterEndpoint(MessageHubId messageHubId,
                                       EndpointId endpointId) {
  return onEndpointRegistrationStateChanged(messageHubId, endpointId,
                                            /* isRegistered = */ false);
}

bool MessageRouter::onEndpointRegistrationStateChanged(
    MessageHubId messageHubId, EndpointId endpointId, bool isRegistered) {
  MessageRouter::MessageHubCallback *callback =
      getCallbackFromMessageHubId(messageHubId);
  if (callback == nullptr) {
    LOGE("Failed to register endpoint with ID %" PRIu64
         " to message hub with ID %" PRIu64 ": hub not found",
         endpointId, messageHubId);
    return false;
  }

  std::optional<DynamicVector<MessageHubRecord>> messageHubRecords =
      getMessageHubRecords();
  if (!messageHubRecords.has_value()) {
    return false;
  }

  for (const MessageHubRecord &messageHubRecord : *messageHubRecords) {
    if (messageHubRecord.info.id == messageHubId) {
      continue;
    }

    if (isRegistered) {
      messageHubRecord.callback->onEndpointRegistered(messageHubId, endpointId);
    } else {
      messageHubRecord.callback->onEndpointUnregistered(messageHubId,
                                                        endpointId);
    }
  }

  return true;
}

std::optional<DynamicVector<MessageRouter::MessageHubRecord>>
MessageRouter::getMessageHubRecords() {
  LockGuard<Mutex> lock(mMutex);
  DynamicVector<MessageHubRecord> messageHubRecords;
  if (!messageHubRecords.reserve(mMessageHubs.size())) {
    LOG_OOM();
    return std::nullopt;
  }

  for (const MessageHubRecord &messageHubRecord : mMessageHubs) {
    // Will not fail because we reserved space above
    messageHubRecords.push_back(messageHubRecord);
  }
  return messageHubRecords;
}

const MessageRouter::MessageHubRecord *MessageRouter::getMessageHubRecordLocked(
    MessageHubId messageHubId) {
  for (MessageHubRecord &messageHubRecord : mMessageHubs) {
    if (messageHubRecord.info.id == messageHubId) {
      return &messageHubRecord;
    }
  }
  return nullptr;
}

std::optional<size_t> MessageRouter::findSessionIndexLocked(
    MessageHubId fromMessageHubId, SessionId sessionId) {
  for (size_t i = 0; i < mSessions.size(); ++i) {
    if (mSessions[i].sessionId == sessionId) {
      if (mSessions[i].initiator.messageHubId == fromMessageHubId ||
          mSessions[i].peer.messageHubId == fromMessageHubId) {
        return i;
      }

      LOGE("Hub mismatch for session with ID %" PRIu16
           ": requesting hub ID %" PRIu64
           " but session is between hubs %" PRIu64 " and %" PRIu64,
           sessionId, fromMessageHubId, mSessions[i].initiator.messageHubId,
           mSessions[i].peer.messageHubId);
      break;
    }
  }
  return std::nullopt;
}

MessageRouter::MessageHubCallback *MessageRouter::getCallbackFromMessageHubId(
    MessageHubId messageHubId) {
  LockGuard<Mutex> lock(mMutex);
  return getCallbackFromMessageHubIdLocked(messageHubId);
}

MessageRouter::MessageHubCallback *
MessageRouter::getCallbackFromMessageHubIdLocked(MessageHubId messageHubId) {
  const MessageHubRecord *messageHubRecord =
      getMessageHubRecordLocked(messageHubId);
  return messageHubRecord == nullptr ? nullptr : messageHubRecord->callback;
}

bool MessageRouter::checkIfEndpointExists(
    MessageRouter::MessageHubCallback *callback, EndpointId endpointId) {
  struct EndpointContext {
    EndpointId endpointId;
    bool foundEndpoint = false;
  };
  EndpointContext context = {
      .endpointId = endpointId,
  };

  callback->forEachEndpoint([&context](const EndpointInfo &endpointInfo) {
    if (context.endpointId == endpointInfo.id) {
      context.foundEndpoint = true;
      return true;
    }
    return false;
  });
  return context.foundEndpoint;
}

SessionId MessageRouter::getNextSessionIdLocked() {
  constexpr size_t kMaxIterations = 10;

  if (mNextSessionId >= kReservedSessionId) {
    mNextSessionId = 0;
  }

  bool foundSessionIdConflict;
  size_t iterations = 0;
  do {
    foundSessionIdConflict = false;
    for (const Session &session : mSessions) {
      if (session.sessionId == mNextSessionId) {
        ++mNextSessionId;
        if (mNextSessionId >= kReservedSessionId) {
          mNextSessionId = 0;
        }
        foundSessionIdConflict = true;
        break;
      }
    }
    ++iterations;
  } while (foundSessionIdConflict && iterations < kMaxIterations);

  return foundSessionIdConflict ? SESSION_ID_INVALID : mNextSessionId++;
}

}  // namespace chre::message
