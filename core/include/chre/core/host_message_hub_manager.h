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

#pragma once

#ifdef CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED

#include <optional>

#include "chre/platform/atomic.h"
#include "chre/platform/mutex.h"
#include "chre/util/memory_pool.h"
#include "chre/util/non_copyable.h"
#include "chre/util/system/message_common.h"
#include "chre/util/system/message_router.h"

#include "pw_allocator/unique_ptr.h"
#include "pw_containers/intrusive_list.h"
#include "pw_containers/vector.h"
#include "pw_function/function.h"
#include "pw_span/span.h"

#if !defined(CHRE_MESSAGE_ROUTER_MAX_HOST_HUBS)
#error "Must define maximum host message hubs for platform"
#elif defined(CHRE_MESSAGE_ROUTER_MAX_MESSAGE_HUBS) && \
    CHRE_MESSAGE_ROUTER_MAX_HOST_HUBS >= CHRE_MESSAGE_ROUTER_MAX_MESSAGE_HUBS
#error "Message hub limit must be greater than host message hub limit"
#endif  // !defined(CHRE_MESSAGE_ROUTER_MAX_HOST_HUBS)

#ifndef CHRE_MESSAGE_ROUTER_MAX_HOST_ENDPOINTS
#error "Must define maximum host endpoints for platform"
#endif  // CHRE_MESSAGE_ROUTER_MAX_HOST_ENDPOINTS

namespace chre {

/**
 * Manages the registration of host-side message hubs with MessageRouter and
 * routes messages between them.
 */
class HostMessageHubManager : public NonCopyable {
 public:
  /** Interface registered for routing communication to host hubs. */
  class HostCallback {
   public:
    virtual ~HostCallback() = default;

    /**
     * Notifies the HAL that the host message hub proxies have been reset.
     *
     * Invoked within MessageHubManager::reset().
     */
    virtual void onReset() = 0;

    /**
     * Notifies the HAL of a new embedded message hub.
     */
    virtual void onHubRegistered(const message::MessageHubInfo &hub) = 0;

    /**
     * Notifies the HAL of a new embedded endpoint.
     */
    virtual void onEndpointRegistered(
        message::MessageHubId hub, const message::EndpointInfo &endpoint) = 0;

    /**
     * Sends a message within a session.
     *
     * Invoked within MessageHubCallback::onMessageReceived().
     *
     * @param hub The destination hub id
     * @param session The session id
     * @param data Message data
     * @param type Message type
     * @param permissions Message permissiosn
     * @return true if the message was successfully sent
     */
    virtual bool onMessageReceived(message::MessageHubId hub,
                                   message::SessionId session,
                                   pw::UniquePtr<std::byte[]> &&data,
                                   uint32_t type, uint32_t permissions) = 0;
    /**
     * Sends a request to open a session with a host endpoint
     *
     * Invoked within MessageHubCallback::onSessionOpenRequest().
     *
     * @param session The session details
     */
    virtual void onSessionOpenRequest(const message::Session &session) = 0;

    /**
     * Sends a notification that a session has been accepted
     *
     * Invoked within MessageHubCallback::onSessionOpened().
     *
     * @param hub The id of the destination host hub
     * @param session The session id
     */
    virtual void onSessionOpened(message::MessageHubId hub,
                                 message::SessionId session) = 0;

    /**
     * Sends a notification that a session has been closed
     *
     * Invoked within MessageHubCallback::onSessionClosed().
     *
     * @param hub The id of the destination host hub
     * @param session The session id
     * @param reason The reason the session has been closed
     */
    virtual void onSessionClosed(message::MessageHubId hub,
                                 message::SessionId session,
                                 message::Reason reason) = 0;
  };

  HostMessageHubManager() = default;
  ~HostMessageHubManager() = default;

  /**
   * Initializes the interface for host communication
   *
   * Must be called exactly once before any other HostMessageHubManager APIs.
   *
   * @param cb Implementation of HostCallback
   */
  void onHostTransportReady(HostCallback &cb);

  /**
   * Resets host message hub state.
   *
   * Existing message hubs are cleared (see Hub::clear() below) though they
   * remain registered with MessageRouter. When the same hub is registered again
   * the same slot is re-activated.
   */
  void reset();

  /**
   * Registers a new host message hub
   *
   * @param info Details of the message hub
   */
  void registerHub(const message::MessageHubInfo &info);

  /**
   * Unregisters a host message hub
   *
   * @param id Id of the message hub
   */
  void unregisterHub(message::MessageHubId id);

  /**
   * Registers a host endpoint
   *
   * @param hubId Id of the owning message hub
   * @param info Details of the endpoint
   */
  void registerEndpoint(message::MessageHubId hubId,
                        const message::EndpointInfo &info);

  /**
   * Unregisters a host endpoint
   *
   * @param hubId Id of the owning message hub
   * @param id Id of the endpoint
   */
  void unregisterEndpoint(message::MessageHubId hubId, message::EndpointId id);

  /**
   * Requests the creation of a new session
   *
   * @param hubId Id of the host hub
   * @param endpointId Id of the host endpoint
   * @param destinationHubId Id of the destination hub
   * @param destinationEndpointId Id of the destination endpoint
   * @param sessionId Id of the new session
   * @param serviceDescriptor The protocol for the session
   */
  void openSession(message::MessageHubId hubId, message::EndpointId endpointId,
                   message::MessageHubId destinationHubId,
                   message::EndpointId destinationEndpointId,
                   message::SessionId sessionId, const char *serviceDescriptor);

