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

#include "message_hub_manager.h"

#include <unistd.h>

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <aidl/android/hardware/contexthub/BnContextHub.h>

#include "chre_host/log.h"
#include "pw_result/result.h"
#include "pw_status/status.h"
#include "pw_status/try.h"

namespace android::hardware::contexthub::common::implementation {

using HostHub = MessageHubManager::HostHub;

HostHub::~HostHub() {
  std::lock_guard lock(mManager.mLock);
  unlinkCallbackIfNecessaryLocked();
}

pw::Status HostHub::setCallback(std::shared_ptr<IEndpointCallback> callback) {
  std::lock_guard lock(mManager.mLock);
  auto *cookie = new DeathRecipientCookie{&mManager, kPid};
  if (AIBinder_linkToDeath(callback->asBinder().get(),
                           mManager.mDeathRecipient.get(),
                           cookie) != STATUS_OK) {
    LOGE("Failed to link callback for hub %ld (pid %d) to death recipient", kId,
         kPid);
    delete cookie;
    return pw::Status::Internal();
  }
  unlinkCallbackIfNecessaryLocked();
  mCallback = std::move(callback);
  mCookie = cookie;
  return pw::OkStatus();
}

std::shared_ptr<IEndpointCallback> HostHub::getCallback() const {
  std::lock_guard lock(mManager.mLock);
  return mCallback;
}

pw::Status HostHub::addEndpoint(std::weak_ptr<HostHub> self,
                                const EndpointInfo &info) {
  std::lock_guard lock(mManager.mLock);
  PW_TRY(checkValidLocked());
  int64_t id = info.id.id;
  if (auto it = mIdToEndpoint.find(id); it != mIdToEndpoint.end()) {
    LOGE("Endpoint %ld already exists in hub %ld (pid %d)", id, kId, kPid);
    return pw::Status::AlreadyExists();
  }
  if (kId == kHubIdInvalid) {
    // If this is the hub's first endpoint, store its hub id and register it
    // with the manager.
    if (info.id.hubId == kContextHubServiceHubId &&
        AIBinder_getCallingUid() != kSystemServerUid) {
      LOGW(
          "Non-systemserver client (pid %d) trying to register as "
          "ContextHubService",
          kPid);
      return pw::Status::InvalidArgument();
    }
    kId = info.id.hubId;
    mManager.mIdToHostHub.insert({kId, self});
  }
  mIdToEndpoint.insert({id, std::make_unique<EndpointInfo>(info)});
  return pw::OkStatus();
}

pw::Status HostHub::removeEndpoint(const EndpointId &id) {
  std::lock_guard lock(mManager.mLock);
  PW_TRY(checkValidLocked());
  if (auto it = mIdToEndpoint.find(id.id); it != mIdToEndpoint.end()) {
    mIdToEndpoint.erase(it);
    return pw::OkStatus();
  }
  LOGE("Client (hub %ld, pid %d) tried to remove unknown endpoint %ld", kId,
       kPid, id.id);
  return pw::Status::NotFound();
}

pw::Result<std::pair<uint16_t, uint16_t>> HostHub::reserveSessionIdRange(
    uint16_t size) {
  std::lock_guard lock(mManager.mLock);
  PW_TRY(checkValidLocked());
  if (!size || size > kSessionIdMaxRange) {
    LOGE("Client (hub %ld, pid %d) tried to allocate %hu session ids", kId,
         kPid, size);
    return pw::Status::InvalidArgument();
  }
  if (USHRT_MAX - mManager.mNextSessionId + 1 < size) {
    LOGW("Could not allocate %hu session ids, ids exhausted", size);
    return pw::Status::ResourceExhausted();
  }
  mSessionIdRanges.push_back(
      {mManager.mNextSessionId, mManager.mNextSessionId + size - 1});
  mManager.mNextSessionId += size;
  return mSessionIdRanges.back();
}

pw::Result<std::shared_ptr<HostHub>> HostHub::openSession(
    std::weak_ptr<HostHub> self, const EndpointId &localId,
    const EndpointId &remoteId, uint16_t sessionId) {
  std::lock_guard lock(mManager.mLock);
  PW_TRY(checkValidLocked());

  // Lookup the endpoints.
  PW_TRY_ASSIGN(std::shared_ptr<EndpointInfo> local,
                getEndpointLocked(localId));
  PW_TRY_ASSIGN(std::shared_ptr<EndpointInfo> remote,
                mManager.getEmbeddedEndpointLocked(remoteId));

  // Validate the session id.
  bool hostInitiated = AIBinder_isHandlingTransaction();
  if (hostInitiated) {
    if (!sessionIdInRangeLocked(sessionId)) {
      LOGE("Session id %hu out of range for hub %ld", sessionId, kId);
      return pw::Status::OutOfRange();
    }
  } else if (sessionId >= kHostSessionIdBase) {
    LOGE(
        "Remote endpoint (%ld, %ld) attempting to start session with "
        "invalid id %hu",
        remoteId.hubId, remoteId.id, sessionId);
    return pw::Status::InvalidArgument();
  }

  // Prune a stale session with this id if present.
  std::shared_ptr<HostHub> prunedHostHub;
  if (auto it = mManager.mIdToSession.find(sessionId);
      it != mManager.mIdToSession.end()) {
    SessionStrongRef session(it->second);
    if (session) {
      // If the session is in a valid state, prune it if it was not host
      // initiated and is pending a final ack from message router.
      if (!hostInitiated && !session.pendingDestination &&
          session.pendingMessageRouter) {
        prunedHostHub = std::move(session.hub);
      } else if (hostInitiated && session.local->id == localId) {
        LOGE("Hub %ld trying to override its own session %hu", kId, sessionId);
        return pw::Status::InvalidArgument();
      } else {
        LOGE("(host? %d) trying to override session id %hu, hub %ld",
             hostInitiated, sessionId, kId);
        return pw::Status::AlreadyExists();
      }
    }
    mManager.mIdToSession.erase(it);
  }

  // Create and map the new session.
  mManager.mIdToSession.emplace(
      std::piecewise_construct, std::forward_as_tuple(sessionId),
      std::forward_as_tuple(self, local, remote, hostInitiated));
  return prunedHostHub;
}

pw::Status HostHub::closeSession(uint16_t id) {
  std::lock_guard lock(mManager.mLock);
  PW_TRY(checkValidLocked());
  auto it = mManager.mIdToSession.find(id);
  if (it == mManager.mIdToSession.end()) {
    LOGE("Closing unopened session %hu", id);
    return pw::Status::NotFound();
  }
  SessionStrongRef session(it->second);
  if (session && session.hub->kPid != kPid) {
    LOGE("Trying to close session %hu for client %d from client %d (hub %ld)",
         id, session.hub->kPid, kPid, kId);
    return pw::Status::PermissionDenied();
  }
  mManager.mIdToSession.erase(it);
  return pw::OkStatus();
}

pw::Status HostHub::ackSession(uint16_t id) {
  return mManager.ackSessionAndGetHostHub(id).status();
}

pw::Status HostHub::checkSessionOpen(uint16_t id) {
  return mManager.checkSessionOpenAndGetHostHub(id).status();
}

int64_t HostHub::id() const {
  std::lock_guard lock(mManager.mLock);
  return kId;
}

int64_t MessageHubManager::HostHub::unlinkFromManager() {
  std::lock_guard lock(mManager.mLock);
  // TODO(b/378545373): Release the session id range.
  if (kId != kHubIdInvalid) mManager.mIdToHostHub.erase(kId);
  mManager.mPidToHostHub.erase(kPid);
  mUnlinked = true;
  return kId;
}

void HostHub::unlinkCallbackIfNecessaryLocked() {
  if (!mCallback) return;
  if (AIBinder_unlinkToDeath(mCallback->asBinder().get(),
                             mManager.mDeathRecipient.get(),
                             mCookie) != STATUS_OK) {
    LOGW("Failed to unlink client (pid: %d, hub id: %ld)", kPid, kId);
  }
  mCallback.reset();
  mCookie = nullptr;
}

pw::Status HostHub::checkValidLocked() {
  if (!mCallback) {
    ALOGW("Endpoint APIs invoked by client %d before callback registered",
          kPid);
    return pw::Status::FailedPrecondition();
  } else if (mUnlinked) {
    ALOGW("Client %d went down mid-operation", kPid);
    return pw::Status::Aborted();
  }
  return pw::OkStatus();
}

pw::Result<std::shared_ptr<EndpointInfo>> HostHub::getEndpointLocked(
    const EndpointId &id) {
  if (id.hubId != kId) {
    LOGE("Rejecting lookup on unowned endpoint (%ld, %ld) from hub %ld",
         id.hubId, id.id, kId);
    return pw::Status::InvalidArgument();
  }
  if (auto it = mIdToEndpoint.find(id.id); it != mIdToEndpoint.end())
    return it->second;
  return pw::Status::NotFound();
}

bool HostHub::sessionIdInRangeLocked(uint16_t id) {
  for (auto range : mSessionIdRanges) {
    if (id >= range.first && id <= range.second) return true;
  }
  return false;
}

MessageHubManager::MessageHubManager(HostHubDownCb cb)
    : mHostHubDownCb(std::move(cb)) {
  mDeathRecipient = ndk::ScopedAIBinder_DeathRecipient(
      AIBinder_DeathRecipient_new(onClientDeath));
  AIBinder_DeathRecipient_setOnUnlinked(
      mDeathRecipient.get(), /*onUnlinked= */ [](void *cookie) {
        LOGI("Callback is unlinked. Releasing the death recipient cookie.");
        delete static_cast<HostHub::DeathRecipientCookie *>(cookie);
      });
}

std::shared_ptr<HostHub> MessageHubManager::getHostHubByPid(pid_t pid) {
  std::lock_guard lock(mLock);
  if (auto it = mPidToHostHub.find(pid); it != mPidToHostHub.end())
    return it->second;
  std::shared_ptr<HostHub> hub(new HostHub(*this, pid));
  mPidToHostHub.insert({pid, hub});
  return hub;
}

std::shared_ptr<HostHub> MessageHubManager::getHostHubByEndpointId(
    const EndpointId &id) {
  std::lock_guard lock(mLock);
  if (auto it = mIdToHostHub.find(id.hubId); it != mIdToHostHub.end())
    return it->second.lock();
  return {};
}

pw::Result<std::shared_ptr<HostHub>>
MessageHubManager::checkSessionOpenAndGetHostHub(uint16_t id) {
  std::lock_guard lock(mLock);
  PW_TRY_ASSIGN(SessionStrongRef session, checkSessionLocked(id));
  if (AIBinder_getCallingPid() != session.hub->kPid) {
    LOGE("Trying to check unowned session %hu", id);
    return pw::Status::PermissionDenied();
  }
  if (!session.pendingDestination && !session.pendingMessageRouter)
    return std::move(session.hub);
  LOGE("Session %hu is pending", id);
  return pw::Status::FailedPrecondition();
}

pw::Result<std::shared_ptr<HostHub>> MessageHubManager::ackSessionAndGetHostHub(
    uint16_t id) {
  std::lock_guard lock(mLock);
  PW_TRY_ASSIGN(SessionStrongRef session, checkSessionLocked(id));
  bool isBinderCall = AIBinder_isHandlingTransaction();
  bool isHostSession = id >= kHostSessionIdBase;
  if (isBinderCall && AIBinder_getCallingPid() != session.hub->kPid) {
    LOGE("Trying to ack unowned session %hu", id);
    return pw::Status::PermissionDenied();
  } else if (session.pendingDestination) {
    if (isHostSession == isBinderCall) {
      LOGE("Session %hu must be acked by other side (host? %d)", id,
           !isBinderCall);
      return pw::Status::PermissionDenied();
    }
    session.pendingDestination = false;
  } else if (session.pendingMessageRouter) {
    if (isBinderCall) {
      LOGE("Message router must ack session %hu", id);
      return pw::Status::PermissionDenied();
    }
    session.pendingMessageRouter = false;
  } else {
    LOGE("Received unexpected ack on session %hu, host: %d", id, isBinderCall);
  }
  return std::move(session.hub);
}

void MessageHubManager::forEachHostHub(std::function<void(HostHub &hub)> fn) {
  std::list<std::shared_ptr<HostHub>> hubs;
  {
    std::lock_guard lock(mLock);
    for (auto &[pid, hub] : mPidToHostHub) hubs.push_back(hub);
  }
  for (auto &hub : hubs) fn(*hub);
}

pw::Result<MessageHubManager::SessionStrongRef>
MessageHubManager::checkSessionLocked(uint16_t id) {
  auto sessionIt = mIdToSession.find(id);
  if (sessionIt == mIdToSession.end()) {
    LOGE("Did not find expected session %hu", id);
    return pw::Status::NotFound();
  }
  SessionStrongRef session(sessionIt->second);
  if (!session) {
    LOGD(
        "Pruning session %hu due to one or more of host hub, host endpoint, "
        "or embedded endpoint going down.",
        id);
    mIdToSession.erase(sessionIt);
    return pw::Status::Unavailable();
  }
  return std::move(session);
}

void MessageHubManager::initEmbeddedHubsAndEndpoints(
    const std::vector<HubInfo> &hubs,
    const std::vector<EndpointInfo> &endpoints) {
  std::lock_guard lock(mLock);
  mIdToEmbeddedHub.clear();
  for (const auto &hub : hubs) mIdToEmbeddedHub[hub.hubId].info = hub;
  for (const auto &endpoint : endpoints) addEmbeddedEndpointLocked(endpoint);
}

void MessageHubManager::addEmbeddedHub(const HubInfo &hub) {
  std::lock_guard lock(mLock);
  if (mIdToEmbeddedHub.count(hub.hubId)) return;
  mIdToEmbeddedHub[hub.hubId].info = hub;
}

std::vector<EndpointId> MessageHubManager::removeEmbeddedHub(int64_t id) {
  std::lock_guard lock(mLock);
  std::vector<EndpointId> endpoints;
  auto it = mIdToEmbeddedHub.find(id);
  if (it != mIdToEmbeddedHub.end()) {
    for (const auto &[endpointId, info] : it->second.idToEndpoint)
      endpoints.push_back({.id = endpointId, .hubId = id});
    mIdToEmbeddedHub.erase(it);
  }
  return endpoints;
}

std::vector<HubInfo> MessageHubManager::getEmbeddedHubs() const {
  std::lock_guard lock(mLock);
  std::vector<HubInfo> hubs;
  for (const auto &[id, hub] : mIdToEmbeddedHub) hubs.push_back(hub.info);
  return hubs;
}

void MessageHubManager::addEmbeddedEndpoint(const EndpointInfo &endpoint) {
  std::lock_guard lock(mLock);
  addEmbeddedEndpointLocked(endpoint);
}

std::vector<EndpointInfo> MessageHubManager::getEmbeddedEndpoints() const {
  std::lock_guard lock(mLock);
  std::vector<EndpointInfo> endpoints;
  for (const auto &[id, hub] : mIdToEmbeddedHub) {
    for (const auto &[endptId, endptInfo] : hub.idToEndpoint)
      endpoints.push_back(*endptInfo);
  }
  return endpoints;
}

void MessageHubManager::onClientDeath(void *cookie) {
  auto *cookieData = reinterpret_cast<HostHub::DeathRecipientCookie *>(cookie);
  MessageHubManager *manager = cookieData->manager;
  std::shared_ptr<HostHub> hub = manager->getHostHubByPid(cookieData->pid);
  LOGW("Hub %ld (pid %d) died", hub->id(), cookieData->pid);
  manager->mHostHubDownCb(hub->unlinkFromManager());
}

void MessageHubManager::addEmbeddedEndpointLocked(
    const EndpointInfo &endpoint) {
  auto it = mIdToEmbeddedHub.find(endpoint.id.hubId);
  if (it == mIdToEmbeddedHub.end()) {
    LOGW("Could not find hub %ld for endpoint %ld", endpoint.id.hubId,
         endpoint.id.id);
    return;
  }
  it->second.idToEndpoint.insert(
      {endpoint.id.id, std::make_shared<EndpointInfo>(endpoint)});
}

pw::Result<std::shared_ptr<EndpointInfo>>
MessageHubManager::getEmbeddedEndpointLocked(const EndpointId &id) {
  auto hubIt = mIdToEmbeddedHub.find(id.hubId);
  if (hubIt != mIdToEmbeddedHub.end()) {
    auto it = hubIt->second.idToEndpoint.find(id.id);
    if (it != hubIt->second.idToEndpoint.end()) return it->second;
  }
  LOGW("Could not find remote endpoint (%ld, %ld)", id.hubId, id.id);
  return pw::Status::NotFound();
}

}  // namespace android::hardware::contexthub::common::implementation
