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

#ifdef CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED

#include "chre/util/system/message_common.h"
#include "pw_allocator/unique_ptr.h"
#include "pw_function/function.h"
#include "pw_span/span.h"

namespace chre {

using ::chre::message::EndpointId;
using ::chre::message::EndpointInfo;
using ::chre::message::MessageHubId;
using ::chre::message::MessageHubInfo;
using ::chre::message::MessageRouter;
using ::chre::message::Reason;
using ::chre::message::Session;
using ::chre::message::SessionId;

void HostMessageHubManager::onHostTransportReady(HostCallback & /*cb*/) {
  // TODO(b/390447515): implement
}

void HostMessageHubManager::reset(
    pw::span<const message::MessageHubInfo> /*hubs*/,
    pw::span<std::pair<message::MessageHubId, const message::EndpointInfo>>
    /*endpoints*/) {
  // TODO(b/390447515): implement
  (void)mCb;
  (void)mHubs;
}

void HostMessageHubManager::registerHub(const MessageHubInfo & /*info*/) {
  // TODO(b/390447515): implement
}

void HostMessageHubManager::unregisterHub(MessageHubId /*id*/) {
  // TODO(b/390447515): implement
}

void HostMessageHubManager::registerEndpoint(const EndpointInfo & /*info*/) {
  // TODO(b/390447515): implement
}

void HostMessageHubManager::unregisterEndpoint(MessageHubId /*hubId*/,
                                               EndpointId /*id*/) {
  // TODO(b/390447515): implement
}

void HostMessageHubManager::openSession(MessageHubId /*hubId*/,
                                        EndpointId /*endpointId*/,
                                        MessageHubId /*destinationHubId*/,
                                        EndpointId /*destinationEndpointId*/,
                                        SessionId /*sessionId*/) {
  // TODO(b/390447515): implement
}

void HostMessageHubManager::ackSession(MessageHubId /*hubId*/,
                                       SessionId /*sessionId*/) {
  // TODO(b/390447515): implement
}

void HostMessageHubManager::closeSession(MessageHubId /*hubId*/,
                                         SessionId /*sessionId*/,
                                         Reason /*reason*/) {
  // TODO(b/390447515): implement
}

void HostMessageHubManager::sendMessage(MessageHubId /*hubId*/,
                                        SessionId /*sessionId*/,
                                        pw::UniquePtr<std::byte[]> && /*data*/,
                                        uint32_t /*type*/,
                                        uint32_t /*permissions*/) {
  // TODO(b/390447515): implement
}

bool HostMessageHubManager::Hub::restoreOrCreate(
    const MessageHubInfo & /*info*/,
    pw::IntrusiveList<Endpoint> & /*endpoints*/) {
  // TODO(b/390447515): implement
  return false;
}

HostMessageHubManager::Hub::~Hub() {
  // TODO(b/390447515): implement
}

void HostMessageHubManager::Hub::clear() {
  // TODO(b/390447515): implement
}

void HostMessageHubManager::Hub::addEndpoint(const EndpointInfo & /*info*/) {
  // TODO(b/390447515): implement
}

void HostMessageHubManager::Hub::removeEndpoint(EndpointId /*id*/) {
  // TODO(b/390447515): implement
}

bool HostMessageHubManager::Hub::onMessageReceived(
    pw::UniquePtr<std::byte[]> && /*data*/, uint32_t /*messageType*/,
    uint32_t /*messagePermissions*/, const Session & /*session*/,
    bool /*sentBySessionInitiator*/) {
  // TODO(b/390447515): implement
  return false;
}

void HostMessageHubManager::Hub::onSessionOpenRequest(
    const Session & /*session*/) {
  // TODO(b/390447515): implement
}

void HostMessageHubManager::Hub::onSessionOpened(const Session & /*session*/) {
  // TODO(b/390447515): implement
}

void HostMessageHubManager::Hub::onSessionClosed(const Session & /*session*/,
                                                 Reason /*reason*/) {
  // TODO(b/390447515): implement
}

void HostMessageHubManager::Hub::forEachEndpoint(
    const pw::Function<bool(const EndpointInfo &)> & /*function*/) {
  // TODO(b/390447515): implement
}

std::optional<EndpointInfo> HostMessageHubManager::Hub::getEndpointInfo(
    EndpointId /*endpointId*/) {
  // TODO(b/390447515): implement
  return {};
}

}  // namespace chre

#endif  // CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED
