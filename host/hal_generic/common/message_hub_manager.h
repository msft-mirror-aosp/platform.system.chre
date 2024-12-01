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

#pragma once

#include <unistd.h>

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <aidl/android/hardware/contexthub/BnContextHub.h>
#include <android-base/thread_annotations.h>

#include "pw_result/result.h"
#include "pw_status/status.h"

namespace android::hardware::contexthub::common::implementation {

using ::aidl::android::hardware::contexthub::EndpointId;
using ::aidl::android::hardware::contexthub::EndpointInfo;
using ::aidl::android::hardware::contexthub::HubInfo;
using ::aidl::android::hardware::contexthub::IEndpointCallback;

/**
 * Stores host and embedded MessageHub objects and maintains global mappings.
 */
class MessageHubManager {
 public:
  /**
   * Represents a host-side MessageHub. Clients of the IContextHub (V4+)
   * interface each get a HostHub instance.
   */
  class HostHub {
   public:
    ~HostHub();

    /**
     * Sets the callback for sending endpoint events back to the HAL client
     *
     * @param callback The callback provided by the client
     * @return pw::OkStatus() on success.
     */
    pw::Status setCallback(std::shared_ptr<IEndpointCallback> callback)
        EXCLUDES(mManager.mLock);

    /**
     * Returns the callback registered in setCallback()
     *
     * @return The previously registered callback
     */
    std::shared_ptr<IEndpointCallback> getCallback() const
        EXCLUDES(mManager.mLock);

    /**
     * Adds an endpoint to this message hub
     *
     * @param self Self-reference for mapping from hub id
     * @param info Description of the endpoint
     * @return pw::OkStatus() on success
     */
    pw::Status addEndpoint(std::weak_ptr<HostHub> self,
                           const EndpointInfo &info) EXCLUDES(mManager.mLock);

    /**
     * Removes an endpoint from this message hub
     *
     * @param info Id of endpoint to remove
     * @return pw::OkStatus() on success
     */
    pw::Status removeEndpoint(const EndpointId &info) EXCLUDES(mManager.mLock);

    /**
     * Reserves a session id range to be used by this message hub
     *
     * @param size The size of this range, max 1024
     * @return A pair of the smallest and largest id in the range on success
     */
    pw::Result<std::pair<uint16_t, uint16_t>> reserveSessionIdRange(
        uint16_t size) EXCLUDES(mManager.mLock);

    /**
     * Opens a session between the given endpoints with given session id
     *
     * Updates state for this hub as well as global state for lookups
     *
     * @param self Self-reference to be stored in session state
     * @param localId The id of an endpoint hosted by this hub
     * @param remoteId The id of the remote endpoint
     * @param sessionId The id to be used for this session. Must be in the range
     * allocated to this hub
     * @param hostInitiated true if the session was initiated by a host endpoint
     * @return pw::OkStatus() on success
     */
    pw::Status openSession(std::weak_ptr<HostHub> self,
                           const EndpointId &localId,
                           const EndpointId &remoteId, uint16_t sessionId,
                           bool hostInitiated) EXCLUDES(mManager.mLock);

    /**
     * Removes the given session and any local and global mappings
     *
     * @param id The session id
     * @return pw::OkStatus() on success
     */
    pw::Status closeSession(uint16_t id) EXCLUDES(mManager.mLock);

    /**
     * Returns the EndpointInfo for an endpoint on this hub
     *
     * @param id The endpoint id
     * @return the EndpointInfo for the given id if present
     */
    pw::Result<EndpointInfo> getEndpointById(int64_t id)
        EXCLUDES(mManager.mLock);

    /**
     * Returns the EndpointInfo on this hub for an active session
     *
     * @param id The session id
     * @return the EndpointInfo for this side of the session
     */
    pw::Result<EndpointInfo> getEndpointBySessionId(uint16_t id)
        EXCLUDES(mManager.mLock);

    /**
     * Returns the registered id of this message hub.
     *
     * @return kId
     */
    int64_t id() const;

