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

#include "pw_allocator/unique_ptr.h"

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

//! An invalid MessageHub ID
constexpr MessageHubId MESSAGE_HUB_ID_INVALID = 0;

//! A MessageHub ID that matches any MessageHub
constexpr MessageHubId MESSAGE_HUB_ID_ANY = MESSAGE_HUB_ID_INVALID;

//! An invalid endpoint ID
constexpr EndpointId ENDPOINT_ID_INVALID = 0;

//! An endpoint ID that matches any endpoint
constexpr EndpointId ENDPOINT_ID_ANY = ENDPOINT_ID_INVALID;

//! An invalid session ID
constexpr SessionId SESSION_ID_INVALID = UINT16_MAX;

//! Endpoint types
enum class EndpointType : uint8_t {
  HOST_FRAMEWORK = 1,
  HOST_APP = 2,
  HOST_NATIVE = 3,
  NANOAPP = 4,
  GENERIC = 5,
};

//! Endpoint permissions
//! This should match the CHRE_MESSAGE_PERMISSION_* constants.
enum class EndpointPermission : uint32_t {
  NONE = 0,
  AUDIO = 1,
  GNSS = 1 << 1,
  WIFI = 1 << 2,
  WWAN = 1 << 3,
  BLE = 1 << 4,
};

//! The reason for closing a session
enum class Reason : uint8_t {
  UNSPECIFIED = 0,
  OUT_OF_MEMORY,
  TIMEOUT,
  OPEN_ENDPOINT_SESSION_REQUEST_REJECTED,
  CLOSE_ENDPOINT_SESSION_REQUESTED,
  ENDPOINT_INVALID,
  ENDPOINT_GONE,
  ENDPOINT_CRASHED,
  HUB_RESET,
  PERMISSION_DENIED,
};

//! The format of an RPC message sent using a service
enum class RpcFormat : uint8_t {
  CUSTOM = 0,
  AIDL,
  PW_RPC_PROTOBUF,
};

//! Represents a single endpoint connected to a MessageHub
struct Endpoint {
  MessageHubId messageHubId;
  EndpointId endpointId;

  Endpoint()
      : messageHubId(MESSAGE_HUB_ID_INVALID), endpointId(ENDPOINT_ID_INVALID) {}

  Endpoint(MessageHubId messageHubId, EndpointId endpointId)
      : messageHubId(messageHubId),
        endpointId(endpointId) {}

  bool operator==(const Endpoint &other) const {
    return messageHubId == other.messageHubId && endpointId == other.endpointId;
  }

  bool operator!=(const Endpoint &other) const {
    return !(*this == other);
  }
};

//! Represents a session between two endpoints
struct Session {
  static constexpr size_t kMaxServiceDescriptorLength = 127;

  Session()
      : sessionId(SESSION_ID_INVALID),
        isActive(false),
        hasServiceDescriptor(false) {
    serviceDescriptor[0] = '\0';
  }

  Session(SessionId sessionId, Endpoint initiator, Endpoint peer,
          const char *serviceDescriptor)
      : sessionId(sessionId),
        isActive(false),
        hasServiceDescriptor(serviceDescriptor != nullptr),
        initiator(initiator),
        peer(peer) {
    if (serviceDescriptor != nullptr) {
      std::strncpy(this->serviceDescriptor, serviceDescriptor,
                   kMaxServiceDescriptorLength);
    } else {
      this->serviceDescriptor[0] = '\0';
    }
    this->serviceDescriptor[kMaxServiceDescriptorLength] = '\0';
  }

  SessionId sessionId;
  bool isActive;
  bool hasServiceDescriptor;
  Endpoint initiator;
  Endpoint peer;
  char serviceDescriptor[kMaxServiceDescriptorLength + 1];

  bool operator==(const Session &other) const {
    return sessionId == other.sessionId && initiator == other.initiator &&
           peer == other.peer && isActive == other.isActive &&
           hasServiceDescriptor == other.hasServiceDescriptor &&
           (!hasServiceDescriptor ||
            std::strncmp(serviceDescriptor, other.serviceDescriptor,
                         kMaxServiceDescriptorLength) == 0);
  }

  bool operator!=(const Session &other) const {
    return !(*this == other);
  }

  //! @return true if the two sessions are equivalent, i.e. they have the same
  //! endpoints and service descriptor (if present), false otherwise
  bool isEquivalent(const Session &other) const {
    bool sameEndpoints = (initiator == other.initiator && peer == other.peer) ||
                         (initiator == other.peer && peer == other.initiator);
    return hasServiceDescriptor == other.hasServiceDescriptor &&
           sameEndpoints &&
           (!hasServiceDescriptor ||
            std::strncmp(serviceDescriptor, other.serviceDescriptor,
                         kMaxServiceDescriptorLength) == 0);
  }
};

