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

#ifndef CHRE_CORE_CHRE_MESSAGE_HUB_MANAGER_H_
#define CHRE_CORE_CHRE_MESSAGE_HUB_MANAGER_H_

#ifdef CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED

#include "chre/core/event_loop_common.h"
#include "chre/platform/mutex.h"
#include "chre/util/dynamic_vector.h"
#include "chre/util/non_copyable.h"
#include "chre/util/system/callback_allocator.h"
#include "chre/util/system/message_common.h"
#include "chre/util/system/message_router.h"
#include "chre/util/unique_ptr.h"
#include "chre_api/chre.h"
#include "pw_containers/vector.h"

#include <cinttypes>
#include <cstdint>
#include <optional>

namespace chre {

//! Manager class for the CHRE Message Hub.
class ChreMessageHubManager
    : public NonCopyable,
      public message::MessageRouter::MessageHubCallback {
 public:
  //! The ID of the CHRE MessageHub
  constexpr static message::MessageHubId kChreMessageHubId = CHRE_PLATFORM_ID;

  //! Constructor for the ChreMessageHubManager
  ChreMessageHubManager();

  //! Initializes the ChreMessageHubManager
  void init();

  //! @return the MessageHub for the CHRE Message Hub
  message::MessageRouter::MessageHub &getMessageHub() {
    return mChreMessageHub;
  }

  //! Gets endpoint information for the given hub and endpoint IDs.
  //! @return whether the endpoint information was successfully populated.
  bool getEndpointInfo(message::MessageHubId hubId,
                       message::EndpointId endpointId,
                       chreMsgEndpointInfo &info);

  //! Configures ready events for the given endpoint or service.
  //! This function must be called from the event loop thread.
  //! @return true if the ready events were configured successfully, false
  //! otherwise.
  bool configureReadyEvents(uint16_t nanoappInstanceId,
                            message::EndpointId fromEndpointId,
                            message::MessageHubId hubId,
                            message::EndpointId endpointId,
                            const char *serviceDescriptor, bool enable);

  //! Gets session information for the given session ID.
  //! @return whether the session information was successfully populated.
  bool getSessionInfo(message::EndpointId fromEndpointId,
                      message::SessionId sessionId, chreMsgSessionInfo &info);

  //! Opens a session from the given endpoint to the other endpoint in an
  //! asynchronous manner.
  //! @return true if the session was opened successfully, false otherwise
  bool openSessionAsync(message::EndpointId fromEndpointId,
                        message::MessageHubId toHubId,
                        message::EndpointId toEndpointId,
                        const char *serviceDescriptor);

  //! Opens a session from the given endpoint to the other endpoint in an
  //! asynchronous manner. Either toHubId, toEndpointId, or serviceDescriptor
  //! can be set to CHRE_MSG_HUB_ID_INVALID, ENDPOINT_ID_INVALID, or nullptr,
  //! respectively. If they are set to invalid values, the default values will
  //! be used if available. If no default values are available, the session will
  //! not be opened and this function will return false.
  //! @return true if the session was opened successfully, false otherwise
  bool openDefaultSessionAsync(message::EndpointId fromEndpointId,
                               message::MessageHubId toHubId,
                               message::EndpointId toEndpointId,
                               const char *serviceDescriptor);

  //! Closes the session and verifies the fromEndpointId is a member of the
  //! session.
  //! @return true if the session was closed successfully, false otherwise
  bool closeSession(message::EndpointId fromEndpointId,
                    message::SessionId sessionId);

  //! Sends a reliable message on the given session. If this function fails,
  //! the free callback will be called and it will return false.
  //! @return whether the message was successfully sent
  bool sendMessage(void *message, size_t messageSize, uint32_t messageType,
                   uint16_t sessionId, uint32_t messagePermissions,
                   chreMessageFreeFunction *freeCallback,
                   message::EndpointId fromEndpointId);

  //! Publishes a service from the given nanoapp.
  //! This function must be called from the event loop thread.
  //! @return true if the service was published successfully, false otherwise
  bool publishServices(message::EndpointId fromEndpointId,
                       const chreMsgServiceInfo *serviceInfos,
                       size_t numServices);

  //! Unregisters the given endpoint (nanoapp) from the MessageHub
  //! This will clean up all pending resources then unregister the endpoint
  //! from the MessageHub.
  void unregisterEndpoint(message::EndpointId endpointId);

  //! Cleans up all pending resources for the given endpoint (nanoapp).
  //! This should only be called from the event loop thread.
  void cleanupEndpointResources(message::EndpointId endpointId);

  //! Converts a message::EndpointType to a CHRE endpoint type
  //! @return the CHRE endpoint type
  chreMsgEndpointType toChreEndpointType(message::EndpointType type);

  //! Converts a message::Reason to a CHRE endpoint reason
  //! @return the CHRE endpoint reason
  chreMsgEndpointReason toChreEndpointReason(message::Reason reason);

 private:
  //! Data to be passed to the message callback
  struct MessageCallbackData {
    chreMsgMessageFromEndpointData messageToNanoapp;
    pw::UniquePtr<std::byte[]> data;
    uint64_t nanoappId;
  };

  //! Data to be passed to the message free callback
  struct MessageFreeCallbackData {
    chreMessageFreeFunction *freeCallback;
    uint64_t nanoappId;
  };

  //! Data to be passed to the session closed callback
  struct SessionCallbackData {
    chreMsgSessionInfo sessionData;
    bool isClosed;
    uint64_t nanoappId;
  };

  //! Data that represents a service published by a nanoapp
  struct NanoappServiceData {
    uint64_t nanoappId;
    chreMsgServiceInfo serviceInfo;
  };

  //! Data that represents a ready event configured for an endpoint or service
  struct EndpointReadyEventData {
    message::EndpointId fromEndpointId;
    message::MessageHubId messageHubId;
    message::EndpointId endpointId;
    const char *serviceDescriptor;
  };

  constexpr static size_t kMaxFreeCallbackRecords = 25;

  //! Callback to process message sent to a nanoapp - used by the event loop
  static void onMessageToNanoappCallback(
      SystemCallbackType type,
      UniquePtr<ChreMessageHubManager::MessageCallbackData> &&data);

  //! Callback to process session closed or opened event for a nanoapp - used
  //! by the event loop
  static void onSessionStateChangedCallback(
      SystemCallbackType type,
      UniquePtr<ChreMessageHubManager::SessionCallbackData> &&data);

  //! Callback called when a message is freed
  static void onMessageFreeCallback(std::byte *message, size_t length,
                                    MessageFreeCallbackData &&callbackData);

  //! Callback passed to deferCallback when handling a message free callback
  static void handleMessageFreeCallback(uint16_t type, void *data,
                                        void *extraData);

  //! Called on a state change for a session - open or close. If reason is
  //! not provided, the state change is open, else it is closed.
  void onSessionStateChanged(const message::Session &session,
                             std::optional<message::Reason> reason);

  //! Processes an endpoint ready event from MessageRouter. Can only be called
  //! from the event loop thread.
  void onEndpointReadyEvent(message::MessageHubId messageHubId,
                            message::EndpointId endpointId);

  //! @return The free callback record from the callback allocator.
  std::optional<CallbackAllocator<MessageFreeCallbackData>::CallbackRecord>
  getAndRemoveFreeCallbackRecord(void *ptr) {
    return mAllocator.GetAndRemoveCallbackRecord(ptr);
  }

  //! @return The first MessageHub ID for the given endpoint ID
  message::MessageHubId findDefaultMessageHubId(message::EndpointId endpointId);

  //! @return true if the nanoapp has a service with the given service
  //! descriptor in the legacy service descriptor format.
  bool doesNanoappHaveLegacyService(uint64_t nanoappId, uint64_t serviceId);

  //! @return true if the services are valid and can be published, false
  //! otherwise. Caller must hold mNanoappPublishedServicesMutex.
  bool validateServicesLocked(uint64_t nanoappId,
                              const chreMsgServiceInfo *serviceInfos,
                              size_t numServices);

  //! Searches for an endpoint with the given hub ID, endpoint ID, and service
  //! descriptor. The hubId can be MESSAGE_HUB_ID_ANY to search for the
  //! endpoint on any hub, the endpointId can be ENDPOINT_ID_ANY to search for
  //! the endpoint on any hub, or the service descriptor can be non-nullptr to
  //! search for any endpoint that has the service.
  //! @return the endpoint if found, std::nullopt otherwise.
  std::optional<message::Endpoint> searchForEndpoint(
      message::MessageHubId messageHubId, message::EndpointId endpointId,
      const char *serviceDescriptor);

  //! Removes the ready event request for the given endpoint or service.
  void disableReadyEvents(message::EndpointId fromEndpointId,
                          message::MessageHubId hubId,
                          message::EndpointId endpointId,
                          const char *serviceDescriptor);

  //! Definitions for MessageHubCallback
  //! @see MessageRouter::MessageHubCallback
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
  void onEndpointRegistered(message::MessageHubId messageHubId,
                            message::EndpointId endpointId) override;
  void onEndpointUnregistered(message::MessageHubId messageHubId,
                              message::EndpointId endpointId) override;

  //! The MessageHub for the CHRE
  message::MessageRouter::MessageHub mChreMessageHub;

  //! The vector of free callback records - used by the
  //! CallbackAllocator
  pw::Vector<CallbackAllocator<MessageFreeCallbackData>::CallbackRecord,
             kMaxFreeCallbackRecords>
      mFreeCallbackRecords;

  //! The allocator for message free callbacks - used when sending a message
  //! from a nanoapp with a free callback
  CallbackAllocator<MessageFreeCallbackData> mAllocator;

  //! Mutex to protect mNanoappPublishedServices
  Mutex mNanoappPublishedServicesMutex;

  //! The vector of services published by nanoapps
  DynamicVector<NanoappServiceData> mNanoappPublishedServices;

  //! The vector of ready event requests
  //! This should only be accessed from the event loop thread
  DynamicVector<EndpointReadyEventData> mEndpointReadyEventRequests;
};

}  // namespace chre

#endif  // CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED

#endif  // CHRE_CORE_CHRE_MESSAGE_HUB_MANAGER_H_
