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

#include <inttypes.h>
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
#include <android-base/thread_annotations.h>

#include "chre_host/log.h"
#include "pw_result/result.h"
#include "pw_status/status.h"
#include "pw_status/try.h"

namespace android::hardware::contexthub::common::implementation {

using ::aidl::android::hardware::contexthub::Reason;

using HostHub = MessageHubManager::HostHub;

HostHub::HostHub(MessageHubManager &manager,
                 std::shared_ptr<IEndpointCallback> callback,
                 const HubInfo &info)
    : mManager(manager), kInfo(info) {
  auto *cookie = new DeathRecipientCookie{&mManager, kInfo.hubId};
  if (AIBinder_linkToDeath(callback->asBinder().get(),
                           mManager.mDeathRecipient.get(),
                           cookie) != STATUS_OK) {
    LOGE("Failed to link callback for hub %" PRId64 " to death recipient",
         kInfo.hubId);
    delete cookie;
    return;
  }
  mCookie = cookie;
  mCallback = std::move(callback);
}

pw::Status HostHub::addEndpoint(const EndpointInfo &info) {
  std::lock_guard lock(mManager.mLock);
  PW_TRY(checkValidLocked());
  if (info.id.hubId != kInfo.hubId) {
    LOGE("Hub %" PRId64 " registering endpoint for different hub %" PRId64,
         kInfo.hubId, info.id.hubId);
    return pw::Status::PermissionDenied();
  }
  int64_t id = info.id.id;
  if (auto it = mIdToEndpoint.find(id); it != mIdToEndpoint.end()) {
    LOGE("Endpoint %" PRId64 " already exists in hub %" PRId64, id,
         kInfo.hubId);
    return pw::Status::AlreadyExists();
  }
  mIdToEndpoint.insert({id, info});
  return pw::OkStatus();
}

pw::Result<std::vector<uint16_t>> HostHub::removeEndpoint(
    const EndpointId &id) {
  std::lock_guard lock(mManager.mLock);
  PW_TRY(checkValidLocked());
  if (auto it = mIdToEndpoint.find(id.id); it != mIdToEndpoint.end()) {
    std::vector<uint16_t> sessions;
    for (const auto &[sessionId, session] : mIdToSession) {
      if (session.mHostEndpoint == id) sessions.push_back(sessionId);
    }
    for (auto sessionId : sessions) mIdToSession.erase(sessionId);
    mIdToEndpoint.erase(it);
    return sessions;
  }
  LOGE("Hub %" PRId64 " tried to remove unknown endpoint %" PRId64, kInfo.hubId,
       id.id);
  return pw::Status::NotFound();
}

pw::Result<std::pair<uint16_t, uint16_t>> HostHub::reserveSessionIdRange(
    uint16_t size) {
  std::lock_guard lock(mManager.mLock);
  PW_TRY(checkValidLocked());
  if (!size || size > kSessionIdMaxRange) {
    LOGE("Hub %" PRId64 " tried to allocate %" PRIu16 " session ids",
         kInfo.hubId, size);
    return pw::Status::InvalidArgument();
  }
  if (USHRT_MAX - mManager.mNextSessionId + 1 < size) {
    LOGW("Could not allocate %" PRIu16 " session ids, ids exhausted", size);
    return pw::Status::ResourceExhausted();
  }
  mSessionIdRanges.push_back(
      {mManager.mNextSessionId, mManager.mNextSessionId + size - 1});
  mManager.mNextSessionId += size;
  return mSessionIdRanges.back();
}

pw::Result<bool> HostHub::openSession(const EndpointId &hostEndpoint,
                                      const EndpointId &embeddedEndpoint,
                                      uint16_t sessionId) {
  std::lock_guard lock(mManager.mLock);
  PW_TRY(checkValidLocked());

  // Lookup the endpoints.
  PW_TRY(endpointExistsLocked(hostEndpoint));
  PW_TRY(mManager.embeddedEndpointExistsLocked(embeddedEndpoint));

  // Validate the session id.
  bool hostInitiated = AIBinder_isHandlingTransaction();
  if (hostInitiated) {
    if (!sessionIdInRangeLocked(sessionId)) {
      LOGE("Session id %" PRIu16 " out of range for hub %" PRId64, sessionId,
           kInfo.hubId);
      return pw::Status::OutOfRange();
    }
  } else if (sessionId >= kHostSessionIdBase) {
    LOGE("Remote endpoint (%" PRId64 ", %" PRId64
         ") attempting to start "
         "session with invalid id %" PRIu16,
         embeddedEndpoint.hubId, embeddedEndpoint.id, sessionId);
    return pw::Status::InvalidArgument();
  }

  // Prune a stale session with this id if present.
  bool sendClose = false;
  if (auto it = mIdToSession.find(sessionId); it != mIdToSession.end()) {
    Session &session = it->second;
    // If the session is in a valid state, prune it if it was not host
    // initiated and is pending a final ack from message router.
    if (!hostInitiated && !session.mPendingDestination &&
        session.mPendingMessageRouter) {
      sendClose = true;
    } else if (hostInitiated && session.mHostEndpoint == hostEndpoint) {
      LOGE("Hub %" PRId64 " trying to override its own session %" PRIu16,
           kInfo.hubId, sessionId);
      return pw::Status::InvalidArgument();
    } else {
      LOGE("(host? %" PRId32 ") trying to override session id %" PRIu16
           ", hub %" PRId64,
           hostInitiated, sessionId, kInfo.hubId);
      return pw::Status::AlreadyExists();
    }
    mIdToSession.erase(it);
  }

  // Create and map the new session.
  mIdToSession.emplace(
      std::piecewise_construct, std::forward_as_tuple(sessionId),
      std::forward_as_tuple(hostEndpoint, embeddedEndpoint, hostInitiated));
  return sendClose;
}

pw::Status HostHub::closeSession(uint16_t id) {
  std::lock_guard lock(mManager.mLock);
  PW_TRY(checkValidLocked());
  auto it = mIdToSession.find(id);
  if (it == mIdToSession.end()) {
    LOGE("Closing unopened session %" PRIu16, id);
    return pw::Status::NotFound();
  }
  mIdToSession.erase(it);
  return pw::OkStatus();
}

pw::Status HostHub::checkSessionOpen(uint16_t id) {
  std::lock_guard lock(mManager.mLock);
  PW_TRY(checkValidLocked());
  PW_TRY_ASSIGN(Session * session, getSessionLocked(id));
  if (!session->mPendingDestination && !session->mPendingMessageRouter)
    return pw::OkStatus();
  LOGE("Session %" PRIu16 " is pending", id);
  return pw::Status::FailedPrecondition();
}

pw::Status HostHub::ackSession(uint16_t id) {
  std::lock_guard lock(mManager.mLock);
  PW_TRY(checkValidLocked());
  PW_TRY_ASSIGN(Session * session, getSessionLocked(id));
  bool isBinderCall = AIBinder_isHandlingTransaction();
  bool isHostSession = id >= kHostSessionIdBase;
  if (session->mPendingDestination) {
    if (isHostSession == isBinderCall) {
      LOGE("Session %" PRIu16 " must be acked by other side (host? %" PRId32
           ")",
           id, !isBinderCall);
      return pw::Status::PermissionDenied();
    }
    session->mPendingDestination = false;
  } else if (session->mPendingMessageRouter) {
    if (isBinderCall) {
      LOGE("Message router must ack session %" PRIu16, id);
      return pw::Status::PermissionDenied();
    }
    session->mPendingMessageRouter = false;
  } else {
    LOGE("Received unexpected ack on session %" PRIu16 ", host: %" PRId32, id,
         isBinderCall);
  }
  return pw::OkStatus();
}

pw::Status HostHub::unregister() {
  // If unlinkFromManager() fails, onClientDeath() was already called for this
  // and we do not need to unlink the death recipient.
  PW_TRY(unlinkFromManager());
  if (AIBinder_unlinkToDeath(mCallback->asBinder().get(),
                             mManager.mDeathRecipient.get(),
                             mCookie) != STATUS_OK) {
    LOGW("Process hosting hub %" PRId64 " died simultaneously with unregister",
         kInfo.hubId);
  }
  return pw::OkStatus();
}

std::vector<EndpointInfo> HostHub::getEndpoints() const {
  std::vector<EndpointInfo> endpoints;
  std::lock_guard lock(mManager.mLock);
  for (const auto &[id, endpoint] : mIdToEndpoint)
    endpoints.push_back(endpoint);
  return endpoints;
}

pw::Status HostHub::unlinkFromManager() {
  std::lock_guard lock(mManager.mLock);
  PW_TRY(checkValidLocked());  // returns early if already unlinked
  // TODO(b/378545373): Release the session id range.
  mManager.mIdToHostHub.erase(kInfo.hubId);
  mUnlinked = true;
  return pw::OkStatus();
}

pw::Status HostHub::checkValidLocked() {
  if (!mCallback) {
    ALOGE("APIs invoked on hub %" PRId64
          " which was not successfully registered.",
          kInfo.hubId);
    return pw::Status::FailedPrecondition();
  } else if (mUnlinked) {
    ALOGW("Hub %" PRId64 " went down mid-operation", kInfo.hubId);
    return pw::Status::Aborted();
  }
  return pw::OkStatus();
}

pw::Status HostHub::endpointExistsLocked(const EndpointId &id) {
  if (id.hubId != kInfo.hubId) {
    LOGE("Rejecting lookup on unowned endpoint (%" PRId64 ", %" PRId64
         ") from hub %" PRId64,
         id.hubId, id.id, kInfo.hubId);
    return pw::Status::InvalidArgument();
  }
  if (auto it = mIdToEndpoint.find(id.id); it != mIdToEndpoint.end())
    return pw::OkStatus();
  return pw::Status::NotFound();
}

bool HostHub::sessionIdInRangeLocked(uint16_t id) {
  for (auto range : mSessionIdRanges) {
    if (id >= range.first && id <= range.second) return true;
  }
  return false;
}

pw::Result<HostHub::Session *> HostHub::getSessionLocked(uint16_t id) {
  auto sessionIt = mIdToSession.find(id);
  if (sessionIt == mIdToSession.end()) {
    LOGE("Did not find expected session %" PRIu16 " in hub %" PRId64, id,
         kInfo.hubId);
    return pw::Status::NotFound();
  }
  return &sessionIt->second;
}

MessageHubManager::MessageHubManager(HostHubDownCb cb)
    : mHostHubDownCb(std::move(cb)) {
  mDeathRecipient = ndk::ScopedAIBinder_DeathRecipient(
      AIBinder_DeathRecipient_new(onClientDeath));
  AIBinder_DeathRecipient_setOnUnlinked(
      mDeathRecipient.get(), /*onUnlinked= */ [](void *cookie) {
        LOGD("Callback is unlinked. Releasing the death recipient cookie.");
        delete static_cast<HostHub::DeathRecipientCookie *>(cookie);
      });
}

pw::Result<std::shared_ptr<HostHub>> MessageHubManager::createHostHub(
    std::shared_ptr<IEndpointCallback> callback, const HubInfo &info) {
  if (info.hubId == kContextHubServiceHubId &&
      AIBinder_getCallingUid() != kSystemServerUid) {
    LOGE("(pid %" PRId32 ", uid %" PRId32
         ") attempting to impersonate ContextHubService",
         AIBinder_getCallingPid(), AIBinder_getCallingUid());
    return pw::Status::PermissionDenied();
  }
  std::lock_guard lock(mLock);
  if (mIdToHostHub.count(info.hubId)) return pw::Status::AlreadyExists();
  std::shared_ptr<HostHub> hub(new HostHub(*this, std::move(callback), info));
  if (!hub->callback()) return pw::Status::Internal();
  mIdToHostHub.insert({info.hubId, hub});
  LOGI("Registered host hub %" PRId64, info.hubId);
  return hub;
}

std::shared_ptr<HostHub> MessageHubManager::getHostHub(int64_t id) {
  std::lock_guard lock(mLock);
  if (auto it = mIdToHostHub.find(id); it != mIdToHostHub.end())
    return it->second;
  return {};
}

void MessageHubManager::forEachHostHub(std::function<void(HostHub &hub)> fn) {
  std::list<std::shared_ptr<HostHub>> hubs;
  {
    std::lock_guard lock(mLock);
    for (auto &[id, hub] : mIdToHostHub) hubs.push_back(hub);
  }
  for (auto &hub : hubs) fn(*hub);
}

void MessageHubManager::initEmbeddedState() {
  std::lock_guard lock(mLock);
  mIdToEmbeddedHub.clear();
  mIdToEmbeddedHubReady = true;
}

void MessageHubManager::clearEmbeddedState() {
  std::lock_guard lock(mLock);
  mIdToEmbeddedHubReady = false;

  // Clear embedded hub state, caching the list of now removed endpoints.
  std::vector<EndpointId> endpoints;
  for (const auto &[hubId, hub] : mIdToEmbeddedHub) {
    for (const auto &[endpointId, endpoint] : hub.idToEndpoint)
      endpoints.push_back(endpoint.id);
  }
  mIdToEmbeddedHub.clear();

  // For each host hub, close all sessions and send all removed endpoints.
  for (const auto &[hubId, hub] : mIdToHostHub) {
    ::android::base::ScopedLockAssertion lockAssertion(hub->mManager.mLock);
    for (const auto &[sessionId, session] : hub->mIdToSession)
      hub->mCallback->onCloseEndpointSession(sessionId, Reason::HUB_RESET);
    hub->mCallback->onEndpointStopped(endpoints, Reason::HUB_RESET);
  }
}

void MessageHubManager::addEmbeddedHub(const HubInfo &hub) {
  std::lock_guard lock(mLock);
  if (!mIdToEmbeddedHubReady) {
    LOGW("Skipping embedded hub registration before initEmbeddedState()");
    return;
  }
  if (mIdToEmbeddedHub.count(hub.hubId)) return;
  mIdToEmbeddedHub[hub.hubId].info = hub;
}

void MessageHubManager::removeEmbeddedHub(int64_t id) {
  std::lock_guard lock(mLock);

  // Get the list of endpoints being removed and remove the hub.
  std::vector<EndpointId> endpoints;
  auto it = mIdToEmbeddedHub.find(id);
  if (it == mIdToEmbeddedHub.end()) return;
  for (const auto &[endpointId, info] : it->second.idToEndpoint)
    endpoints.push_back(info.id);
  mIdToEmbeddedHub.erase(it);

  // For each host hub, determine which sessions if any are now closed and send
  // notifications as appropriate. Also send the list of removed endpoints.
  for (auto &[hostHubId, hub] : mIdToHostHub) {
    ::android::base::ScopedLockAssertion lockAssertion(hub->mManager.mLock);
    std::vector<uint16_t> closedSessions;
    for (const auto &[sessionId, session] : hub->mIdToSession) {
      if (session.mEmbeddedEndpoint.hubId == id) {
        hub->mCallback->onCloseEndpointSession(sessionId, Reason::HUB_RESET);
        closedSessions.push_back(sessionId);
      }
    }
    for (auto session : closedSessions) hub->mIdToSession.erase(session);
    hub->mCallback->onEndpointStopped(endpoints, Reason::HUB_RESET);
  }
}

std::vector<HubInfo> MessageHubManager::getEmbeddedHubs() const {
  std::lock_guard lock(mLock);
  std::vector<HubInfo> hubs;
  for (const auto &[id, hub] : mIdToEmbeddedHub) hubs.push_back(hub.info);
  return hubs;
}

void MessageHubManager::addEmbeddedEndpoint(const EndpointInfo &endpoint) {
  std::lock_guard lock(mLock);
  if (!mIdToEmbeddedHubReady) {
    LOGW("Skipping embedded endpoint registration before initEmbeddedState()");
    return;
  }
  addEmbeddedEndpointLocked(endpoint);
  for (auto &[hostHubId, hub] : mIdToHostHub) {
    ::android::base::ScopedLockAssertion lockAssertion(hub->mManager.mLock);
    hub->mCallback->onEndpointStarted({endpoint});
  }
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

void MessageHubManager::removeEmbeddedEndpoint(const EndpointId &id) {
  std::lock_guard lock(mLock);
  auto hubIt = mIdToEmbeddedHub.find(id.hubId);
  if (hubIt == mIdToEmbeddedHub.end()) return;
  if (!hubIt->second.idToEndpoint.erase(id.id)) return;

  // For each host hub, determine which sessions if any are now closed and send
  // notifications as appropriate. Also send the removed endpoint notification.
  for (auto &[hostHubId, hub] : mIdToHostHub) {
    ::android::base::ScopedLockAssertion lockAssertion(hub->mManager.mLock);
    std::vector<uint16_t> closedSessions;
    for (const auto &[sessionId, session] : hub->mIdToSession) {
      if (session.mEmbeddedEndpoint == id) {
        hub->mCallback->onCloseEndpointSession(sessionId,
                                               Reason::ENDPOINT_GONE);
        closedSessions.push_back(sessionId);
      }
    }
    for (auto session : closedSessions) hub->mIdToSession.erase(session);
    hub->mCallback->onEndpointStopped({id}, Reason::ENDPOINT_GONE);
  }
}

void MessageHubManager::onClientDeath(void *cookie) {
  auto *cookieData = reinterpret_cast<HostHub::DeathRecipientCookie *>(cookie);
  LOGW("Process hosting hub %" PRId64 " died", cookieData->hubId);
  MessageHubManager *manager = cookieData->manager;
  std::shared_ptr<HostHub> hub = manager->getHostHub(cookieData->hubId);
  // NOTE: if IEndpointCommunication.unregister() was called simultaneously, hub
  // may be null or unlinkFromManager() may fail.
  if (hub) {
    manager->mHostHubDownCb([hubPtr = hub.get()]() -> pw::Result<int64_t> {
      PW_TRY(hubPtr->unlinkFromManager());
      return hubPtr->id();
    });
  }
}

void MessageHubManager::addEmbeddedEndpointLocked(
    const EndpointInfo &endpoint) {
  auto it = mIdToEmbeddedHub.find(endpoint.id.hubId);
  if (it == mIdToEmbeddedHub.end()) {
    LOGW("Could not find hub %" PRId64 " for endpoint %" PRId64,
         endpoint.id.hubId, endpoint.id.id);
    return;
  }
  it->second.idToEndpoint.insert({endpoint.id.id, endpoint});
}

pw::Status MessageHubManager::embeddedEndpointExistsLocked(
    const EndpointId &id) {
  auto hubIt = mIdToEmbeddedHub.find(id.hubId);
  if (hubIt != mIdToEmbeddedHub.end()) {
    auto it = hubIt->second.idToEndpoint.find(id.id);
    if (it != hubIt->second.idToEndpoint.end()) return pw::OkStatus();
  }
  LOGW("Could not find remote endpoint (%" PRId64 ", %" PRId64 ")", id.hubId,
       id.id);
  return pw::Status::NotFound();
}

}  // namespace android::hardware::contexthub::common::implementation