  /**
   * Notifies that a new session has been accepted
   *
   * @param hubId Id of the sending host hub
   * @param sessionId Id of the new session
   */
  void ackSession(message::MessageHubId hubId, message::SessionId sessionId);

  /**
   * Notifies that a session has been closed / rejected
   *
   * @param hubId Id of the sending host hub
   * @param sessionId Id of the session
   * @param reason The reason for the closure / rejection
   */
  void closeSession(message::MessageHubId hubId, message::SessionId sessionId,
                    message::Reason reason);

  /**
   * Sends a message within a session
   *
   * @param hubId Id of the sending host hub
   * @param sessionId Id of the session
   * @param data Message data
   * @param type Message type
   * @param permissions Message permissions
   */
  void sendMessage(message::MessageHubId hubId, message::SessionId sessionId,
                   pw::UniquePtr<std::byte[]> &&data, uint32_t type,
                   uint32_t permissions);

 private:
  /**
   * Wrapper around EndpointInfo which can be allocated from a
   * pw::allocator::TypedPool and tracked per-hub in a pw::IntrusiveList.
   */
  struct Endpoint : public pw::IntrusiveList<Endpoint>::Item {
    message::EndpointInfo kInfo;
    explicit Endpoint(const message::EndpointInfo &info) : kInfo(info) {}
  };

  /**
   * Represents a host message hub. Registered with MessageRouter and stores the
   * returned MessageRouter::MessageHub. Stores the list of registered endpoints
   * for inspection by MessageRouter.
   *
   * The public APIs are expected to be called as a result of some host-side
   * operation with HostMessageHubManager::mHubsLock held.
   */
  class Hub : public NonCopyable,
              public message::MessageRouter::MessageHubCallback {
   public:
    /**
     * Either reactivates the hub matching info or creates and registers it.
     *
     * @param info Details of the host message hub
     * @param endpoints The list of endpoints to initialize the hub with.
     * Endpoints must have been allocated using mEndpointAllocator.
     * @return true on successful registration or reactivation
     */
    static bool restoreOrCreateLocked(const message::MessageHubInfo &info,
                                      pw::IntrusiveList<Endpoint> &endpoints);

    /** NOTE: Use restoreOrCreateLocked() */
    Hub(const char *name, pw::IntrusiveList<Endpoint> &endpoints);
    Hub(Hub &&) = delete;
    Hub &operator=(Hub &&) = delete;
    virtual ~Hub();

    /**
     * Marks the hub inactive and clears all endpoints.
     *
     * NOTE: This is done instead of destroying the hub instance, as
     * unregistering MessageHubs from MessageRouter is currently racy (i.e.
     * there is no way to know when it is safe to destroy the
     * MessageHubCallback). This isn't an issue, as we expect the set of host
     * hubs which may be registered at any point to be fixed. If a host hub
     * happens to disconnect, we can hold on to its Hub instance without
     * unregistering it.
     */
    void clear();

    void addEndpoint(const message::EndpointInfo &info);

    void removeEndpoint(message::EndpointId id);

    message::MessageRouter::MessageHub &getMessageHub() {
      return mMessageHub;
    }

   private:
    static constexpr size_t kNameMaxLen = 50;

    // Implementation of MessageRouter::MessageHubCallback;
    bool onMessageReceived(pw::UniquePtr<std::byte[]> &&data,
                           uint32_t messageType, uint32_t messagePermissions,
                           const message::Session &session,
                           bool sentBySessionInitiator) override;
    void onSessionOpenRequest(const message::Session &session) override;
    void onSessionOpened(const message::Session &session) override;
    void onSessionClosed(const message::Session &session,
                         message::Reason reason) override;
    void forEachEndpoint(const pw::Function<bool(const message::EndpointInfo &)>
                             &function) override;
    std::optional<message::EndpointInfo> getEndpointInfo(
        message::EndpointId endpointId) override;
    std::optional<message::EndpointId> getEndpointForService(
        const char *serviceDescriptor) override;
    bool doesEndpointHaveService(message::EndpointId endpointId,
                                 const char *serviceDescriptor) override;

    char kName[kNameMaxLen + 1];

    message::MessageRouter::MessageHub mMessageHub;
    AtomicBool mActive = false;

    // Guards mEndpoints. Must be the innermost lock.
    Mutex mEndpointsLock;
    pw::IntrusiveList<Endpoint> mEndpoints;
  };

  // Consumes and deallocates all entries in the list.
  static void deallocateEndpoints(pw::IntrusiveList<Endpoint> &endpoints);

  HostCallback *mCb;

  // Endpoint storage and allocator.
  // NOTE: This is only accessed on host-triggered invocations which take
  // mHubsLock, so additional synchronization is not required.
  MemoryPool<Endpoint, CHRE_MESSAGE_ROUTER_MAX_HOST_ENDPOINTS>
      mEndpointAllocator;

  // Guards mHubs. This lock is only safe to take when coming from an external
  // path, i.e. on message from the host. MessageRouter accesses Hub instances
  // directly, i.e. not through mHubs via the registered MessageHubCallback
  // interface.
  Mutex mHubsLock;
  pw::Vector<Hub, CHRE_MESSAGE_ROUTER_MAX_HOST_HUBS> mHubs;

  // Serializes embedded hub and endpoint state changes being sent to the host
  // with the operations in reset().
  Mutex mEmbeddedHubOpLock;
};

}  // namespace chre

#endif  // CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED
