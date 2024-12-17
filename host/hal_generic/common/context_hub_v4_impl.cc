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
#include <aidl/android/hardware/contexthub/BnEndpointCommunication.h>
#include <chre_host/generated/host_messages_generated.h>
#include <chre_host/log.h>

namespace android::hardware::contexthub::common::implementation {

using ::aidl::android::hardware::contexthub::BnContextHub;
using ::chre::fbs::ChreMessage;
using HostHub = MessageHubManager::HostHub;

void ContextHubV4Impl::init() {
  // TODO(b/378545373): Send message to get hubs/endpoints state dump to
  // initialize mManager.
}

namespace {

ScopedAStatus fromPwStatus(pw::Status status) {
  switch (status.code()) {
    case PW_STATUS_OK:
      return ScopedAStatus::ok();
    case PW_STATUS_NOT_FOUND:
      [[fallthrough]];
    case PW_STATUS_ALREADY_EXISTS:
      return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    case PW_STATUS_OUT_OF_RANGE:
      [[fallthrough]];
    case PW_STATUS_PERMISSION_DENIED:
      [[fallthrough]];
    case PW_STATUS_INVALID_ARGUMENT:
      return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    case PW_STATUS_UNIMPLEMENTED:
      return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    default:
      return ScopedAStatus::fromServiceSpecificError(
          BnContextHub::EX_CONTEXT_HUB_UNSPECIFIED);
  }
}

}  // namespace

ScopedAStatus ContextHubV4Impl::getHubs(std::vector<HubInfo> *hubs) {
  *hubs = mManager.getEmbeddedHubs();
  return ScopedAStatus::ok();
}

ScopedAStatus ContextHubV4Impl::getEndpoints(
    std::vector<EndpointInfo> *endpoints) {
  *endpoints = mManager.getEmbeddedEndpoints();
  return ScopedAStatus::ok();
}

ScopedAStatus ContextHubV4Impl::registerEndpointHub(
    const std::shared_ptr<IEndpointCallback> &callback, const HubInfo &hubInfo,
    std::shared_ptr<IEndpointCommunication> * /*hubInterface*/) {
  auto statusOrHub = mManager.createHostHub(callback, hubInfo);
  if (!statusOrHub.ok()) {
    LOGE("Failed to register message hub %" PRId64 " with %" PRId32,
         hubInfo.hubId, statusOrHub.status().ok());
    return fromPwStatus(statusOrHub.status());
  }
  // TODO(b/378545373): Register the hub with CHRE.
  // *hubInterface =
  //     ndk::SharedRefBase::make<HostHubInterface>(std::move(*statusOrHub));
  // return ScopedAStatus::ok();
  (*statusOrHub)->unregister();
  return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus HostHubInterface::registerEndpoint(const EndpointInfo &endpoint) {
  if (auto status = mHub->addEndpoint(endpoint); !status.ok()) {
    LOGE("Failed to register endpoint %" PRId64 " on hub %" PRId64
         " with %" PRId32,
         endpoint.id.id, mHub->id(), status.code());
    return fromPwStatus(status);
  }
  // TODO(b/378545373): Send the endpoint info to CHRE.
  return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus HostHubInterface::unregisterEndpoint(
    const EndpointInfo &endpoint) {
  if (auto status = mHub->removeEndpoint(endpoint.id); !status.ok()) {
    LOGE("Failed to unregister endpoint %" PRId32 " on hub %" PRId32
         " with %" PRId32,
         endpoint.id.id, mHub->id(), status.code());
    return fromPwStatus(status);
  }
  // TODO(b/378545373): Send the endpoint info to CHRE.
  return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus HostHubInterface::requestSessionIdRange(
    int32_t size, std::array<int32_t, 2> *ids) {
  auto statusOrRange = mHub->reserveSessionIdRange(size);
  if (!statusOrRange.ok()) {
    LOGE("Failed to reserve %" PRId32 " session ids on hub %" PRId64
         " with %" PRId32,
         size, mHub->id(), statusOrRange.status().code());
    return fromPwStatus(statusOrRange.status());
  }
  (*ids)[0] = statusOrRange->first;
  (*ids)[1] = statusOrRange->second;
  return ScopedAStatus::ok();
}

ScopedAStatus HostHubInterface::openEndpointSession(
    int32_t sessionId, const EndpointId &destination,
    const EndpointId &initiator,
    const std::optional<std::string> & /*serviceDescriptor*/) {
  // Ignore the flag to send a close. This hub overriding its own session is an
  // should just return error.
  auto status = mHub->openSession(initiator, destination, sessionId).status();
  if (!status.ok()) {
    LOGE("Failed to open session %" PRId32 " from (%" PRId64 ", %" PRId64
         ") to (%" PRId64 ", %" PRId64 ") with %" PRId32,
         sessionId, initiator.hubId, initiator.id, destination.hubId,
         destination.id, status.code());
    return fromPwStatus(status);
  }
  // TODO(b/378545373): Send the session open request to CHRE.
  return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus HostHubInterface::sendMessageToEndpoint(int32_t sessionId,
                                                      const Message & /*msg*/) {
  if (auto status = mHub->checkSessionOpen(sessionId); !status.ok()) {
    if (status.IsUnavailable()) {
      mHub->callback()->onCloseEndpointSession(sessionId,
                                               Reason::ENDPOINT_GONE);
    } else {
      LOGE("Failed to verify session %" PRId32 " on hub %" PRId64
           " with %" PRId32,
           sessionId, mHub->id(), status.code());
    }
    return fromPwStatus(status);
  }
  // TODO(b/378545373): Handle reliable messages.
  // TODO(b/378545373): Send the message to CHRE.
  return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus HostHubInterface::sendMessageDeliveryStatusToEndpoint(
    int32_t sessionId, const MessageDeliveryStatus & /*msgStatus*/) {
  if (auto status = mHub->checkSessionOpen(sessionId); !status.ok()) {
    if (status.IsUnavailable()) {
      mHub->callback()->onCloseEndpointSession(sessionId,
                                               Reason::ENDPOINT_GONE);
    } else {
      LOGE("Failed to verify session %" PRId32 " on hub %" PRId64
           " with %" PRId32,
           sessionId, mHub->id(), status.code());
    }
    return fromPwStatus(status);
  }
  // TODO(b/378545373): Send the message to CHRE.
  return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus HostHubInterface::closeEndpointSession(int32_t sessionId,
                                                     Reason /*reason*/) {
  if (auto status = mHub->closeSession(sessionId); !status.ok()) {
    LOGE("Failed to close session %" PRId32 " on hub %" PRId64 " with %" PRId32,
         sessionId, mHub->id(), status.code());
    return fromPwStatus(status);
  }
  mHub->callback()->onCloseEndpointSession(
      sessionId, Reason::CLOSE_ENDPOINT_SESSION_REQUESTED);
  return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus HostHubInterface::endpointSessionOpenComplete(int32_t sessionId) {
  if (auto status = mHub->ackSession(sessionId); !status.ok()) {
    if (status.IsUnavailable()) {
      mHub->callback()->onCloseEndpointSession(sessionId,
                                               Reason::ENDPOINT_GONE);
    } else {
      LOGE("Failed to verify session %" PRId32 " on hub %" PRId64
           " with %" PRId32,
           sessionId, mHub->id(), status.code());
    }
    return fromPwStatus(status);
  }
  // TODO(b/378545373): Send the session id to CHRE.
  return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus HostHubInterface::unregister() {
  return fromPwStatus(mHub->unregister());
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
  // TODO(b/378545373): Parse flatbuffer message
  std::vector<HubInfo> hubs;
  std::vector<EndpointInfo> endpoints;
  LOGI("Initializing embedded message hub cache");
  mManager.initEmbeddedHubsAndEndpoints(hubs, endpoints);
}

void ContextHubV4Impl::onRegisterMessageHub(
    const ::chre::fbs::RegisterMessageHubT & /*msg*/) {
  // TODO(b/378545373): Parse flatbuffer message
  HubInfo hub;
  LOGI("Embedded message hub %" PRId64 " registered", hub.hubId);
  mManager.addEmbeddedHub(hub);
}

void ContextHubV4Impl::onUnregisterMessageHub(
    const ::chre::fbs::UnregisterMessageHubT & /*msg*/) {
  // TODO(b/378545373): Parse flatbuffer message
  int64_t id = 0;
  LOGI("Embedded message hub %" PRId64 " unregistered", id);
  std::vector<EndpointId> endpoints = mManager.removeEmbeddedHub(id);
  if (!endpoints.empty()) {
    mManager.forEachHostHub([&endpoints](HostHub &hub) {
      hub.callback()->onEndpointStopped(endpoints, Reason::HUB_RESET);
    });
  }
}

void ContextHubV4Impl::onRegisterEndpoint(
    const ::chre::fbs::RegisterEndpointT & /*msg*/) {
  // TODO(b/378545373): Parse flatbuffer message
  EndpointInfo endpoint;
  LOGI("Adding embedded endpoint (%" PRId64 ", %" PRId64 ")", endpoint.id.hubId,
       endpoint.id.id);
  mManager.addEmbeddedEndpoint(endpoint);
  mManager.forEachHostHub([&endpoint](HostHub &hub) {
    hub.callback()->onEndpointStarted({endpoint});
  });
}

void ContextHubV4Impl::onUnregisterEndpoint(
    const ::chre::fbs::UnregisterEndpointT & /*msg*/) {
  // TODO(b/378545373): Parse flatbuffer message
  EndpointId endpoint;
  LOGI("Removing embedded endpoint (%" PRId64 ", %" PRId64 ")", endpoint.hubId,
       endpoint.id);
  mManager.removeEmbeddedEndpoint(endpoint);
  mManager.forEachHostHub([&endpoint](HostHub &hub) {
    hub.callback()->onEndpointStopped({endpoint}, Reason::ENDPOINT_GONE);
  });
}

void ContextHubV4Impl::onOpenEndpointSessionRequest(
    const ::chre::fbs::OpenEndpointSessionRequestT & /*msg*/) {
  // TODO(b/378545373): Parse flatbuffer message
  std::optional<std::string> serviceDescriptor;
  EndpointId local, remote;
  int64_t hubId = 0;
  uint16_t sessionId = 0;
  LOGD("New session (%" PRIu16 ") request from (%" PRId64 ", %" PRId64
       ") to "
       "(%" PRId64 ", %" PRId64 ")",
       sessionId, remote.hubId, remote.id, local.hubId, local.id);
  std::shared_ptr<HostHub> hub = mManager.getHostHub(hubId);
  if (!hub) {
    LOGW("Unable to find host hub");
    return;
  }

  // Record the open session request and pass it on to the appropriate client.
  pw::Result<bool> statusOrSendClose =
      hub->openSession(local, remote, sessionId);
  if (!statusOrSendClose.ok()) {
    LOGE("Failed to request session %" PRIu16 " with %" PRId32, sessionId,
         statusOrSendClose.status().code());
    // TODO(b/378545373): Send close session back to MessageRouter
    return;
  } else if (*statusOrSendClose) {
    // Send a closed session notification on the hub that hosted the pruned
    // session.
    auto status = hub->closeSession(sessionId);
    LOGD("Pruned session %" PRIu16 " with status %" PRId32, sessionId,
         status.code());
  }
  hub->callback()->onEndpointSessionOpenRequest(sessionId, local, remote,
                                                std::move(serviceDescriptor));
}

namespace {

void logSessionFailure(pw::Status status, uint16_t sessionId) {
  if (status.IsUnavailable()) {
    LOGD("Session %" PRIu16 " was pruned.", sessionId);
  } else {
    LOGE("Failed to operate on session %" PRIu16 " with %" PRId32, sessionId,
         status.code());
  }
}

}  // namespace

void ContextHubV4Impl::onEndpointSessionOpened(
    const ::chre::fbs::EndpointSessionOpenedT & /*msg*/) {
  // TODO(b/378545373): Parse flatbuffer message
  int64_t hubId = 0;
  uint16_t sessionId = 0;
  LOGD("New session ack for id %" PRIu16 " on hub %" PRId64, sessionId, hubId);
  std::shared_ptr<HostHub> hub = mManager.getHostHub(hubId);
  if (!hub) {
    LOGW("Unable to find host hub");
    return;
  }
  if (auto status = hub->ackSession(sessionId); !status.ok()) {
    logSessionFailure(status, sessionId);
    // TODO(b/378545373): Send a notification back to CHRE.
    return;
  }
  // Only send a session open complete message to the host hub client if it was
  // the initiator.
  if (sessionId >= MessageHubManager::kHostSessionIdBase)
    hub->callback()->onEndpointSessionOpenComplete(sessionId);
}

void ContextHubV4Impl::onEndpointSessionClosed(
    const ::chre::fbs::EndpointSessionClosedT & /*msg*/) {
  // TODO(b/378545373): Parse flatbuffer message
  int64_t hubId = 0;
  uint16_t sessionId = 0;
  Reason reason = Reason::UNSPECIFIED;
  LOGD("Closing session id %" PRIu16 " for %" PRIu8, sessionId, reason);
  std::shared_ptr<HostHub> hub = mManager.getHostHub(hubId);
  if (!hub) {
    LOGW("Unable to find host hub");
    return;
  }
  if (!hub->closeSession(sessionId).ok()) return;
  hub->callback()->onCloseEndpointSession(sessionId, reason);
}

void ContextHubV4Impl::onEndpointSessionMessage(
    const ::chre::fbs::EndpointSessionMessageT & /*msg*/) {
  // TODO(b/378545373): Parse flatbuffer message
  Message message;
  int64_t hubId = 0;
  uint16_t sessionId = 0;
  std::shared_ptr<HostHub> hub = mManager.getHostHub(hubId);
  if (!hub) {
    LOGW("Unable to find host hub");
    return;
  }
  if (auto status = hub->checkSessionOpen(sessionId); !status.ok()) {
    logSessionFailure(status, sessionId);
    // TODO(b/378545373): Send a notification back to CHRE.
    return;
  }
  hub->callback()->onMessageReceived(sessionId, message);
}

void ContextHubV4Impl::onEndpointSessionMessageDeliveryStatus(
    const ::chre::fbs::EndpointSessionMessageDeliveryStatusT & /*msg*/) {
  // TODO(b/378545373): Parse flatbuffer message
  MessageDeliveryStatus status;
  int64_t hubId = 0;
  uint16_t sessionId = 0;
  std::shared_ptr<HostHub> hub = mManager.getHostHub(hubId);
  if (!hub) {
    LOGW("Unable to find host hub");
    return;
  }
  if (auto status = hub->checkSessionOpen(sessionId); !status.ok()) {
    logSessionFailure(status, sessionId);
    // TODO(b/378545373): Send a notification back to CHRE.
    return;
  }
  // TODO(b/378545373): Handle reliable messages.
  hub->callback()->onMessageDeliveryStatusReceived(sessionId, status);
}

void ContextHubV4Impl::onHostHubDown(int64_t /*id*/) {
  // TODO(b/378545373): Send an UnregisterMessageHub message to CHRE with id.
}

}  // namespace android::hardware::contexthub::common::implementation
