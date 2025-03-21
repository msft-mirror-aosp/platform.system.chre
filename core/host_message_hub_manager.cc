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
#include "chre/target_platform/log.h"

#ifdef CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED

#include <cstring>
#include <optional>

#include "chre/core/event_loop_manager.h"
#include "chre/platform/assert.h"
#include "chre/platform/log.h"
#include "chre/platform/memory.h"
#include "chre/platform/mutex.h"
#include "chre/platform/shared/fbs/host_messages_generated.h"
#include "chre/platform/shared/host_protocol_chre.h"
#include "chre/util/lock_guard.h"
#include "chre/util/memory.h"
#include "chre/util/system/message_common.h"
#include "chre/util/system/message_router.h"

#include "pw_allocator/unique_ptr.h"
#include "pw_function/function.h"
#include "pw_intrusive_ptr/intrusive_ptr.h"
#include "pw_span/span.h"

namespace chre {

using ::chre::message::EndpointId;
using ::chre::message::EndpointInfo;
using ::chre::message::MESSAGE_HUB_ID_INVALID;
using ::chre::message::MessageHubId;
using ::chre::message::MessageHubInfo;
using ::chre::message::MessageRouter;
using ::chre::message::MessageRouterSingleton;
using ::chre::message::Reason;
using ::chre::message::ServiceInfo;
using ::chre::message::Session;
using ::chre::message::SessionId;

HostMessageHubManager::~HostMessageHubManager() {
  LockGuard<Mutex> hostLock(mHubsLock);
  clearHubsLocked();
}

void HostMessageHubManager::onHostTransportReady(HostCallback &cb) {
  CHRE_ASSERT_LOG(mCb == nullptr,
                  "HostMessageHubManager::init() called more than once");
  mCb = &cb;
}

void HostMessageHubManager::reset() {
  LOGI("Resetting HostMessageHubManager");
  CHRE_ASSERT_NOT_NULL(mCb);
  LockGuard<Mutex> hostLock(mHubsLock);
  clearHubsLocked();

  // Serialize the following against any other embedded hub or endpoint
  // registration events.
  LockGuard<Mutex> embeddedLock(mEmbeddedHubOpLock);

  // Notify the HAL to accept embedded hub/endpoint registrations.
  mCb->onReset();
  MessageRouterSingleton::get()->forEachMessageHub(
      [this](const MessageHubInfo &info) {
        mCb->onHubRegistered(info);
        return false;
      });
  MessageRouterSingleton::get()->forEachEndpoint(
      [this](const MessageHubInfo &hub, const EndpointInfo &endpoint) {
        mCb->onEndpointRegistered(hub.id, endpoint);
      });
  MessageRouterSingleton::get()->forEachService(
      [this](const MessageHubInfo &hub, const EndpointInfo &endpoint,
             const ServiceInfo &service) {
        mCb->onEndpointService(hub.id, endpoint.id, service);
        return false;
      });
  MessageRouterSingleton::get()->forEachEndpoint(
      [this](const MessageHubInfo &hub, const EndpointInfo &endpoint) {
        mCb->onEndpointReady(hub.id, endpoint.id);
      });
  LOGI("Initialized HostMessageHubManager");
}

void HostMessageHubManager::registerHub(const MessageHubInfo &info) {
  LockGuard<Mutex> lock(mHubsLock);
  pw::IntrusiveList<Endpoint> endpoints;
  HostMessageHubManager::Hub::createLocked(this, info, endpoints);
}

void HostMessageHubManager::unregisterHub(MessageHubId id) {
  LockGuard<Mutex> lock(mHubsLock);

  for (auto it = mHubs.begin(); it != mHubs.end(); ++it) {
    if ((*it)->getMessageHub().getId() == id) {
      (*it)->clear();
      mHubs.erase(it);
      return;
    }
  }
  LOGE("No host hub 0x%" PRIx64 " for unregister", id);
}

void HostMessageHubManager::registerEndpoint(
    MessageHubId hubId, const EndpointInfo &info,
    DynamicVector<ServiceInfo> &&services) {
  LockGuard<Mutex> lock(mHubsLock);
  for (auto &hub : mHubs) {
    if (hub->getMessageHub().getId() != hubId) continue;
    hub->addEndpoint(info, std::move(services));
    return;
  }
  LOGE("No host hub 0x%" PRIx64 " for add endpoint", hubId);
}

void HostMessageHubManager::unregisterEndpoint(MessageHubId hubId,
                                               EndpointId id) {
  LockGuard<Mutex> lock(mHubsLock);
  for (auto &hub : mHubs) {
    if (hub->getMessageHub().getId() != hubId) continue;
    hub->removeEndpoint(id);
    hub->getMessageHub().unregisterEndpoint(id);
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
    if (hub->getMessageHub().getId() != hubId) continue;
    if (hub->getMessageHub().openSession(
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
    if (hub->getMessageHub().getId() != hubId) continue;
    hub->getMessageHub().onSessionOpenComplete(sessionId);
    mCb->onSessionOpened(hubId, sessionId);
    return;
  }
  LOGE("No host hub 0x%" PRIx64 " for ack session", hubId);
}

void HostMessageHubManager::closeSession(MessageHubId hubId,
                                         SessionId sessionId, Reason reason) {
  LockGuard<Mutex> lock(mHubsLock);
  for (auto &hub : mHubs) {
    if (hub->getMessageHub().getId() != hubId) continue;
    hub->getMessageHub().closeSession(sessionId, reason);
    return;
  }
  LOGE("No host hub 0x%" PRIx64 " for close session", hubId);
}

void HostMessageHubManager::sendMessage(MessageHubId hubId, SessionId sessionId,
                                        pw::span<const std::byte> data,
                                        uint32_t type, uint32_t permissions) {
  LockGuard<Mutex> lock(mHubsLock);
  for (auto &hub : mHubs) {
    if (hub->getMessageHub().getId() != hubId) continue;
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
    hub->getMessageHub().sendMessage(std::move(dataCopy), type, permissions,
                                     sessionId);
    return;
  }
  LOGE("No host hub 0x%" PRIx64 " for send message", hubId);
}

bool HostMessageHubManager::Hub::createLocked(
    HostMessageHubManager *manager, const MessageHubInfo &info,
    pw::IntrusiveList<Endpoint> &endpoints) {
  CHRE_ASSERT(manager != nullptr);

  // If there is an available slot, create a new Hub and try to register it with
  // MessageRouter, cleaning it up on failure.
  if (manager->mHubs.full()) {
    LOGE("No space to register new host hub 0x%" PRIx64, info.id);
    deallocateEndpoints(endpoints);
    return false;
  }

  Hub *hubPtr = memoryAlloc<Hub>(manager, info.name, endpoints);
  if (hubPtr == nullptr) {
    LOGE("Failed to allocate storage for new host hub %" PRIu64, info.id);
    deallocateEndpoints(endpoints);
    return false;
  }

  pw::IntrusivePtr<Hub> hub(hubPtr);
  manager->mHubs.push_back(hub);
  std::optional<MessageRouter::MessageHub> maybeHub =
      MessageRouterSingleton::get()->registerMessageHub(hub->kName, info.id,
                                                        hub);
  if (!maybeHub) {
    LOGE("Failed to register host hub 0x%" PRIx64, info.id);
    manager->mHubs.pop_back();
    return false;
  }
  hub->mMessageHub = std::move(*maybeHub);
  return true;
}

HostMessageHubManager::Hub::Hub(HostMessageHubManager *manager,
                                const char *name,
                                pw::IntrusiveList<Endpoint> &endpoints)
    : mManager(manager) {
  std::strncpy(kName, name, kNameMaxLen);
  kName[kNameMaxLen] = 0;
  mEndpoints.splice_after(mEndpoints.before_begin(), endpoints);
}

HostMessageHubManager::Hub::~Hub() {
  // clear() should be explicitly called before destruction.
  CHRE_ASSERT_LOG(!mMessageHub.isRegistered(),
                  "Hub destroyed while registered");
}

void HostMessageHubManager::Hub::clear() {
  getMessageHub().unregister();

  // This lock needs to be held to ensure the manager does not destruct
  // while deallocating endpoints are called.
  LockGuard<Mutex> managerLock(mManagerLock);
  mManager = nullptr;

  LockGuard<Mutex> lock(mEndpointsLock);
  deallocateEndpoints(mEndpoints);
}

void HostMessageHubManager::Hub::addEndpoint(
    const EndpointInfo &info, DynamicVector<ServiceInfo> &&services) {
  Endpoint *endpoint = nullptr;
  {
    LockGuard<Mutex> managerLock(mManagerLock);
    CHRE_ASSERT_LOG(mManager != nullptr,
                    "The HostMessageHubManager has been destroyed.");

    endpoint = mManager->mEndpointAllocator.allocate(info, std::move(services));
    if (endpoint == nullptr) {
      LOGE("Failed to allocate storage for endpoint (0x%" PRIx64 ", 0x%" PRIx64
           ")",
           mMessageHub.getId(), info.id);
      for (const auto &service : services)
        memoryFree(const_cast<char *>(service.serviceDescriptor));
      return;
    }
  }

  {
    LockGuard<Mutex> lock(mEndpointsLock);
    mEndpoints.push_back(*endpoint);
  }
  mMessageHub.registerEndpoint(info.id);
}

void HostMessageHubManager::Hub::removeEndpoint(EndpointId id) {
  Endpoint *endpoint = nullptr;
  {
    LockGuard<Mutex> lock(mEndpointsLock);
    for (auto it = mEndpoints.begin(), eraseIt = mEndpoints.before_begin();
         it != mEndpoints.end(); ++it, ++eraseIt) {
      if (it->kInfo.id == id) {
        mEndpoints.erase_after(eraseIt);
        endpoint = &(*it);
        break;
      }
    }
  }
  if (endpoint) {
    LockGuard<Mutex> managerLock(mManagerLock);
    CHRE_ASSERT_LOG(mManager != nullptr,
                    "The HostMessageHubManager has been destroyed.");
    mManager->mEndpointAllocator.deallocate(endpoint);
  }
}

bool HostMessageHubManager::Hub::onMessageReceived(
    pw::UniquePtr<std::byte[]> &&data, uint32_t messageType,
    uint32_t messagePermissions, const Session &session,
    bool /*sentBySessionInitiator*/) {
  LockGuard<Mutex> managerLock(mManagerLock);
  if (mManager == nullptr) {
    LOGW("The HostMessageHubManager has been destroyed.");
    return false;
  }

  return mManager->mCb->onMessageReceived(mMessageHub.getId(),
                                          session.sessionId, std::move(data),
                                          messageType, messagePermissions);
}

void HostMessageHubManager::Hub::onSessionOpenRequest(const Session &session) {
  LockGuard<Mutex> managerLock(mManagerLock);
  if (mManager == nullptr) {
    LOGW("The HostMessageHubManager has been destroyed.");
    return;
  }

  return mManager->mCb->onSessionOpenRequest(session);
}

void HostMessageHubManager::Hub::onSessionOpened(const Session &session) {
  LockGuard<Mutex> managerLock(mManagerLock);
  if (mManager == nullptr) {
    LOGW("The HostMessageHubManager has been destroyed.");
    return;
  }

  return mManager->mCb->onSessionOpened(mMessageHub.getId(), session.sessionId);
}

void HostMessageHubManager::Hub::onSessionClosed(const Session &session,
                                                 Reason reason) {
  LockGuard<Mutex> managerLock(mManagerLock);
  if (mManager == nullptr) {
    LOGW("The HostMessageHubManager has been destroyed.");
    return;
  }

  return mManager->mCb->onSessionClosed(mMessageHub.getId(), session.sessionId,
                                        reason);
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
    const char *serviceDescriptor) {
  LockGuard<Mutex> lock(mEndpointsLock);
  for (const auto &endpoint : mEndpoints) {
    for (const auto &service : endpoint.mServices) {
      if (!std::strcmp(serviceDescriptor, service.serviceDescriptor))
        return endpoint.kInfo.id;
    }
  }
  return {};
}

bool HostMessageHubManager::Hub::doesEndpointHaveService(
    EndpointId endpointId, const char *serviceDescriptor) {
  LockGuard<Mutex> lock(mEndpointsLock);
  for (const auto &endpoint : mEndpoints) {
    if (endpoint.kInfo.id != endpointId) continue;
    for (const auto &service : endpoint.mServices) {
      if (!std::strcmp(serviceDescriptor, service.serviceDescriptor))
        return true;
    }
    return false;
  }
  return false;
}

void HostMessageHubManager::Hub::forEachService(
    const pw::Function<bool(const message::EndpointInfo &,
                            const message::ServiceInfo &)> &function) {
  LockGuard<Mutex> lock(mEndpointsLock);
  for (const auto &endpoint : mEndpoints) {
    for (const auto &service : endpoint.mServices)
      function(endpoint.kInfo, service);
  }
}

void HostMessageHubManager::Hub::onHubRegistered(const MessageHubInfo &info) {
  LockGuard<Mutex> managerLock(mManagerLock);
  if (mManager == nullptr) {
    LOGW("The HostMessageHubManager has been destroyed.");
    return;
  }

  LockGuard<Mutex> lock(mManager->mEmbeddedHubOpLock);
  mManager->mCb->onHubRegistered(info);
}

void HostMessageHubManager::Hub::onHubUnregistered(MessageHubId id) {
  LockGuard<Mutex> managerLock(mManagerLock);
  if (mManager == nullptr) {
    LOGW("The HostMessageHubManager has been destroyed.");
    return;
  }

  LockGuard<Mutex> lock(mManager->mEmbeddedHubOpLock);
  mManager->mCb->onHubUnregistered(id);
}

void HostMessageHubManager::Hub::onEndpointRegistered(MessageHubId messageHubId,
                                                      EndpointId endpointId) {
  std::optional<EndpointInfo> endpoint =
      MessageRouterSingleton::get()->getEndpointInfo(messageHubId, endpointId);
  if (!endpoint) return;
  LockGuard<Mutex> managerLock(mManagerLock);
  if (mManager == nullptr) {
    LOGW("The HostMessageHubManager has been destroyed.");
    return;
  }
  LockGuard<Mutex> lock(mManager->mEmbeddedHubOpLock);

  mManager->mCb->onEndpointRegistered(messageHubId, *endpoint);
  struct {
    HostCallback *cb;
    MessageHubId hub;
    EndpointId endpoint;
  } context = {
      .cb = mManager->mCb, .hub = messageHubId, .endpoint = endpointId};
  MessageRouterSingleton::get()->forEachService(
      [&context](const MessageHubInfo &hub, const EndpointInfo &endpoint,
                 const ServiceInfo &service) {
        if (context.hub != hub.id) return false;
        if (context.endpoint != endpoint.id) return false;
        context.cb->onEndpointService(hub.id, endpoint.id, service);
        return false;
      });
  mManager->mCb->onEndpointReady(messageHubId, endpointId);
}

void HostMessageHubManager::Hub::onEndpointUnregistered(
    MessageHubId messageHubId, EndpointId endpointId) {
  LockGuard<Mutex> managerLock(mManagerLock);
  if (mManager == nullptr) {
    LOGW("The HostMessageHubManager has been destroyed.");
    return;
  }

  LockGuard<Mutex> lock(mManager->mEmbeddedHubOpLock);
  mManager->mCb->onEndpointUnregistered(messageHubId, endpointId);
}

void HostMessageHubManager::Hub::pw_recycle() {
  memoryFreeAndDestroy(this);
}

void *HostMessageHubManager::ChreAllocator::DoAllocate(Layout layout) {
  return memoryAlloc(layout.size());
}

void HostMessageHubManager::ChreAllocator::DoDeallocate(void *ptr) {
  memoryFree(ptr);
}

void HostMessageHubManager::deallocateEndpoints(
    pw::IntrusiveList<Endpoint> &endpoints) {
  while (!endpoints.empty()) {
    auto &endpoint = endpoints.front();
    endpoints.pop_front();
    EventLoopManagerSingleton::get()
        ->getHostMessageHubManager()
        .mEndpointAllocator.deallocate(&endpoint);
  }
}

void HostMessageHubManager::clearHubsLocked() {
  // Deactivate all existing message hubs. We need to call clear() on each hub
  // to unregister it from MessageRouter as both MessageRouter and the
  // HostMessageHubManager have a pw::IntrusivePtr to the Hub, which will not
  // deallocate the Hub until both references are gone.
  for (auto &hub : mHubs) {
    hub->clear();
  }
  mHubs.clear();
}

}  // namespace chre

#endif  // CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED
