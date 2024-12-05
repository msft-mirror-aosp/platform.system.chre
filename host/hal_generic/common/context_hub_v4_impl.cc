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

#include "context_hub_v4_impl.h"

#include <inttypes.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <aidl/android/hardware/contexthub/BnContextHub.h>
#include <chre_host/generated/host_messages_generated.h>
#include <chre_host/log.h>

namespace android::hardware::contexthub::common::implementation {

using ::chre::fbs::ChreMessage;

void ContextHubV4Impl::init() {
  // TODO(b/378545373): Send message to get hubs/endpoints state dump to
  // initialize mManager.
}

ScopedAStatus ContextHubV4Impl::getHubs(std::vector<HubInfo> * /*hubs*/) {
  // TODO(b/378545373): Implement
  return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus ContextHubV4Impl::getEndpoints(
    std::vector<EndpointInfo> * /*endpoints*/) {
  // TODO(b/378545373): Implement
  return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus ContextHubV4Impl::registerEndpoint(
    const EndpointInfo & /*endpoint*/) {
  // TODO(b/378545373): Implement
  return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus ContextHubV4Impl::unregisterEndpoint(
    const EndpointInfo & /*endpoint*/) {
  // TODO(b/378545373): Implement
  return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus ContextHubV4Impl::registerEndpointCallback(
    const std::shared_ptr<IEndpointCallback> & /*callback*/) {
  // TODO(b/378545373): Implement
  return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus ContextHubV4Impl::requestSessionIdRange(
    int32_t /*size*/, std::vector<int32_t> * /*ids*/) {
  // TODO(b/378545373): Implement
  return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus ContextHubV4Impl::openEndpointSession(
    int32_t /*sessionId*/, const EndpointId & /*destination*/,
    const EndpointId & /*initiator*/,
    const std::optional<std::string> & /*serviceDescriptor*/) {
  // TODO(b/378545373): Implement
  return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus ContextHubV4Impl::sendMessageToEndpoint(int32_t /*sessionId*/,
                                                      const Message & /*msg*/) {
  // TODO(b/378545373): Implement
  return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus ContextHubV4Impl::sendMessageDeliveryStatusToEndpoint(
    int32_t /*sessionId*/, const MessageDeliveryStatus & /*msgStatus*/) {
  // TODO(b/378545373): Implement
  return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus ContextHubV4Impl::closeEndpointSession(int32_t /*sessionId*/,
                                                     Reason /*reason*/) {
  // TODO(b/378545373): Implement
  return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus ContextHubV4Impl::endpointSessionOpenComplete(
    int32_t /*sessionId*/) {
  // TODO(b/378545373): Implement
  return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

bool ContextHubV4Impl::handleMessageFromChre(
    const ::chre::fbs::ChreMessageUnion &message) {
  switch (message.type) {
    case ChreMessage::GetMessageHubsAndEndpointsResponse:
      onGetMessageHubsAndEndpointsResponse(
          *message.AsGetMessageHubsAndEndpointsResponse());
      break;
    case ChreMessage::RegisterMessageHub:
      onRegisterMessageHub(*message.AsRegisterMessageHub());
      break;
    case ChreMessage::UnregisterMessageHub:
      onUnregisterMessageHub(*message.AsUnregisterMessageHub());
      break;
    case ChreMessage::RegisterEndpoint:
      onRegisterEndpoint(*message.AsRegisterEndpoint());
      break;
    case ChreMessage::OpenEndpointSessionRequest:
      onOpenEndpointSessionRequest(*message.AsOpenEndpointSessionRequest());
      break;
    case ChreMessage::EndpointSessionOpened:
      onEndpointSessionOpened(*message.AsEndpointSessionOpened());
      break;
    case ChreMessage::EndpointSessionClosed:
      onEndpointSessionClosed(*message.AsEndpointSessionClosed());
      break;
    case ChreMessage::EndpointSessionMessage:
      onEndpointSessionMessage(*message.AsEndpointSessionMessage());
      break;
    case ChreMessage::EndpointSessionMessageDeliveryStatus:
      onEndpointSessionMessageDeliveryStatus(
          *message.AsEndpointSessionMessageDeliveryStatus());
      break;
    default:
      LOGW("Got unexpected message type %" PRIu8,
           static_cast<uint8_t>(message.type));
      return false;
  }
  return true;
}

void ContextHubV4Impl::onGetMessageHubsAndEndpointsResponse(
    const ::chre::fbs::GetMessageHubsAndEndpointsResponseT & /*msg*/) {
  // TODO(b/378545373): Implement
}

void ContextHubV4Impl::onRegisterMessageHub(
    const ::chre::fbs::RegisterMessageHubT & /*msg*/) {
  // TODO(b/378545373): Implement
}

void ContextHubV4Impl::onUnregisterMessageHub(
    const ::chre::fbs::UnregisterMessageHubT & /*msg*/) {
  // TODO(b/378545373): Implement
}

void ContextHubV4Impl::onRegisterEndpoint(
    const ::chre::fbs::RegisterEndpointT & /*msg*/) {
  // TODO(b/378545373): Implement
}

void ContextHubV4Impl::onUnregisterEndpoint(
    const ::chre::fbs::UnregisterEndpointT & /*msg*/) {
  // TODO(b/378545373): Implement
}

void ContextHubV4Impl::onOpenEndpointSessionRequest(
    const ::chre::fbs::OpenEndpointSessionRequestT & /*msg*/) {
  // TODO(b/378545373): Implement
}

void ContextHubV4Impl::onEndpointSessionOpened(
    const ::chre::fbs::EndpointSessionOpenedT & /*msg*/) {
  // TODO(b/378545373): Implement
}

void ContextHubV4Impl::onEndpointSessionClosed(
    const ::chre::fbs::EndpointSessionClosedT & /*msg*/) {
  // TODO(b/378545373): Implement
}

void ContextHubV4Impl::onEndpointSessionMessage(
    const ::chre::fbs::EndpointSessionMessageT & /*msg*/) {
  // TODO(b/378545373): Implement
}

void ContextHubV4Impl::onEndpointSessionMessageDeliveryStatus(
    const ::chre::fbs::EndpointSessionMessageDeliveryStatusT & /*msg*/) {
  // TODO(b/378545373): Implement
}

void ContextHubV4Impl::onHostHubDown(int64_t /*id*/) {
  // TODO(b/378545373): Send an UnregisterMessageHub message to CHRE with id.
}

}  // namespace android::hardware::contexthub::common::implementation
