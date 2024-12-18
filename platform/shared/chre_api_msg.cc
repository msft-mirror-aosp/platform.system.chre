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

DLL_EXPORT bool chreMsgGetEndpointInfo(uint64_t hubId, uint64_t endpointId,
                                       struct chreMsgEndpointInfo *info) {
  // TODO(b/371009029): Implement this.
  UNUSED_VAR(hubId);
  UNUSED_VAR(endpointId);
  UNUSED_VAR(info);
  return false;
}

DLL_EXPORT bool chreMsgConfigureEndpointReadyEvents(uint64_t hubId,
                                                    uint64_t endpointId,
                                                    bool enable) {
  // TODO(b/371009029): Implement this - requires MessageRouter changes (endpoint
  // lifecycle).
  UNUSED_VAR(hubId);
  UNUSED_VAR(endpointId);
  UNUSED_VAR(enable);
  return false;
}

DLL_EXPORT bool chreMsgConfigureServiceReadyEvents(
    uint64_t hubId, const char *serviceDescriptor, bool enable) {
  // TODO(b/371009029): Implement this - requires MessageRouter changes
  // (endpoint lifecycle).
  UNUSED_VAR(hubId);
  UNUSED_VAR(serviceDescriptor);
  UNUSED_VAR(enable);
  return false;
}

DLL_EXPORT bool chreMsgSessionGetInfo(uint16_t sessionId,
                                      struct chreMsgSessionInfo *info) {
  // TODO(b/371009029): Implement this.
  UNUSED_VAR(sessionId);
  UNUSED_VAR(info);
  return false;
}

DLL_EXPORT bool chreMsgPublishServices(
    const struct chreMsgServiceInfo *services, size_t numServices) {
  // TODO(b/371009029): Implement this - requires MessageRouter changes (service
  // integration).
  UNUSED_VAR(services);
  UNUSED_VAR(numServices);
  return false;
}

DLL_EXPORT bool chreMsgSessionOpenAsync(uint64_t hubId, uint64_t endpointId,
                                        const char *serviceDescriptor) {
  // TODO(b/371009029): Implement this - requires MessageRouter changes (service
  // integration).
  UNUSED_VAR(hubId);
  UNUSED_VAR(endpointId);
  UNUSED_VAR(serviceDescriptor);
  return false;
}

DLL_EXPORT bool chreMsgSessionCloseAsync(uint16_t sessionId) {
  // TODO(b/371009029): Implement this.
  UNUSED_VAR(sessionId);
  return false;
}

DLL_EXPORT bool chreMsgSend(
    void *message, size_t messageSize, uint32_t messageType, uint16_t sessionId,
    uint32_t messagePermissions, chreMessageFreeFunction *freeCallback) {
  // TODO(b/371009029): Implement this.
  UNUSED_VAR(message);
  UNUSED_VAR(messageSize);
  UNUSED_VAR(messageType);
  UNUSED_VAR(sessionId);
  UNUSED_VAR(messagePermissions);
  UNUSED_VAR(freeCallback);
  return false;
}
