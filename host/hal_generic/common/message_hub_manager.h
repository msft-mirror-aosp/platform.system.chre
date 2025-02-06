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
    ~HostHub() = default;

    /**
     * Adds an endpoint to this message hub
     *
     * @param info Description of the endpoint
     * @return pw::OkStatus() on success
     */
    pw::Status addEndpoint(const EndpointInfo &info) EXCLUDES(mManager.mLock);

    /**
     * Removes an endpoint from this message hub
     *
     * @param info Id of endpoint to remove
     * @return List of sessions to prune on success
     */
    pw::Result<std::vector<uint16_t>> removeEndpoint(const EndpointId &info)
        EXCLUDES(mManager.mLock);

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
     * The session is pending until updated by the destination endpoint.
     *
     * @param hostEndpoint The id of an endpoint hosted by this hub
     * @param embeddedEndpoint The id of the embedded endpoint
     * @param sessionId The id to be used for this session. Must be in the range
     * allocated to this hub
     * @return On success, true if the client should be notified that a session
     * with the same id has been closed.
     */
    pw::Result<bool> openSession(const EndpointId &hostEndpoint,
                                 const EndpointId &embeddedEndpoint,
                                 uint16_t sessionId) EXCLUDES(mManager.mLock);

    /**
     * Acks a pending session.
     *
     * @param id Session id
     * @return pw::OkStatus() on success
     */
    pw::Status ackSession(uint16_t id) EXCLUDES(mManager.mLock);

    /**
     * Checks that a session is open.
     *
     * @param id Session id
     * @return pw::OkStatus() on success
     */
    pw::Status checkSessionOpen(uint16_t id) EXCLUDES(mManager.mLock);

    /**
     * Removes the given session and any local and global mappings
     *
     * @param id The session id
     * @return pw::OkStatus() on success
     */
    pw::Status closeSession(uint16_t id) EXCLUDES(mManager.mLock);

    /**
     * Unregisters this HostHub.
     *
     * @return pw::OkStatus() if the host hub was successfully initialized and
     * not yet unregistered.
     */
    pw::Status unregister() EXCLUDES(mManager.mLock);

    /**
     * Returns the list of endpoints registered on this hub.
     */
    std::vector<EndpointInfo> getEndpoints() const;

    /** Returns the callback for the associated client */
    std::shared_ptr<IEndpointCallback> callback() const {
      return mCallback;
    }

    /**
     * Returns the Message Hub info
     */
    const HubInfo &info() const {
      return kInfo;
    }

    /**
     * Returns the registered id of this message hub.
     */
    int64_t id() const {
      return kInfo.hubId;
    }

   private:
    friend class MessageHubManager;

    // Reresents a session between a host and embedded endpoint.
    //
    // A Session is created on an openSession() request (triggered either by a
    // local or remote endpoint) with mPendingDestination unset via a call to
    // ackSession*() from the destination endpoint. For Sessions started by
    // embedded endpoints, an additional ackSession*() must be received from the
    // CHRE MessageRouter after passing it the ack from the destination host
    // endpoint. This unsets mPendingMessageRouter. A session is only open for
    // messages once both mPendingDestination and mPendingMessageRouter are
    // unset.
    struct Session {
      EndpointId mHostEndpoint;
      EndpointId mEmbeddedEndpoint;
      bool mPendingDestination = true;
      bool mPendingMessageRouter;

      Session(const EndpointId &hostEndpoint,
              const EndpointId &embeddedEndpoint, bool hostInitiated)
          : mHostEndpoint(hostEndpoint),
            mEmbeddedEndpoint(embeddedEndpoint),
            mPendingMessageRouter(!hostInitiated) {}
    };

    // Cookie associated with each registered client callback.
    struct DeathRecipientCookie {
      MessageHubManager *manager;
      int64_t hubId;
    };

    static constexpr uint16_t kSessionIdMaxRange = 1024;

    static constexpr int64_t kHubIdInvalid = 0;

    HostHub(MessageHubManager &manager,
            std::shared_ptr<IEndpointCallback> callback, const HubInfo &info);

    // Unlinks this hub from the manager, destroying internal references.
    // Propagates the unlinking to CHRE. If already unlinked, returns early with
    // error.
    pw::Status unlinkFromManager() EXCLUDES(mManager.mLock);

    // Returns pw::OkStatus() if the hub is in a valid state.
    pw::Status checkValidLocked() REQUIRES(mManager.mLock);

    // Returns pw::OkStatus() if the given endpoint exists on this hub.
    pw::Status endpointExistsLocked(const EndpointId &id)
        REQUIRES(mManager.mLock);

    // Returns pw::OkStatus() if the session id is in range for this hub.
    bool sessionIdInRangeLocked(uint16_t id) REQUIRES(mManager.mLock);

    // Returns a pointer to the session with given id.
    pw::Result<Session *> getSessionLocked(uint16_t id)
        REQUIRES(mManager.mLock);

    MessageHubManager &mManager;
    std::shared_ptr<IEndpointCallback> mCallback;  // Callback to client.
    DeathRecipientCookie *mCookie;  // Death cookie associated with mCallback.
    const HubInfo kInfo;            // Details of this hub.

    // Used to lookup a host endpoint.
    std::unordered_map<int64_t, EndpointInfo> mIdToEndpoint
        GUARDED_BY(mManager.mLock);

    // Used to lookup state for sessions including an endpoint on this hub.
    std::unordered_map<uint16_t, Session> mIdToSession
        GUARDED_BY(mManager.mLock);

    // Session id ranges allocated to this HostHub. The ranges are stored as a
    // pair of the lowest and highest id in the range.
    std::vector<std::pair<uint16_t, uint16_t>> mSessionIdRanges
        GUARDED_BY(mManager.mLock);

    // Set in unlinkFromManager().
    bool mUnlinked GUARDED_BY(mManager.mLock) = false;
  };

  // Callback registered to pass up the id of a host hub which disconnected.
  using HostHubDownCb =
      std::function<void(std::function<pw::Result<int64_t>()> unlinkFn)>;

  // The base session id for sessions initiated from host endpoints.
  static constexpr uint16_t kHostSessionIdBase = 0x8000;

  explicit MessageHubManager(HostHubDownCb cb);
  ~MessageHubManager() = default;

  /**
   * Registers a new client, creating a HostHub instance for it
   *
   * @param callback Interface for communicating with the client
   * @param info Details of the hub being registered
   * @return On success, shared_ptr to the HostHub instance
   */
  pw::Result<std::shared_ptr<HostHub>> createHostHub(
      std::shared_ptr<IEndpointCallback> callback, const HubInfo &info)
      EXCLUDES(mLock);

  /**
   * Retrieves a HostHub instance given its id
   *
   * @param id The HostHub id
   * @return shared_ptr to the HostHub instance, nullptr if not found
   */
  std::shared_ptr<HostHub> getHostHub(int64_t id) EXCLUDES(mLock);

  /**
   * Apply the given function to each host hub.
   *
   * @param fn The function to apply.
   */
  void forEachHostHub(std::function<void(HostHub &hub)> fn) EXCLUDES(mLock);

  /**
   * Wipes and marks the embedded state cache ready.
   *
   * This should only be called once during startup as it invalidates session
   * state (i.e. existing sessions will be pruned).
   */
  void initEmbeddedState() EXCLUDES(mLock);

  /**
   * Clears cache of embedded state and closes all sessions.
   *
   * Called on CHRE disconnection. Invalidates the cache. initEmbeddedState()
   * must be called again before sessions can be established.
   */
  void clearEmbeddedState() EXCLUDES(mLock);

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

  // Invoked on client death. Cleans up references to the client.
  static void onClientDeath(void *cookie);

  // Adds an embedded endpoint to the cache.
  void addEmbeddedEndpointLocked(const EndpointInfo &endpoint) REQUIRES(mLock);

  // Returns pw::OkStatus() if the given embedded endpoint is in the cache.
  pw::Status embeddedEndpointExistsLocked(const EndpointId &id) REQUIRES(mLock);

  // Callback to pass up the id of a host hub for a client that disconnected.
  HostHubDownCb mHostHubDownCb;

  // Death recipient handling clients' disconnections.
  ndk::ScopedAIBinder_DeathRecipient mDeathRecipient;

  // Guards hub, endpoint, and session state.
  mutable std::mutex mLock;

  // Map of EmbeddedHubs.
  std::unordered_map<int64_t, EmbeddedHub> mIdToEmbeddedHub GUARDED_BY(mLock);

  // Map of HostHubs for registered IContextHub V4+ clients.
  std::unordered_map<int64_t, std::shared_ptr<HostHub>> mIdToHostHub
      GUARDED_BY(mLock);

  // Next session id from which to allocate ranges.
  uint16_t mNextSessionId GUARDED_BY(mLock) = kHostSessionIdBase;

  // True if the embedded hub cache is initialized.
  bool mIdToEmbeddedHubReady GUARDED_BY(mLock) = false;
};

}  // namespace android::hardware::contexthub::common::implementation