//! Represents a message sent using the MessageRouter
struct Message {
  Endpoint sender;
  Endpoint recipient;
  SessionId sessionId;
  pw::UniquePtr<std::byte[]> data;
  uint32_t messageType;
  uint32_t messagePermissions;

  Message()
      : sessionId(SESSION_ID_INVALID),
        data(nullptr),
        messageType(0),
        messagePermissions(0) {}

  Message(pw::UniquePtr<std::byte[]> &&data,
          uint32_t messageType, uint32_t messagePermissions, Session session,
          bool sentBySessionInitiator)
      : sender(sentBySessionInitiator ? session.initiator : session.peer),
        recipient(sentBySessionInitiator ? session.peer : session.initiator),
        sessionId(session.sessionId),
        data(std::move(data)),
        messageType(messageType),
        messagePermissions(messagePermissions) {}

  Message(const Message &) = delete;
  Message &operator=(const Message &) = delete;

  Message(Message &&other)
      : sender(other.sender),
        recipient(other.recipient),
        sessionId(other.sessionId),
        data(std::move(other.data)),
        messageType(other.messageType),
        messagePermissions(other.messagePermissions) {}

  Message &operator=(Message &&other) {
    sender = other.sender;
    recipient = other.recipient;
    sessionId = other.sessionId;
    data = std::move(other.data);
    messageType = other.messageType;
    messagePermissions = other.messagePermissions;
    return *this;
  }
};

//! Represents information about an endpoint
struct EndpointInfo {
  static constexpr size_t kMaxNameLength = 50;

  EndpointInfo(EndpointId id, const char *name, uint32_t version,
               EndpointType type, uint32_t requiredPermissions)
      : id(id),
        version(version),
        type(type),
        requiredPermissions(requiredPermissions) {
    if (name != nullptr) {
      std::strncpy(this->name, name, kMaxNameLength);
    } else {
      this->name[0] = '\0';
    }
    this->name[kMaxNameLength] = '\0';
  }

  EndpointId id;
  char name[kMaxNameLength + 1];
  uint32_t version;
  EndpointType type;
  uint32_t requiredPermissions;

  bool operator==(const EndpointInfo &other) const {
    return id == other.id && version == other.version && type == other.type &&
           requiredPermissions == other.requiredPermissions &&
           std::strncmp(name, other.name, kMaxNameLength) == 0;
  }

  bool operator!=(const EndpointInfo &other) const {
    return !(*this == other);
  }
};

//! Represents information about a service provided by an endpoint.
struct ServiceInfo {
  ServiceInfo(const char *serviceDescriptor, uint32_t majorVersion,
              uint32_t minorVersion, RpcFormat format)
      : serviceDescriptor(serviceDescriptor),
        majorVersion(majorVersion),
        minorVersion(minorVersion),
        format(format) {}

  bool operator==(const ServiceInfo &other) const {
    if (majorVersion != other.majorVersion ||
        minorVersion != other.minorVersion || format != other.format) {
      return false;
    }

    if ((serviceDescriptor == nullptr) !=
        (other.serviceDescriptor == nullptr)) {
      return false;
    }
    if (serviceDescriptor != nullptr &&
        std::strcmp(serviceDescriptor, other.serviceDescriptor) != 0) {
      return false;
    }
    return true;
  }

  bool operator!=(const ServiceInfo &other) const {
    return !(*this == other);
  }

  //! The service descriptor, a null-terminated ASCII string. This must be valid
  //! only for the lifetime of the service iteration methods in MessageRouter.
  const char *serviceDescriptor;

  //! Version of the service.
  uint32_t majorVersion;
  uint32_t minorVersion;

  //! The format of the RPC messages sent using this service.
  RpcFormat format;
};

//! Represents information about a MessageHub
struct MessageHubInfo {
  MessageHubId id;
  const char *name;

  bool operator==(const MessageHubInfo &other) const {
    if (id != other.id) {
      return false;
    }

    if ((name == nullptr) != (other.name == nullptr)) {
      return false;
    }
    if (name != nullptr && std::strcmp(name, other.name) != 0) {
      return false;
    }
    return true;
  }

  bool operator!=(const MessageHubInfo &other) const {
    return !(*this == other);
  }
};

}  // namespace chre::message

#endif  // CHRE_UTIL_SYSTEM_MESSAGE_COMMON_H_
