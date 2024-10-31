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

#ifndef CHRE_UTIL_SYSTEM_MESSAGE_COMMON_H_
#define CHRE_UTIL_SYSTEM_MESSAGE_COMMON_H_

#include <pw_allocator/unique_ptr.h>
#include <pw_function/function.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace chre::message {

//! The ID of a MessageHub
using MessageHubId = uint64_t;

//! The ID of an endpoint
using EndpointId = uint64_t;

//! The ID of a session
using SessionId = uint16_t;

//! An invalid session ID
constexpr static SessionId SESSION_ID_INVALID = UINT16_MAX;

//! An invalid MessageHub ID
constexpr static MessageHubId MESSAGE_HUB_ID_INVALID = UINT64_MAX;

//! Endpoint types
enum class EndpointType : uint8_t {
  NANOAPP = 0,
  GENERIC = 1,
  HOST_ENDPOINT = 2,
};

//! Represents a single endpoint connected to a MessageHub
struct Endpoint {
  MessageHubId messageHubId;
  EndpointId endpointId;

  bool operator==(const Endpoint &other) const {
    return messageHubId == other.messageHubId && endpointId == other.endpointId;
  }

  bool operator!=(const Endpoint &other) const {
    return !(*this == other);
  }
};

//! Represents a session between two endpoints
struct Session {
  SessionId sessionId;
  Endpoint initiator;
  Endpoint peer;

  bool operator==(const Session &other) const {
    return sessionId == other.sessionId && initiator == other.initiator &&
           peer == other.peer;
  }

  bool operator!=(const Session &other) const {
    return !(*this == other);
  }

  bool isEquivalent(const Session &other) const {
    return (initiator == other.initiator && peer == other.peer) ||
           (initiator == other.peer && peer == other.initiator);
  }
};

//! Represents a message sent using the MessageRouter
struct Message {
  Endpoint sender;
  Endpoint recipient;
  SessionId sessionId;
  pw::UniquePtr<std::byte[]> data;
  size_t length;
  uint32_t messageType;
  uint32_t messagePermissions;

  Message()
      : sessionId(SESSION_ID_INVALID),
        data(nullptr),
        length(0),
        messageType(0),
        messagePermissions(0) {}
  Message(pw::UniquePtr<std::byte[]> &&data, size_t length,
          uint32_t messageType, uint32_t messagePermissions, Session session,
          bool sentBySessionInitiator)
      : sender(sentBySessionInitiator ? session.initiator : session.peer),
        recipient(sentBySessionInitiator ? session.peer : session.initiator),
        sessionId(session.sessionId),
        data(std::move(data)),
        length(length),
        messageType(messageType),
        messagePermissions(messagePermissions) {}
  Message(Message &&other)
      : sender(other.sender),
        recipient(other.recipient),
        sessionId(other.sessionId),
        data(std::move(other.data)),
        length(other.length),
        messageType(other.messageType),
        messagePermissions(other.messagePermissions) {}

  Message(const Message &) = delete;
  Message &operator=(const Message &) = delete;

  Message &operator=(Message &&other) {
    sender = other.sender;
    recipient = other.recipient;
    sessionId = other.sessionId;
    data = std::move(other.data);
    length = other.length;
    messageType = other.messageType;
    messagePermissions = other.messagePermissions;
    return *this;
  }
};

//! Represents information about an endpoint
//! Service information is stored in ServiceManager
struct EndpointInfo {
  EndpointId id;
  const char *name;
  uint32_t version;
  EndpointType type;
  uint32_t requiredPermissions;

  bool operator==(const EndpointInfo &other) const {
    return id == other.id && version == other.version && type == other.type &&
           requiredPermissions == other.requiredPermissions &&
           std::strcmp(name, other.name) == 0;
  }

  bool operator!=(const EndpointInfo &other) const {
    return !(*this == other);
  }
};

//! Represents information about a MessageHub
struct MessageHubInfo {
  MessageHubId id;
  const char *name;

  bool operator==(const MessageHubInfo &other) const {
    return id == other.id && std::strcmp(name, other.name) == 0;
  }

  bool operator!=(const MessageHubInfo &other) const {
    return !(*this == other);
  }
};

}  // namespace chre::message

#endif  // CHRE_UTIL_SYSTEM_MESSAGE_COMMON_H_