   private:
    friend class MessageHubManager;

    // Cookie associated with each registered client callback.
    struct DeathRecipientCookie {
      MessageHubManager *manager;
      pid_t pid;
    };

    static constexpr uint16_t kSessionIdMaxRange = 1024;

    static constexpr int64_t kHubIdInvalid = 0;

    HostHub(MessageHubManager &manager, pid_t pid)
        : mManager(manager), kPid(pid) {}

    // Unlinks this hub from the manager, destroying internal references.
    // Returns the id so that it can be propagated to CHRE.
    int64_t unlinkFromManager() EXCLUDES(mManager.mLock);

    // Unlink the current callback from the manager's death recipient.
    void unlinkCallbackIfNecessaryLocked() REQUIRES(mManager.mLock);

    // Returns pw::OkStatus() if the hub is in a valid state.
    pw::Status checkValidLocked() REQUIRES(mManager.mLock);

    MessageHubManager &mManager;
    const pid_t kPid;

    // Hub id, set when the first endpoint is registered.
    int64_t kId GUARDED_BY(mManager.mLock) = kHubIdInvalid;

    // Callback to HAL client.
    std::shared_ptr<IEndpointCallback> mCallback GUARDED_BY(mManager.mLock);

    // Cookie associated with mCallback.
    DeathRecipientCookie *mCookie GUARDED_BY(mManager.mLock);

    // Used to lookup a host endpoint. Owns the associated EndpointInfo.
    std::unordered_map<int64_t, std::shared_ptr<EndpointInfo>> mIdToEndpoint
        GUARDED_BY(mManager.mLock);

    // Used to lookup the EndpointInfo for the host endpoint associated with a
    // session, e.g. to check permissions for an incoming message.
    std::unordered_map<uint16_t, std::weak_ptr<EndpointInfo>>
        mSessionIdToEndpoint GUARDED_BY(mManager.mLock);

    // Session id ranges allocated to this HostHub. The ranges are stored as a
    // pair of the lowest and highest id in the range.
    std::vector<std::pair<uint16_t, uint16_t>> mSessionIdRanges
        GUARDED_BY(mManager.mLock);

    // Set in unlinkFromManager().
    bool mUnlinked GUARDED_BY(mManager.mLock) = false;
  };

  // Callback registered to pass up the id of a host hub which disconnected.
  using HostHubDownCb = std::function<void(int64_t hubId)>;

  explicit MessageHubManager(HostHubDownCb cb);
  ~MessageHubManager() = default;

  /**
   * Retrieves the HostHub instance for the calling process
   *
   * This API should be used for any HostHub lookup coming from the
   * IContextHub interface. The first call to this API by any client process
   * will trigger the creation of a HostHub for that client.
   *
   * @param pid The caller's system process id
   * @return shared_ptr to the HostHub instance
   */
  std::shared_ptr<HostHub> getHostHubByPid(pid_t pid) EXCLUDES(mLock);

  /**
   * Retrieves the HostHub instance for the given EndpointId
   *
   * @param id The endpoint id hosted by the returned hub
   * @return shared_ptr to the HostHub instance
   */
  std::shared_ptr<HostHub> getHostHubByEndpointId(const EndpointId &id)
      EXCLUDES(mLock);

  /**
   * Retrieves the HostHub instance for the given session id
   *
   * @param id The session id whose host endpoint is in the returned hub
   * @return shared_ptr to the HostHub instance
   */
  std::shared_ptr<HostHub> getHostHubBySessionId(uint16_t id) EXCLUDES(mLock);

  /**
   * Updates the cache of embedded hubs and endpoints
   *
   * Ignores existing hubs and removes hubs which no longer exist
   *
   * @param hubs The list of message hubs
   * @param endpoints The list of endpoints
   */
  void updateEmbeddedHubsAndEndpoints(
      const std::vector<HubInfo> &hubs,
      const std::vector<EndpointInfo> &endpoints) EXCLUDES(mLock);

