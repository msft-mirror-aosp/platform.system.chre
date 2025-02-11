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

#include "chre/core/event_loop_manager.h"
#include "chre/util/macros.h"
#include "chre_api/chre/common.h"
#include "chre_api/chre/event.h"
#include "chre_api/chre/msg.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

using ::chre::EventLoopManager;
using ::chre::EventLoopManagerSingleton;
using ::chre::Nanoapp;

DLL_EXPORT bool chreMsgGetEndpointInfo(uint64_t hubId, uint64_t endpointId,
                                       struct chreMsgEndpointInfo *info) {
#ifdef CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED
  return info != nullptr && EventLoopManagerSingleton::get()
                                ->getChreMessageHubManager()
                                .getEndpointInfo(hubId, endpointId, *info);
#else
  UNUSED_VAR(hubId);
  UNUSED_VAR(endpointId);
  UNUSED_VAR(info);
  return false;
#endif  // CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED
}

DLL_EXPORT bool chreMsgConfigureEndpointReadyEvents(uint64_t hubId,
                                                    uint64_t endpointId,
                                                    bool enable) {
#ifdef CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED
  Nanoapp *nanoapp = EventLoopManager::validateChreApiCall(__func__);
  return EventLoopManagerSingleton::get()
      ->getChreMessageHubManager()
      .configureReadyEvents(nanoapp->getInstanceId(), nanoapp->getAppId(),
                            hubId, endpointId,
                            /* serviceDescriptor= */ nullptr, enable);
#else
  UNUSED_VAR(hubId);
  UNUSED_VAR(endpointId);
  UNUSED_VAR(enable);
  return false;
#endif  // CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED
}

DLL_EXPORT bool chreMsgConfigureServiceReadyEvents(
    uint64_t hubId, const char *serviceDescriptor, bool enable) {
#ifdef CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED
  Nanoapp *nanoapp = EventLoopManager::validateChreApiCall(__func__);
  return EventLoopManagerSingleton::get()
      ->getChreMessageHubManager()
      .configureReadyEvents(nanoapp->getInstanceId(), nanoapp->getAppId(),
                            hubId,
                            /* endpointId= */ CHRE_MSG_ENDPOINT_ID_INVALID,
                            serviceDescriptor, enable);
#else
  UNUSED_VAR(hubId);
  UNUSED_VAR(serviceDescriptor);
  UNUSED_VAR(enable);
  return false;
#endif  // CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED
}

DLL_EXPORT bool chreMsgSessionGetInfo(uint16_t sessionId,
                                      struct chreMsgSessionInfo *info) {
#ifdef CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED
  Nanoapp *nanoapp = EventLoopManager::validateChreApiCall(__func__);
  return info != nullptr &&
         EventLoopManagerSingleton::get()
             ->getChreMessageHubManager()
             .getSessionInfo(nanoapp->getAppId(), sessionId, *info);
#else
  UNUSED_VAR(sessionId);
  UNUSED_VAR(info);
  return false;
#endif  // CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED
}

DLL_EXPORT bool chreMsgPublishServices(
    const struct chreMsgServiceInfo *services, size_t numServices) {
#ifdef CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED
  Nanoapp *nanoapp = EventLoopManager::validateChreApiCall(__func__);
  return EventLoopManagerSingleton::get()
      ->getChreMessageHubManager()
      .publishServices(nanoapp->getAppId(), services, numServices);
#else
  UNUSED_VAR(services);
  UNUSED_VAR(numServices);
  return false;
#endif  // CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED
}

DLL_EXPORT bool chreMsgSessionOpenAsync(uint64_t hubId, uint64_t endpointId,
                                        const char *serviceDescriptor) {
#ifdef CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED
  Nanoapp *nanoapp = EventLoopManager::validateChreApiCall(__func__);
  return EventLoopManagerSingleton::get()
      ->getChreMessageHubManager()
      .openDefaultSessionAsync(nanoapp->getAppId(), hubId, endpointId,
                               serviceDescriptor);
#else
  UNUSED_VAR(hubId);
  UNUSED_VAR(endpointId);
  UNUSED_VAR(serviceDescriptor);
  return false;
#endif  // CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED
}

DLL_EXPORT bool chreMsgSessionCloseAsync(uint16_t sessionId) {
#ifdef CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED
  Nanoapp *nanoapp = EventLoopManager::validateChreApiCall(__func__);
  return EventLoopManagerSingleton::get()
      ->getChreMessageHubManager()
      .closeSession(nanoapp->getAppId(), sessionId);
#else
  UNUSED_VAR(sessionId);
  return false;
#endif  // CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED
}

DLL_EXPORT bool chreMsgSend(
    void *message, size_t messageSize, uint32_t messageType, uint16_t sessionId,
    uint32_t messagePermissions, chreMessageFreeFunction *freeCallback) {
#ifdef CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED
  Nanoapp *nanoapp = EventLoopManager::validateChreApiCall(__func__);
  return EventLoopManagerSingleton::get()
      ->getChreMessageHubManager()
      .sendMessage(message, messageSize, messageType, sessionId,
                   messagePermissions, freeCallback, nanoapp->getAppId());
#else
  UNUSED_VAR(message);
  UNUSED_VAR(messageSize);
  UNUSED_VAR(messageType);
  UNUSED_VAR(sessionId);
  UNUSED_VAR(messagePermissions);
  UNUSED_VAR(freeCallback);
  return false;
#endif  // CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED
}
