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

#include "chre/platform/assert.h"
#include "chre/platform/log.h"
#include "chre/util/lock_guard.h"
#include "chre/util/system/message_common.h"
#include "chre/util/system/message_router.h"

namespace chre::message {

MessageRouter::MessageHub::MessageHub(MessageRouter &router, MessageHubId id)
    : mRouter(router), kHubId(id) {}

SessionId MessageRouter::MessageHub::openSession(EndpointId fromEndpointId,
                                                 MessageHubId toMessageHubId,
                                                 EndpointId toEndpointId) {
  return mRouter.openSession(kHubId, fromEndpointId, toMessageHubId,
                             toEndpointId);
}

bool MessageRouter::MessageHub::closeSession(SessionId sessionId) {
  return mRouter.closeSession(kHubId, sessionId);
}

std::optional<Session> MessageRouter::MessageHub::getSessionWithId(
    SessionId sessionId) {
  return mRouter.getSessionWithId(kHubId, sessionId);
}

bool MessageRouter::MessageHub::sendMessage(pw::UniquePtr<std::byte[]> &&data,
                                            size_t length, uint32_t messageType,
                                            uint32_t messagePermissions,
                                            SessionId sessionId) {
  return mRouter.sendMessage(std::move(data), length, messageType,
                             messagePermissions, sessionId, kHubId);
}

MessageHubId MessageRouter::MessageHub::getId() {
  return kHubId;
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
      .callback = callback,
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

void MessageRouter::forEachMessageHub(
    const pw::Function<bool(const MessageHubInfo &)> &function) {
  LockGuard<Mutex> lock(mMutex);
  for (MessageHubRecord &messageHubRecord : mMessageHubs) {
    function(messageHubRecord.info);
  }
}

SessionId MessageRouter::openSession(MessageHubId fromMessageHubId,
                                     EndpointId fromEndpointId,
                                     MessageHubId toMessageHubId,
                                     EndpointId toEndpointId) {
  if (fromMessageHubId == toMessageHubId) {
    LOGE(
        "Failed to open session: initiator and peer message hubs are the same");
    return SESSION_ID_INVALID;
  }

  MessageRouter::MessageHubCallback *initiatorCallback =
      getCallbackFromMessageHubId(fromMessageHubId);
  MessageRouter::MessageHubCallback *peerCallback =
      getCallbackFromMessageHubId(toMessageHubId);
  if (initiatorCallback == nullptr || peerCallback == nullptr) {
    LOGE("Failed to open session: initiator or peer message hub not found");
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

  {
    LockGuard<Mutex> lock(mMutex);
    if (mSessions.full()) {
      LOGE("Failed to open session: maximum number of sessions reached");
      return SESSION_ID_INVALID;
    }

    Session insertSession = {
        .sessionId = mNextSessionId,
        .initiator = {.messageHubId = fromMessageHubId,
                      .endpointId = fromEndpointId},
        .peer = {.messageHubId = toMessageHubId, .endpointId = toEndpointId},
    };

    for (Session &session : mSessions) {
      if (session.isEquivalent(insertSession)) {
        LOGD("Session with ID %" PRIu16 " already exists", session.sessionId);
        return session.sessionId;
      }
    }

    mSessions.push_back(std::move(insertSession));
    return mNextSessionId++;
  }
}

bool MessageRouter::closeSession(MessageHubId fromMessageHubId,
                                 SessionId sessionId) {
  Session session;
  MessageRouter::MessageHubCallback *initiatorCallback = nullptr;
  MessageRouter::MessageHubCallback *peerCallback = nullptr;
  {
    LockGuard<Mutex> lock(mMutex);

    std::optional<size_t> index =
        findSessionIndexLocked(fromMessageHubId, sessionId);
    if (!index.has_value()) {
      LOGE("Failed to close session with ID %" PRIu16 ": session not found",
           sessionId);
      return false;
    }

    session = mSessions[*index];
    initiatorCallback =
        getCallbackFromMessageHubIdLocked(session.initiator.messageHubId);
    peerCallback = getCallbackFromMessageHubIdLocked(session.peer.messageHubId);
    mSessions.erase(&mSessions[*index]);
  }

  CHRE_ASSERT(initiatorCallback != nullptr && peerCallback != nullptr);
  initiatorCallback->onSessionClosed(session);
  peerCallback->onSessionClosed(session);
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
                                size_t length, uint32_t messageType,
                                uint32_t messagePermissions,
                                SessionId sessionId,
                                MessageHubId fromMessageHubId) {
  MessageRouter::MessageHubCallback *receiverCallback = nullptr;
  Session session;
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
    receiverCallback = getCallbackFromMessageHubIdLocked(
        session.initiator.messageHubId == fromMessageHubId
            ? session.peer.messageHubId
            : session.initiator.messageHubId);
  }

  CHRE_ASSERT(receiverCallback != nullptr);
  bool success = receiverCallback->onMessageReceived(
      std::move(data), length, messageType, messagePermissions, session,
      session.initiator.messageHubId == fromMessageHubId);

  if (!success) {
    closeSession(fromMessageHubId, sessionId);
  }
  return success;
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
  return messageHubRecord != nullptr ? &messageHubRecord->callback : nullptr;
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

}  // namespace chre::message
