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

#include "chre/core/host_message_hub_manager.h"

#ifdef CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED

#include <cstring>
#include <optional>

#include "chre/core/event_loop_manager.h"
#include "chre/platform/assert.h"
#include "chre/platform/log.h"
#include "chre/platform/memory.h"
#include "chre/platform/mutex.h"
#include "chre/platform/shared/generated/host_messages_generated.h"
#include "chre/platform/shared/host_protocol_chre.h"
#include "chre/util/lock_guard.h"
#include "chre/util/system/message_common.h"
#include "pw_allocator/unique_ptr.h"
#include "pw_function/function.h"
#include "pw_span/span.h"

namespace chre {

using ::chre::message::EndpointId;
using ::chre::message::EndpointInfo;
using ::chre::message::MessageHubId;
using ::chre::message::MessageHubInfo;
using ::chre::message::MessageRouter;
using ::chre::message::MessageRouterSingleton;
using ::chre::message::Reason;
using ::chre::message::Session;
using ::chre::message::SessionId;

void HostMessageHubManager::onHostTransportReady(HostCallback &cb) {
  CHRE_ASSERT_LOG(mCb == nullptr,
                  "HostMessageHubManager::init() called more than once");
  mCb = &cb;
}

void HostMessageHubManager::reset() {
  LOGI("Resetting HostMessageHubManager");
  CHRE_ASSERT_NOT_NULL(mCb);
  LockGuard<Mutex> hostLock(mHubsLock);

  // Deactivate all existing message hubs.
  for (auto &hub : mHubs) hub.clear();

  // Serialize the following against any other embedded hub or endpoint
  // registration events.
  LockGuard<Mutex> embeddedLock(mEmbeddedHubOpLock);

  // Notify the HAL to accept embedded hub/endpoint registrations.
  mCb->onReset();
  MessageRouterSingleton::get()->forEachMessageHub(
      [this](const MessageHubInfo &info) {
        for (auto &hub : mHubs) {
          if (hub.getMessageHub().getId() == info.id) return false;
        }
        mCb->onHubRegistered(info);
        return false;
      });
  MessageRouterSingleton::get()->forEachEndpoint(
      [this](const MessageHubInfo &hub, const EndpointInfo &endpoint) {
        mCb->onEndpointRegistered(hub.id, endpoint);
      });
  LOGI("Initialized HostMessageHubManager");
}

void HostMessageHubManager::registerHub(const MessageHubInfo &info) {
  LockGuard<Mutex> lock(mHubsLock);
  pw::IntrusiveList<Endpoint> endpoints;
  HostMessageHubManager::Hub::restoreOrCreateLocked(info, endpoints);
}

void HostMessageHubManager::unregisterHub(MessageHubId id) {
  LockGuard<Mutex> lock(mHubsLock);
  for (auto &hub : mHubs) {
    if (hub.getMessageHub().getId() != id) continue;
    hub.clear();
    return;
  }
  LOGE("No host hub 0x%" PRIx64 " for unregister", id);
}

void HostMessageHubManager::registerEndpoint(MessageHubId hubId,
                                             const EndpointInfo &info) {
  LockGuard<Mutex> lock(mHubsLock);
  for (auto &hub : mHubs) {
    if (hub.getMessageHub().getId() != hubId) continue;
    hub.addEndpoint(info);
    return;
  }
  LOGE("No host hub 0x%" PRIx64 " for add endpoint", hubId);
}

void HostMessageHubManager::unregisterEndpoint(MessageHubId hubId,
                                               EndpointId id) {
  LockGuard<Mutex> lock(mHubsLock);
  for (auto &hub : mHubs) {
    if (hub.getMessageHub().getId() != hubId) continue;
    hub.removeEndpoint(id);
    return;
  }
  LOGE("No host hub 0x%" PRIx64 " for unregister endpoint", hubId);
}

void HostMessageHubManager::openSession(MessageHubId hubId,
                                        EndpointId endpointId,
                                        MessageHubId destinationHubId,
                                        EndpointId destinationEndpointId,
                                        SessionId sessionId,
                                        const char *serviceDescriptor) {
  LockGuard<Mutex> lock(mHubsLock);
  for (auto &hub : mHubs) {
    if (hub.getMessageHub().getId() != hubId) continue;
    if (hub.getMessageHub().openSession(
            endpointId, destinationHubId, destinationEndpointId,
            serviceDescriptor, sessionId) != sessionId) {
      mCb->onSessionClosed(hubId, sessionId,
                           Reason::OPEN_ENDPOINT_SESSION_REQUEST_REJECTED);
    }
    return;
  }
  LOGE("No host hub 0x%" PRIx64 " for open session", hubId);
}

void HostMessageHubManager::ackSession(MessageHubId hubId,
                                       SessionId sessionId) {
  LockGuard<Mutex> lock(mHubsLock);
  for (auto &hub : mHubs) {
    if (hub.getMessageHub().getId() != hubId) continue;
    hub.getMessageHub().onSessionOpenComplete(sessionId);
    mCb->onSessionOpened(hubId, sessionId);
    return;
  }
  LOGE("No host hub 0x%" PRIx64 " for ack session", hubId);
}

void HostMessageHubManager::closeSession(MessageHubId hubId,
                                         SessionId sessionId, Reason reason) {
  LockGuard<Mutex> lock(mHubsLock);
  for (auto &hub : mHubs) {
    if (hub.getMessageHub().getId() != hubId) continue;
    hub.getMessageHub().closeSession(sessionId, reason);
    return;
  }
  LOGE("No host hub 0x%" PRIx64 " for close session", hubId);
}

void HostMessageHubManager::sendMessage(MessageHubId hubId, SessionId sessionId,
                                        pw::span<const std::byte> data,
                                        uint32_t type, uint32_t permissions) {
  LockGuard<Mutex> lock(mHubsLock);
  for (auto &hub : mHubs) {
    if (hub.getMessageHub().getId() != hubId) continue;
    auto dataCopy = mMsgAllocator.MakeUniqueArray<std::byte>(data.size());
    if (dataCopy == nullptr) {
      LOGE("Failed to allocate endpoint message from host hub 0x%" PRIx64
           " over session %" PRIu16,
           hubId, sessionId);
      return;
    }
    std::memcpy(dataCopy.get(), data.data(), data.size());

    // Note: We are assuming here that no host hubs will create sessions with
    // themselves as it is not allowed by the HAL API.
    hub.getMessageHub().sendMessage(std::move(dataCopy), type, permissions,
                                    sessionId);
    return;
  }
  LOGE("No host hub 0x%" PRIx64 " for send message", hubId);
}

namespace {

HostMessageHubManager &getManager() {
  return EventLoopManagerSingleton::get()->getHostMessageHubManager();
}

}  // namespace

bool HostMessageHubManager::Hub::restoreOrCreateLocked(
    const MessageHubInfo &info, pw::IntrusiveList<Endpoint> &endpoints) {
  // If the hub already exists, initialize its endpoint list and reactive it.
  for (auto &hub : getManager().mHubs) {
    if (hub.getMessageHub().getId() != info.id) continue;
    LOGI("Restoring host message hub 0x%" PRIx64, info.id);
    LockGuard<Mutex> lock(hub.mEndpointsLock);
    if (!hub.mEndpoints.empty()) {
      LOGE("Expected reserved hub slot to have no endpoints");
      deallocateEndpoints(hub.mEndpoints);
    }
    hub.mEndpoints.splice_after(hub.mEndpoints.before_begin(), endpoints);
    hub.mActive = true;
    return true;
  }

  // If there is an available slot, create a new Hub and try to register it with
  // MessageRouter, cleaning it up on failure.
  if (getManager().mHubs.full()) {
    LOGE("No space to register new host hub 0x%" PRIx64, info.id);
    deallocateEndpoints(endpoints);
    return false;
  }
  getManager().mHubs.emplace_back(info.name, endpoints);
  auto &hub = getManager().mHubs.back();
  std::optional<MessageRouter::MessageHub> maybeHub =
      MessageRouterSingleton::get()->registerMessageHub(hub.kName, info.id,
                                                        hub);
  if (!maybeHub) {
    LOGE("Failed to register host hub 0x%" PRIx64, info.id);
    getManager().mHubs.pop_back();
    return false;
  }
  LOGI("Registered host hub 0x%" PRIx64, info.id);
  hub.mMessageHub = std::move(*maybeHub);
  hub.mActive = true;
  return true;
}

HostMessageHubManager::Hub::Hub(const char *name,
                                pw::IntrusiveList<Endpoint> &endpoints) {
  std::strncpy(kName, name, kNameMaxLen);
  kName[kNameMaxLen] = 0;
  mEndpoints.splice_after(mEndpoints.before_begin(), endpoints);
}

HostMessageHubManager::Hub::~Hub() {
  clear();
}

void HostMessageHubManager::Hub::clear() {
  LockGuard<Mutex> lock(mEndpointsLock);
  mActive = false;
  deallocateEndpoints(mEndpoints);
}

void HostMessageHubManager::Hub::addEndpoint(const EndpointInfo &info) {
  {
    LockGuard<Mutex> lock(mEndpointsLock);
    auto *endpoint = getManager().mEndpointAllocator.allocate(info);
    if (!endpoint) {
      LOGE("Failed to allocate storage for endpoint (0x%" PRIx64 ", 0x%" PRIx64
           ")",
           mMessageHub.getId(), info.id);
      return;
    }
    mEndpoints.push_back(*endpoint);
  }
  mMessageHub.registerEndpoint(info.id);
}

void HostMessageHubManager::Hub::removeEndpoint(EndpointId id) {
  bool found = false;
  {
    LockGuard<Mutex> lock(mEndpointsLock);
    for (auto it = mEndpoints.begin(), eraseIt = mEndpoints.before_begin();
         it != mEndpoints.end(); ++it, ++eraseIt) {
      auto &endpoint = *it;
      if (endpoint.kInfo.id == id) {
        mEndpoints.erase_after(eraseIt);
        getManager().mEndpointAllocator.deallocate(&endpoint);
        found = true;
        break;
      }
    }
  }
  if (found) mMessageHub.unregisterEndpoint(id);
}

bool HostMessageHubManager::Hub::onMessageReceived(
    pw::UniquePtr<std::byte[]> &&data, uint32_t messageType,
    uint32_t messagePermissions, const Session &session,
    bool /*sentBySessionInitiator*/) {
  if (!mActive) {
    mMessageHub.closeSession(session.sessionId, Reason::HUB_RESET);
    return false;
  }
  return getManager().mCb->onMessageReceived(mMessageHub.getId(),
                                             session.sessionId, std::move(data),
                                             messageType, messagePermissions);
}

void HostMessageHubManager::Hub::onSessionOpenRequest(const Session &session) {
  if (!mActive) {
    mMessageHub.closeSession(session.sessionId, Reason::HUB_RESET);
    return;
  }
  return getManager().mCb->onSessionOpenRequest(session);
}

void HostMessageHubManager::Hub::onSessionOpened(const Session &session) {
  if (!mActive) {
    mMessageHub.closeSession(session.sessionId, Reason::HUB_RESET);
    return;
  }
  return getManager().mCb->onSessionOpened(mMessageHub.getId(),
                                           session.sessionId);
}

void HostMessageHubManager::Hub::onSessionClosed(const Session &session,
                                                 Reason reason) {
  if (!mActive) return;
  return getManager().mCb->onSessionClosed(mMessageHub.getId(),
                                           session.sessionId, reason);
}

void HostMessageHubManager::Hub::forEachEndpoint(
    const pw::Function<bool(const EndpointInfo &)> &function) {
  LockGuard<Mutex> lock(mEndpointsLock);
  for (const auto &endpoint : mEndpoints)
    if (function(endpoint.kInfo)) break;
}

std::optional<EndpointInfo> HostMessageHubManager::Hub::getEndpointInfo(
    EndpointId endpointId) {
  LockGuard<Mutex> lock(mEndpointsLock);
  for (const auto &endpoint : mEndpoints)
    if (endpoint.kInfo.id == endpointId) return endpoint.kInfo;
  return {};
}

std::optional<EndpointId> HostMessageHubManager::Hub::getEndpointForService(
    const char * /*serviceDescriptor*/) {
  // TODO(b/390447515): Add support for service descriptors
  return {};
}

bool HostMessageHubManager::Hub::doesEndpointHaveService(
    EndpointId /*endpointId*/, const char * /*serviceDescriptor*/) {
  // TODO(b/390447515): Add support for service descriptors
  return false;
}

void HostMessageHubManager::Hub::onHubRegistered(const MessageHubInfo &info) {
  LockGuard<Mutex> lock(getManager().mEmbeddedHubOpLock);
  getManager().mCb->onHubRegistered(info);
}

void HostMessageHubManager::Hub::onHubUnregistered(MessageHubId id) {
  LockGuard<Mutex> lock(getManager().mEmbeddedHubOpLock);
  getManager().mCb->onHubUnregistered(id);
}

void HostMessageHubManager::Hub::onEndpointRegistered(MessageHubId messageHubId,
                                                      EndpointId endpointId) {
  std::optional<EndpointInfo> endpoint =
      MessageRouterSingleton::get()->getEndpointInfo(messageHubId, endpointId);
  if (!endpoint) return;
  LockGuard<Mutex> lock(getManager().mEmbeddedHubOpLock);
  getManager().mCb->onEndpointRegistered(messageHubId, *endpoint);
}

void HostMessageHubManager::Hub::onEndpointUnregistered(
    MessageHubId messageHubId, EndpointId endpointId) {
  LockGuard<Mutex> lock(getManager().mEmbeddedHubOpLock);
  getManager().mCb->onEndpointUnregistered(messageHubId, endpointId);
}

void HostMessageHubManager::deallocateEndpoints(
    pw::IntrusiveList<Endpoint> &endpoints) {
  auto &manager = getManager();
  while (!endpoints.empty()) {
    auto &endpoint = endpoints.front();
    endpoints.pop_front();
    manager.mEndpointAllocator.deallocate(&endpoint);
  }
}

void *HostMessageHubManager::ChreAllocator::DoAllocate(Layout layout) {
  return memoryAlloc(layout.size());
}

void HostMessageHubManager::ChreAllocator::DoDeallocate(void *ptr) {
  memoryFree(ptr);
}

}  // namespace chre

#endif  // CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED
