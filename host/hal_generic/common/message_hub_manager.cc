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

MessageHubManager::HostHub::~HostHub() {
  std::lock_guard lock(mManager.mLock);
  unlinkCallbackIfNecessaryLocked();
}

pw::Status MessageHubManager::HostHub::setCallback(
    std::shared_ptr<IEndpointCallback> callback) {
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

std::shared_ptr<IEndpointCallback> MessageHubManager::HostHub::getCallback()
    const {
  std::lock_guard lock(mManager.mLock);
  return mCallback;
}

pw::Status MessageHubManager::HostHub::addEndpoint(std::weak_ptr<HostHub> self,
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

pw::Status MessageHubManager::HostHub::removeEndpoint(const EndpointId &id) {
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

pw::Result<std::pair<uint16_t, uint16_t>>
MessageHubManager::HostHub::reserveSessionIdRange(uint16_t size) {
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

pw::Status MessageHubManager::HostHub::openSession(std::weak_ptr<HostHub> self,
                                                   const EndpointId &localId,
                                                   const EndpointId &remoteId,
                                                   uint16_t sessionId,
                                                   bool hostInitiated) {
  std::lock_guard lock(mManager.mLock);
  PW_TRY(checkValidLocked());

  // Check that the local endpoint exists.
  auto localEndpointIt = mIdToEndpoint.find(localId.id);
  if (localEndpointIt == mIdToEndpoint.end()) {
    LOGE("No endpoint %ld in hub %ld for session %hu", localId.id, kId,
         sessionId);
    return pw::Status::InvalidArgument();
  }

  if (hostInitiated) {
    // Check that the session id is within the ranges allocated to this hub.
    bool sessionIdValid = false;
    for (auto range : mSessionIdRanges) {
      if (sessionId >= range.first && sessionId <= range.second) {
        sessionIdValid = true;
        break;
      }
    }
    if (!sessionIdValid) {
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

  // Check that the session is not in use.
  if (mSessionIdToEndpoint.count(sessionId)) {
    LOGE("Hub %ld trying to re-use session %lu", kId, sessionId);
    return pw::Status::AlreadyExists();
  }

  // Check that the remote endpoint exists.
  if (!mManager.findEmbeddedEndpointLocked(remoteId)) {
    LOGE("Hub %ld trying to open session %lu with unknown endpoint (%ld, %ld)",
         kId, sessionId, remoteId.hubId, remoteId.id);
    return pw::Status::NotFound();
  }

  // Store mappings from the session id to this hub and the local endpoint.
  mManager.mSessionIdToHostHub[sessionId] = self;
  mSessionIdToEndpoint.insert({sessionId, localEndpointIt->second});
  return pw::OkStatus();
}

pw::Status MessageHubManager::HostHub::closeSession(uint16_t id) {
  std::lock_guard lock(mManager.mLock);
  PW_TRY(checkValidLocked());
  if (!mSessionIdToEndpoint.erase(id)) {
    LOGE("Hub %ld closing unused session %hu", kId, id);
    return pw::Status::NotFound();
  }
  mManager.mSessionIdToHostHub.erase(id);
  return pw::OkStatus();
}

pw::Result<EndpointInfo> MessageHubManager::HostHub::getEndpointById(
    int64_t id) {
  std::lock_guard lock(mManager.mLock);
  if (auto it = mIdToEndpoint.find(id); it != mIdToEndpoint.end())
    return *it->second;
  return pw::Status::NotFound();
}

pw::Result<EndpointInfo> MessageHubManager::HostHub::getEndpointBySessionId(
    uint16_t id) {
  std::lock_guard lock(mManager.mLock);
  if (auto it = mSessionIdToEndpoint.find(id);
      it != mSessionIdToEndpoint.end()) {
    if (auto infoPtr = it->second.lock()) return *infoPtr;
    // Prune the session id if the endpoint is gone.
    mSessionIdToEndpoint.erase(it);
    mManager.mSessionIdToHostHub.erase(id);
  }
  LOGW("Could not find target endpoint for session %hu in hub %ld", id, kId);
  return pw::Status::NotFound();
}

int64_t MessageHubManager::HostHub::id() const {
  std::lock_guard lock(mManager.mLock);
  return kId;
}

int64_t MessageHubManager::MessageHubManager::HostHub::unlinkFromManager() {
  std::lock_guard lock(mManager.mLock);
  // TODO(b/378545373): Release the session id range.
  for (const auto &[sessionId, endpoint] : mSessionIdToEndpoint)
    mManager.mSessionIdToHostHub.erase(sessionId);
  if (kId != kHubIdInvalid) mManager.mIdToHostHub.erase(kId);
  mManager.mPidToHostHub.erase(kPid);
  return kId;
}

void MessageHubManager::HostHub::unlinkCallbackIfNecessaryLocked() {
  if (!mCallback) return;
  if (AIBinder_unlinkToDeath(mCallback->asBinder().get(),
                             mManager.mDeathRecipient.get(),
                             mCookie) != STATUS_OK) {
    LOGW("Failed to unlink client (pid: %d, hub id: %ld)", kPid, kId);
  }
  mCallback.reset();
  mCookie = nullptr;
}

pw::Status MessageHubManager::HostHub::checkValidLocked() {
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

std::shared_ptr<MessageHubManager::HostHub> MessageHubManager::getHostHubByPid(
    pid_t pid) {
  std::lock_guard lock(mLock);
  if (auto it = mPidToHostHub.find(pid); it != mPidToHostHub.end())
    return it->second;
  std::shared_ptr<HostHub> hub(new HostHub(*this, pid));
  mPidToHostHub.insert({pid, hub});
  return hub;
}

std::shared_ptr<MessageHubManager::HostHub>
MessageHubManager::getHostHubByEndpointId(const EndpointId &id) {
  std::lock_guard lock(mLock);
  if (auto it = mIdToHostHub.find(id.hubId); it != mIdToHostHub.end())
    return it->second.lock();
  return {};
}

std::shared_ptr<MessageHubManager::HostHub>
MessageHubManager::getHostHubBySessionId(uint16_t id) {
  std::lock_guard lock(mLock);
  if (auto it = mSessionIdToHostHub.find(id); it != mSessionIdToHostHub.end())
    return it->second.lock();
  return {};
}

void MessageHubManager::updateEmbeddedHubsAndEndpoints(
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

void MessageHubManager::removeEmbeddedHub(int64_t id) {
  std::lock_guard lock(mLock);
  mIdToEmbeddedHub.erase(id);
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
      endpoints.push_back(endptInfo);
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
  it->second.idToEndpoint.insert({endpoint.id.id, endpoint});
}

bool MessageHubManager::findEmbeddedEndpointLocked(const EndpointId &id) {
  auto it = mIdToEmbeddedHub.find(id.hubId);
  return it != mIdToEmbeddedHub.end() && it->second.idToEndpoint.count(id.id);
}

}  // namespace android::hardware::contexthub::common::implementation