  /**
   * Adds the given hub to the cache
   *
   * Ignored if the hub already exists
   *
   * @param hub The hub to add
   */
  void addEmbeddedHub(const HubInfo &hub) EXCLUDES(mLock);

  /**
   * Removes the hub with given id from the cache
   *
   * @param id The id of the hub to remove
   */
  void removeEmbeddedHub(int64_t id) EXCLUDES(mLock);

  /**
   * Returns the cached list of embedded message hubs
   *
   * @return HubInfo for every embedded message hub
   */
  std::vector<HubInfo> getEmbeddedHubs() const EXCLUDES(mLock);

  /**
   * Adds an embedded endpoint to the cache
   *
   * Ignored if the endpoint already exists
   *
   * @param endpoint The endpoint to add
   */
  void addEmbeddedEndpoint(const EndpointInfo &endpoint);

  /**
   * Removes an embedded endpoint from the cache
   *
   * @param id The id of the endpoint to remove
   */
  void removeEmbeddedEndpoint(const EndpointId &endpoint);

  /**
   * Returns a list of embedded endpoints
   *
   * @return EndpointInfo for every embedded endpoint
   */
  std::vector<EndpointInfo> getEmbeddedEndpoints() const EXCLUDES(mLock);

 private:
  // Callback invoked when a client goes down.
  using UnlinkToDeathFn = std::function<bool(
      const std::shared_ptr<IEndpointCallback> &callback, void *cookie)>;

  // Represents an embedded MessageHub. Stores the hub details as well as a map
  // of all endpoints hosted by the hub.
  struct EmbeddedHub {
    std::unordered_map<int64_t, EndpointInfo> idToEndpoint;
    HubInfo info;
  };

  // The hub id reserved for the ContextHub service.
  static constexpr int64_t kContextHubServiceHubId = 0x416e64726f696400;

  // The Linux uid of the system_server.
  static constexpr uid_t kSystemServerUid = 1000;

  // The base session id for sessions initiated from host endpoints.
  static constexpr uint16_t kHostSessionIdBase = 0x8000;

  // Invoked on client death. Cleans up references to the client.
  static void onClientDeath(void *cookie);

  // Adds an embedded endpoint to the cache.
  void addEmbeddedEndpointLocked(const EndpointInfo &endpoint) REQUIRES(mLock);

  // Returns true if the embedded endpoint with given id is in the cache.
  bool findEmbeddedEndpointLocked(const EndpointId &id) REQUIRES(mLock);

  // Callback to pass up the id of a host hub for a client that disconnected.
  HostHubDownCb mHostHubDownCb;

  // Death recipient handling clients' disconnections.
  ndk::ScopedAIBinder_DeathRecipient mDeathRecipient;

  // Guards hub, endpoint, and session state.
  mutable std::mutex mLock;

  // Map of EmbeddedHubs.
  std::unordered_map<int64_t, EmbeddedHub> mIdToEmbeddedHub GUARDED_BY(mLock);

  // Used to look up the HostHub associated with the client on IContextHub
  // calls. Has shared ownership of the HostHub instance for the lifetime of the
  // client.
  std::unordered_map<pid_t, std::shared_ptr<HostHub>> mPidToHostHub
      GUARDED_BY(mLock);

  // Used when an embedded endpoint wants to start a session with an endpoint
  // hosted by a specific HostHub.
  std::unordered_map<int64_t, std::weak_ptr<HostHub>> mIdToHostHub
      GUARDED_BY(mLock);

  // Used to lookup the host endpoint to receive a message on an endpoint
  // session.
  std::unordered_map<uint16_t, std::weak_ptr<HostHub>> mSessionIdToHostHub
      GUARDED_BY(mLock);

  // Next session id from which to allocate ranges.
  uint16_t mNextSessionId GUARDED_BY(mLock) = kHostSessionIdBase;
};

}  // namespace android::hardware::contexthub::common::implementation
