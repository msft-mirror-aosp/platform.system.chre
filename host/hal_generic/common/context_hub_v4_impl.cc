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

#include "host_protocol_host_v4.h"

namespace android::hardware::contexthub::common::implementation {

using ::aidl::android::hardware::contexthub::BnContextHub;
using ::chre::fbs::ChreMessage;
using HostHub = MessageHubManager::HostHub;

void ContextHubV4Impl::init() {
  std::lock_guard lock(mHostHubOpLock);  // See header documentation.
  flatbuffers::FlatBufferBuilder builder;
  // NOTE: This message should be renamed as on initialization/CHRE restart it
  // is used both to initialize the CHRE-side host hub proxies and to request
  // embedded hub state.
  HostProtocolHostV4::encodeGetMessageHubsAndEndpointsRequest(builder);
  if (!mSendMessageFn(builder))
    LOGE("Failed to initialize CHRE host hub proxies");
  mManager.forEachHostHub([this](HostHub &hub) {
    flatbuffers::FlatBufferBuilder builder;
    HostProtocolHostV4::encodeRegisterMessageHub(builder, hub.info());
    if (!mSendMessageFn(builder)) {
      LOGE("Failed to initialize proxy for host hub %" PRIu64, hub.id());
      return;
    }
    for (const auto &endpoint : hub.getEndpoints()) {
      flatbuffers::FlatBufferBuilder builder;
      HostProtocolHostV4::encodeRegisterEndpoint(builder, endpoint);
      if (!mSendMessageFn(builder)) {
        LOGE("Failed to initialize proxy for host endpoint (%" PRIu64
             ", %" PRIu64 ")",
             endpoint.id.hubId, endpoint.id.id);
        return;
      }
    }
  });
}

void ContextHubV4Impl::onChreDisconnected() {
  LOGI("Clearing embedded message hub state.");
  mManager.clearEmbeddedState();
}

void ContextHubV4Impl::onChreRestarted() {
  init();
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
    std::shared_ptr<IEndpointCommunication> *hubInterface) {
  std::lock_guard lock(mHostHubOpLock);  // See header documentation.
  auto statusOrHub = mManager.createHostHub(
      callback, hubInfo, AIBinder_getCallingUid(), AIBinder_getCallingPid());
  if (!statusOrHub.ok()) {
    LOGE("Failed to register message hub 0x%" PRIx64 " with %" PRId32,
         hubInfo.hubId, statusOrHub.status().ok());
    return fromPwStatus(statusOrHub.status());
  }
  flatbuffers::FlatBufferBuilder builder;
  HostProtocolHostV4::encodeRegisterMessageHub(builder, hubInfo);
  if (!mSendMessageFn(builder)) {
    LOGE("Failed to send RegisterMessageHub for hub 0x%" PRIx64, hubInfo.hubId);
    (*statusOrHub)->unregister();
    return ScopedAStatus::fromServiceSpecificError(
        BnContextHub::EX_CONTEXT_HUB_UNSPECIFIED);
  }
  *hubInterface = ndk::SharedRefBase::make<HostHubInterface>(
      std::move(*statusOrHub), mSendMessageFn, mHostHubOpLock);
  return ScopedAStatus::ok();
}

ScopedAStatus HostHubInterface::registerEndpoint(const EndpointInfo &endpoint) {
  std::lock_guard lock(mHostHubOpLock);  // See header documentation.
  if (auto status = mHub->addEndpoint(endpoint); !status.ok()) {
    LOGE("Failed to register endpoint 0x%" PRIx64 " on hub 0x%" PRIx64
         " with %" PRId32,
         endpoint.id.id, mHub->id(), status.code());
    return fromPwStatus(status);
  }
  flatbuffers::FlatBufferBuilder builder;
  HostProtocolHostV4::encodeRegisterEndpoint(builder, endpoint);
  if (!mSendMessageFn(builder)) {
    LOGE("Failed to send RegisterEndpoint for (0x%" PRIx64 ", 0x%" PRIx64 ")",
         endpoint.id.hubId, endpoint.id.id);
    mHub->removeEndpoint(endpoint.id).IgnoreError();
    return ScopedAStatus::fromServiceSpecificError(
        BnContextHub::EX_CONTEXT_HUB_UNSPECIFIED);
  }
  return ScopedAStatus::ok();
}

ScopedAStatus HostHubInterface::unregisterEndpoint(
    const EndpointInfo &endpoint) {
  std::lock_guard lock(mHostHubOpLock);  // See header documentation.
  auto statusOrSessions = mHub->removeEndpoint(endpoint.id);
  if (!statusOrSessions.ok()) {
    LOGE("Failed to unregister endpoint 0x%" PRIx64 " on hub 0x%" PRIx64
         " with %" PRId32,
         endpoint.id.id, mHub->id(), statusOrSessions.status().code());
    return fromPwStatus(statusOrSessions.status());
  }
  flatbuffers::FlatBufferBuilder builder;
  HostProtocolHostV4::encodeUnregisterEndpoint(builder, endpoint.id);
  if (!mSendMessageFn(builder)) {
    LOGE("Failed to send RegisterEndpoint for (0x%" PRIx64 ", 0x%" PRIx64 ")",
         endpoint.id.hubId, endpoint.id.id);
    return ScopedAStatus::fromServiceSpecificError(
        BnContextHub::EX_CONTEXT_HUB_UNSPECIFIED);
  }
  return ScopedAStatus::ok();
}

ScopedAStatus HostHubInterface::requestSessionIdRange(
    int32_t size, std::array<int32_t, 2> *ids) {
  auto statusOrRange = mHub->reserveSessionIdRange(size);
  if (!statusOrRange.ok()) {
    LOGE("Failed to reserve %" PRId32 " session ids on hub 0x%" PRIx64
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
    const std::optional<std::string> &serviceDescriptor) {
  // Ignore the flag to send a close. This hub overriding its own session is an
  // should just return error.
  auto status = mHub->openSession(initiator, destination, sessionId,
                                  serviceDescriptor, /*hostInitiated=*/true);
  if (!status.ok()) {
    LOGE("Failed to open session %" PRId32 " from (0x%" PRIx64 ", 0x%" PRIx64
         ") to (0x%" PRIx64 ", 0x%" PRIx64 ") with %" PRId32,
         sessionId, initiator.hubId, initiator.id, destination.hubId,
         destination.id, status.code());
    return fromPwStatus(status);
  }
  flatbuffers::FlatBufferBuilder builder;
  HostProtocolHostV4::encodeOpenEndpointSessionRequest(
      builder, mHub->id(), sessionId, initiator, destination,
      serviceDescriptor);
  if (!mSendMessageFn(builder)) {
    LOGE("Failed to send OpenEndpointSessionRequest for session %" PRId32,
         sessionId);
    mHub->closeSession(sessionId, Reason::UNSPECIFIED).IgnoreError();
    return ScopedAStatus::fromServiceSpecificError(
        BnContextHub::EX_CONTEXT_HUB_UNSPECIFIED);
  }
  return ScopedAStatus::ok();
}

ScopedAStatus HostHubInterface::sendMessageToEndpoint(int32_t sessionId,
                                                      const Message &msg) {
  if (auto status = mHub->checkSessionOpen(sessionId); !status.ok()) {
    LOGE("Failed to verify session %" PRId32 " on hub 0x%" PRIx64
         " with %" PRId32,
         sessionId, mHub->id(), status.code());
    return fromPwStatus(status);
  }
  // TODO(b/378545373): Handle reliable messages.
  flatbuffers::FlatBufferBuilder builder;
  HostProtocolHostV4::encodeEndpointSessionMessage(builder, mHub->id(),
                                                   sessionId, msg);
  if (!mSendMessageFn(builder)) {
    LOGE("Failed to send EndpointSessionMessage over session %" PRId32,
         sessionId);
    return ScopedAStatus::fromServiceSpecificError(
        BnContextHub::EX_CONTEXT_HUB_UNSPECIFIED);
  }
  return ScopedAStatus::ok();
}

ScopedAStatus HostHubInterface::sendMessageDeliveryStatusToEndpoint(
    int32_t sessionId, const MessageDeliveryStatus &msgStatus) {
  if (auto status = mHub->checkSessionOpen(sessionId); !status.ok()) {
    LOGE("Failed to verify session %" PRId32 " on hub 0x%" PRIx64
         " with %" PRId32,
         sessionId, mHub->id(), status.code());
    return fromPwStatus(status);
  }
  flatbuffers::FlatBufferBuilder builder;
  HostProtocolHostV4::encodeEndpointSessionMessageDeliveryStatus(
      builder, mHub->id(), sessionId, msgStatus);
  if (!mSendMessageFn(builder)) {
    LOGE(
        "Failed to send EndpointSessionMessageDeliveryStatus over session "
        "%" PRId32,
        sessionId);
    return ScopedAStatus::fromServiceSpecificError(
        BnContextHub::EX_CONTEXT_HUB_UNSPECIFIED);
  }
  return ScopedAStatus::ok();
}

ScopedAStatus HostHubInterface::closeEndpointSession(int32_t sessionId,
                                                     Reason reason) {
  if (auto status = mHub->closeSession(sessionId); !status.ok()) {
    LOGE("Failed to close session %" PRId32 " on hub 0x%" PRIx64
         " with %" PRId32,
         sessionId, mHub->id(), status.code());
    return fromPwStatus(status);
  }
  flatbuffers::FlatBufferBuilder builder;
  HostProtocolHostV4::encodeEndpointSessionClosed(builder, mHub->id(),
                                                  sessionId, reason);
  if (!mSendMessageFn(builder)) {
    LOGE("Failed to send EndpointSessionClosed for session %" PRId32,
         sessionId);
    return ScopedAStatus::fromServiceSpecificError(
        BnContextHub::EX_CONTEXT_HUB_UNSPECIFIED);
  }
  return ScopedAStatus::ok();
}

ScopedAStatus HostHubInterface::endpointSessionOpenComplete(int32_t sessionId) {
  if (auto status = mHub->ackSession(sessionId, /*hostAcked=*/true);
      !status.ok()) {
    LOGE("Failed to verify session %" PRId32 " on hub 0x%" PRIx64
         " with %" PRId32,
         sessionId, mHub->id(), status.code());
    return fromPwStatus(status);
  }
  flatbuffers::FlatBufferBuilder builder;
  HostProtocolHostV4::encodeEndpointSessionOpened(builder, mHub->id(),
                                                  sessionId);
  if (!mSendMessageFn(builder)) {
    LOGE("Failed to send EndpointSessionOpened for session %" PRId32,
         sessionId);
    return ScopedAStatus::fromServiceSpecificError(
        BnContextHub::EX_CONTEXT_HUB_UNSPECIFIED);
  }
  return ScopedAStatus::ok();
}

ScopedAStatus HostHubInterface::unregister() {
  std::lock_guard lock(mHostHubOpLock);  // See header documentation.
  if (auto status = mHub->unregister(); !status.ok())
    return fromPwStatus(status);
  flatbuffers::FlatBufferBuilder builder;
  HostProtocolHostV4::encodeUnregisterMessageHub(builder, mHub->id());
  if (!mSendMessageFn(builder))
    LOGE("Failed to send UnregisterMessageHub for hub 0x%" PRIx64, mHub->id());
  return ScopedAStatus::ok();
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
    case ChreMessage::UnregisterEndpoint:
      onUnregisterEndpoint(*message.AsUnregisterEndpoint());
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
    case ChreMessage::AddServiceToEndpoint:
      onAddServiceToEndpoint(*message.AsAddServiceToEndpoint());
      break;
    case ChreMessage::EndpointReady:
      onEndpointReady(*message.AsEndpointReady());
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
  LOGI("Initializing embedded message hub cache");
  mManager.initEmbeddedState();
}

void ContextHubV4Impl::onRegisterMessageHub(
    const ::chre::fbs::RegisterMessageHubT &msg) {
  HubInfo hub;
  HostProtocolHostV4::decodeRegisterMessageHub(msg, hub);
  LOGI("Embedded message hub 0x%" PRIx64 " registered", hub.hubId);
  mManager.addEmbeddedHub(hub);
}

void ContextHubV4Impl::onUnregisterMessageHub(
    const ::chre::fbs::UnregisterMessageHubT &msg) {
  int64_t id = 0;
  HostProtocolHostV4::decodeUnregisterMessageHub(msg, id);
  LOGI("Embedded message hub 0x%" PRIx64 " unregistered", id);
  mManager.removeEmbeddedHub(id);
}

void ContextHubV4Impl::onRegisterEndpoint(
    const ::chre::fbs::RegisterEndpointT &msg) {
  EndpointInfo endpoint;
  HostProtocolHostV4::decodeRegisterEndpoint(msg, endpoint);
  LOGI("Adding embedded endpoint (0x%" PRIx64 ", 0x%" PRIx64 ")",
       endpoint.id.hubId, endpoint.id.id);
  mManager.addEmbeddedEndpoint(endpoint);
}

void ContextHubV4Impl::onAddServiceToEndpoint(
    const ::chre::fbs::AddServiceToEndpointT &msg) {
  EndpointId endpoint;
  Service service;
  HostProtocolHostV4::decodeAddServiceToEndpoint(msg, endpoint, service);
  mManager.addEmbeddedEndpointService(endpoint, service);
}

void ContextHubV4Impl::onEndpointReady(const ::chre::fbs::EndpointReadyT &msg) {
  EndpointId endpoint;
  HostProtocolHostV4::decodeEndpointReady(msg, endpoint);
  LOGI("Embedded endpoint (0x%" PRIx64 ", 0x%" PRIx64 ") ready", endpoint.hubId,
       endpoint.id);
  mManager.setEmbeddedEndpointReady(endpoint);
}

void ContextHubV4Impl::onUnregisterEndpoint(
    const ::chre::fbs::UnregisterEndpointT &msg) {
  EndpointId endpoint;
  HostProtocolHostV4::decodeUnregisterEndpoint(msg, endpoint);
  LOGI("Removing embedded endpoint (0x%" PRIx64 ", 0x%" PRIx64 ")",
       endpoint.hubId, endpoint.id);
  mManager.removeEmbeddedEndpoint(endpoint);
}

void ContextHubV4Impl::onOpenEndpointSessionRequest(
    const ::chre::fbs::OpenEndpointSessionRequestT &msg) {
  std::optional<std::string> serviceDescriptor;
  EndpointId local, remote;
  int64_t hubId = 0;
  uint16_t sessionId = 0;
  HostProtocolHostV4::decodeOpenEndpointSessionRequest(
      msg, hubId, sessionId, local, remote, serviceDescriptor);
  LOGD("New session (%" PRIu16 ") request from (0x%" PRIx64 ", 0x%" PRIx64
       ") to "
       "(0x%" PRIx64 ", 0x%" PRIx64 ")",
       sessionId, remote.hubId, remote.id, local.hubId, local.id);
  std::shared_ptr<HostHub> hub = mManager.getHostHub(hubId);
  if (!hub) {
    LOGW("Unable to find host hub");
    return;
  }

  // Record the open session request and pass it on to the appropriate client.
  auto status =
      hub->openSession(local, remote, sessionId, std::move(serviceDescriptor),
                       /*hostInitiated=*/false);
  if (!status.ok()) {
    LOGE("Failed to request session %" PRIu16 " with %" PRId32, sessionId,
         status.code());
    flatbuffers::FlatBufferBuilder builder;
    HostProtocolHostV4::encodeEndpointSessionClosed(
        builder, hub->id(), sessionId, Reason::UNSPECIFIED);
    mSendMessageFn(builder);
    return;
  }
}

void ContextHubV4Impl::onEndpointSessionOpened(
    const ::chre::fbs::EndpointSessionOpenedT &msg) {
  int64_t hubId = 0;
  uint16_t sessionId = 0;
  HostProtocolHostV4::decodeEndpointSessionOpened(msg, hubId, sessionId);
  LOGD("New session ack for id %" PRIu16 " on hub 0x%" PRIx64, sessionId,
       hubId);
  std::shared_ptr<HostHub> hub = mManager.getHostHub(hubId);
  if (!hub) {
    LOGW("Unable to find host hub");
    return;
  }
  if (auto status = hub->ackSession(sessionId, /*hostAcked=*/false);
      !status.ok()) {
    handleSessionFailure(hub, sessionId, status);
    return;
  }
}

void ContextHubV4Impl::onEndpointSessionClosed(
    const ::chre::fbs::EndpointSessionClosedT &msg) {
  int64_t hubId = 0;
  uint16_t sessionId = 0;
  Reason reason = Reason::UNSPECIFIED;
  HostProtocolHostV4::decodeEndpointSessionClosed(msg, hubId, sessionId,
                                                  reason);
  LOGD("Closing session id %" PRIu16 " for %" PRIu8, sessionId, reason);
  std::shared_ptr<HostHub> hub = mManager.getHostHub(hubId);
  if (!hub) {
    LOGW("Unable to find host hub");
    return;
  }
  hub->closeSession(sessionId, reason).IgnoreError();
}

void ContextHubV4Impl::onEndpointSessionMessage(
    const ::chre::fbs::EndpointSessionMessageT &msg) {
  Message message;
  int64_t hubId = 0;
  uint16_t sessionId = 0;
  HostProtocolHostV4::decodeEndpointSessionMessage(msg, hubId, sessionId,
                                                   message);
  std::shared_ptr<HostHub> hub = mManager.getHostHub(hubId);
  if (!hub) {
    LOGW("Unable to find host hub");
    return;
  }
  auto status = hub->handleMessage(sessionId, message);
  if (status.ok()) return;
  handleSessionFailure(hub, sessionId, status);
}

void ContextHubV4Impl::onEndpointSessionMessageDeliveryStatus(
    const ::chre::fbs::EndpointSessionMessageDeliveryStatusT &msg) {
  MessageDeliveryStatus deliveryStatus;
  int64_t hubId = 0;
  uint16_t sessionId = 0;
  HostProtocolHostV4::decodeEndpointSessionMessageDeliveryStatus(
      msg, hubId, sessionId, deliveryStatus);
  std::shared_ptr<HostHub> hub = mManager.getHostHub(hubId);
  if (!hub) {
    LOGW("Unable to find host hub");
    return;
  }
  auto status = hub->handleMessageDeliveryStatus(sessionId, deliveryStatus);
  if (status.ok()) return;
  handleSessionFailure(hub, sessionId, status);
}

void ContextHubV4Impl::unlinkDeadHostHub(
    std::function<pw::Result<int64_t>()> unlinkFn) {
  std::lock_guard lock(mHostHubOpLock);  // See header documentation.
  auto statusOrHubId = unlinkFn();
  if (!statusOrHubId.ok()) return;
  flatbuffers::FlatBufferBuilder builder;
  HostProtocolHostV4::encodeUnregisterMessageHub(builder, *statusOrHubId);
  if (!mSendMessageFn(builder)) {
    LOGE("Failed to send UnregisterMessageHub for hub 0x%" PRIx64,
         *statusOrHubId);
  }
}

void ContextHubV4Impl::handleSessionFailure(const std::shared_ptr<HostHub> &hub,
                                            uint16_t session,
                                            pw::Status status) {
  LOGE("Failed to operate on session %" PRIu16 " on hub 0x%" PRIx64
       " with %" PRId32,
       session, hub->id(), status.code());
  flatbuffers::FlatBufferBuilder builder;
  HostProtocolHostV4::encodeEndpointSessionClosed(builder, hub->id(), session,
                                                  Reason::UNSPECIFIED);
  mSendMessageFn(builder);
  hub->closeSession(session, Reason::UNSPECIFIED).IgnoreError();
}

}  // namespace android::hardware::contexthub::common::implementation
