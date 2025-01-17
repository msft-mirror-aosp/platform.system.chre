/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License") {}
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

#include "host_protocol_host_v4.h"

#include <chre_host/host_protocol_host.h>

#include "permissions_util.h"

namespace android::hardware::contexthub::common::implementation {

using ::chre::fbs::ChreMessage;
using ::chre::fbs::EndpointId;
using ::chre::fbs::EndpointInfo;
using ::chre::fbs::MessageDeliveryStatus;
using ::chre::fbs::MessageHub;
using ::chre::fbs::MessageHubDetails;
using ::chre::fbs::Reason;
using ::flatbuffers::FlatBufferBuilder;
using ::flatbuffers::Offset;
using ::flatbuffers::Vector;

using AidlContextHubInfo =
    ::aidl::android::hardware::contexthub::ContextHubInfo;
using AidlVendorHubInfo = ::aidl::android::hardware::contexthub::VendorHubInfo;
using AidlErrorCode = ::aidl::android::hardware::contexthub::ErrorCode;
using AidlRpcFormat = ::aidl::android::hardware::contexthub::Service::RpcFormat;

void HostProtocolHostV4::encodeGetMessageHubsAndEndpointsResponse(
    FlatBufferBuilder &builder, const std::vector<AidlHubInfo> &hubs,
    const std::vector<AidlEndpointInfo> &endpoints) {
  std::vector<Offset<MessageHub>> fbsHubs;
  fbsHubs.reserve(hubs.size());
  for (const auto &hub : hubs)
    fbsHubs.push_back(aidlToFbsMessageHub(builder, hub));
  std::vector<Offset<EndpointInfo>> fbsEndpoints;
  for (const auto &endpoint : endpoints)
    fbsEndpoints.push_back(aidlToFbsEndpointInfo(builder, endpoint));
  auto hubsVector = builder.CreateVector(fbsHubs);
  auto endpointsVector = builder.CreateVector(fbsEndpoints);
  auto msg = ::chre::fbs::CreateGetMessageHubsAndEndpointsResponse(
      builder, hubsVector, endpointsVector);
  finalize(builder, ChreMessage::GetMessageHubsAndEndpointsResponse,
           msg.Union());
}

void HostProtocolHostV4::encodeRegisterMessageHub(FlatBufferBuilder &builder,
                                                  const AidlHubInfo &info) {
  auto msg = ::chre::fbs::CreateRegisterMessageHub(
      builder, aidlToFbsMessageHub(builder, info));
  finalize(builder, ChreMessage::RegisterMessageHub, msg.Union());
}

void HostProtocolHostV4::encodeUnregisterMessageHub(FlatBufferBuilder &builder,
                                                    int64_t id) {
  auto msg = ::chre::fbs::CreateUnregisterMessageHub(builder, id);
  finalize(builder, ChreMessage::UnregisterMessageHub, msg.Union());
}

void HostProtocolHostV4::encodeRegisterEndpoint(FlatBufferBuilder &builder,
                                                const AidlEndpointInfo &info) {
  auto msg = ::chre::fbs::CreateRegisterEndpoint(
      builder, aidlToFbsEndpointInfo(builder, info));
  finalize(builder, ChreMessage::UnregisterMessageHub, msg.Union());
}

void HostProtocolHostV4::encodeUnregisterEndpoint(FlatBufferBuilder &builder,
                                                  const AidlEndpointId &id) {
  auto msg = ::chre::fbs::CreateUnregisterEndpoint(
      builder, aidlToFbsEndpointId(builder, id));
  finalize(builder, ChreMessage::UnregisterEndpoint, msg.Union());
}

void HostProtocolHostV4::encodeOpenEndpointSessionRequest(
    FlatBufferBuilder &builder, int64_t hostHubId, uint16_t sessionId,
    const AidlEndpointId &initiator, const AidlEndpointId &destination,
    const std::optional<std::string> &serviceDescriptor) {
  Offset<Vector<int8_t>> descriptorVector =
      serviceDescriptor
          ? addStringAsByteVector(builder, serviceDescriptor->c_str())
          : 0;
  auto msg = ::chre::fbs::CreateOpenEndpointSessionRequest(
      builder, hostHubId, sessionId, aidlToFbsEndpointId(builder, initiator),
      aidlToFbsEndpointId(builder, destination), descriptorVector);
  finalize(builder, ChreMessage::OpenEndpointSessionRequest, msg.Union());
}

void HostProtocolHostV4::encodeEndpointSessionOpened(FlatBufferBuilder &builder,
                                                     int64_t hostHubId,
                                                     uint16_t sessionId) {
  auto msg =
      ::chre::fbs::CreateEndpointSessionOpened(builder, hostHubId, sessionId);
  finalize(builder, ChreMessage::EndpointSessionOpened, msg.Union());
}

void HostProtocolHostV4::encodeEndpointSessionClosed(FlatBufferBuilder &builder,
                                                     int64_t hostHubId,
                                                     uint16_t sessionId,
                                                     AidlReason reason) {
  auto msg = ::chre::fbs::CreateEndpointSessionClosed(
      builder, hostHubId, sessionId, static_cast<Reason>(reason));
  finalize(builder, ChreMessage::EndpointSessionClosed, msg.Union());
}

void HostProtocolHostV4::encodeEndpointSessionMessage(
    FlatBufferBuilder &builder, int64_t hostHubId, uint16_t sessionId,
    const AidlMessage &message) {
  auto msg = ::chre::fbs::CreateEndpointSessionMessage(
      builder, hostHubId, sessionId, message.type,
      androidToChrePermissions(message.permissions),
      builder.CreateVector(message.content), message.flags,
      message.sequenceNumber);
  finalize(builder, ChreMessage::EndpointSessionMessage, msg.Union());
}

void HostProtocolHostV4::encodeEndpointSessionMessageDeliveryStatus(
    FlatBufferBuilder &builder, int64_t hostHubId, uint16_t sessionId,
    const AidlMessageDeliveryStatus &status) {
  auto fbsStatus = ::chre::fbs::CreateMessageDeliveryStatus(
      builder, status.messageSequenceNumber,
      static_cast<int8_t>(status.errorCode));
  auto msg = ::chre::fbs::CreateEndpointSessionMessageDeliveryStatus(
      builder, hostHubId, sessionId, fbsStatus);
  finalize(builder, ChreMessage::EndpointSessionMessageDeliveryStatus,
           msg.Union());
}

void HostProtocolHostV4::decodeGetMessageHubsAndEndpointsResponse(
    const ::chre::fbs::GetMessageHubsAndEndpointsResponseT &msg,
    std::vector<AidlHubInfo> &hubs, std::vector<AidlEndpointInfo> &endpoints) {
  for (const auto &hub : msg.hubs) hubs.push_back(fbsMessageHubToAidl(*hub));
  for (const auto &endpoint : msg.endpoints)
    endpoints.push_back(fbsEndpointInfoToAidl(*endpoint));
}

void HostProtocolHostV4::decodeRegisterMessageHub(
    const ::chre::fbs::RegisterMessageHubT &msg, AidlHubInfo &info) {
  info = fbsMessageHubToAidl(*msg.hub);
}

void HostProtocolHostV4::decodeUnregisterMessageHub(
    const ::chre::fbs::UnregisterMessageHubT &msg, int64_t &id) {
  id = msg.id;
}

void HostProtocolHostV4::decodeRegisterEndpoint(
    const ::chre::fbs::RegisterEndpointT &msg, AidlEndpointInfo &info) {
  info = fbsEndpointInfoToAidl(*msg.endpoint);
}

void HostProtocolHostV4::decodeUnregisterEndpoint(
    const ::chre::fbs::UnregisterEndpointT &msg, AidlEndpointId &id) {
  id = fbsEndpointIdToAidl(*msg.endpoint);
}

void HostProtocolHostV4::decodeOpenEndpointSessionRequest(
    const ::chre::fbs::OpenEndpointSessionRequestT &msg, int64_t &hubId,
    uint16_t &sessionId, AidlEndpointId &hostEndpoint,
    AidlEndpointId &embeddedEndpoint,
    std::optional<std::string> &serviceDescriptor) {
  hubId = msg.host_hub_id;
  sessionId = msg.session_id;
  hostEndpoint = fbsEndpointIdToAidl(*msg.toEndpoint);
  embeddedEndpoint = fbsEndpointIdToAidl(*msg.fromEndpoint);
  auto *serviceCStr =
      ::android::chre::getStringFromByteVector(msg.serviceDescriptor);
  if (serviceCStr) serviceDescriptor = std::string(serviceCStr);
}

void HostProtocolHostV4::decodeEndpointSessionOpened(
    const ::chre::fbs::EndpointSessionOpenedT &msg, int64_t &hubId,
    uint16_t &sessionId) {
  hubId = msg.host_hub_id;
  sessionId = msg.session_id;
}

void HostProtocolHostV4::decodeEndpointSessionClosed(
    const ::chre::fbs::EndpointSessionClosedT &msg, int64_t &hubId,
    uint16_t &sessionId, AidlReason &reason) {
  hubId = msg.host_hub_id;
  sessionId = msg.session_id;
  reason = static_cast<AidlReason>(msg.reason);
}

void HostProtocolHostV4::decodeEndpointSessionMessage(
    const ::chre::fbs::EndpointSessionMessageT &msg, int64_t &hubId,
    uint16_t &sessionId, AidlMessage &message) {
  hubId = msg.host_hub_id;
  sessionId = msg.session_id;
  message = {.flags = static_cast<int32_t>(msg.flags),
             .sequenceNumber = static_cast<int32_t>(msg.sequence_number),
             .permissions = chreToAndroidPermissions(msg.permissions),
             .type = static_cast<int32_t>(msg.type),
             .content = msg.data};
}

void HostProtocolHostV4::decodeEndpointSessionMessageDeliveryStatus(
    const ::chre::fbs::EndpointSessionMessageDeliveryStatusT &msg,
    int64_t &hubId, uint16_t &sessionId, AidlMessageDeliveryStatus &status) {
  hubId = msg.host_hub_id;
  sessionId = msg.session_id;
  status = {.messageSequenceNumber =
                static_cast<int32_t>(msg.status->message_sequence_number),
            .errorCode = static_cast<AidlErrorCode>(msg.status->error_code)};
}

Offset<MessageHub> HostProtocolHostV4::aidlToFbsMessageHub(
    FlatBufferBuilder &builder, const AidlHubInfo &info) {
  MessageHubDetails detailsEnum;
  Offset<void> detailsUnion;
  switch (info.hubDetails.getTag()) {
    case AidlHubInfo::HubDetails::Tag::contextHubInfo: {
      detailsEnum = MessageHubDetails::HubInfoResponse;
      const auto &contextHub =
          info.hubDetails.get<AidlHubInfo::HubDetails::Tag::contextHubInfo>();
      uint32_t chrePlatformVersion =
          (static_cast<uint32_t>(contextHub.chreApiMajorVersion) << 24) |
          (static_cast<uint32_t>(contextHub.chreApiMinorVersion) << 16) |
          static_cast<uint32_t>(contextHub.chrePatchVersion);
      detailsUnion =
          ::chre::fbs::CreateHubInfoResponse(
              builder, addStringAsByteVector(builder, contextHub.name.c_str()),
              addStringAsByteVector(builder, contextHub.vendor.c_str()),
              addStringAsByteVector(builder, contextHub.toolchain.c_str()),
              /*platform_version=*/0, /*toolchain_version=*/0,
              contextHub.peakMips,
              /*stopped_power=*/0.0f, /*sleep_power=*/0.0f, /*peak_power=*/0.0f,
              contextHub.maxSupportedMessageLengthBytes,
              contextHub.chrePlatformId, chrePlatformVersion,
              contextHub.supportsReliableMessages)
              .Union();
    } break;
    case AidlHubInfo::HubDetails::Tag::vendorHubInfo: {
      detailsEnum = MessageHubDetails::VendorHubInfo;
      const auto &vendorHub =
          info.hubDetails.get<AidlHubInfo::HubDetails::Tag::vendorHubInfo>();
      detailsUnion =
          ::chre::fbs::CreateVendorHubInfo(
              builder, addStringAsByteVector(builder, vendorHub.name.c_str()),
              vendorHub.version, /*extended_info=*/0)
              .Union();
    } break;
  }
  return ::chre::fbs::CreateMessageHub(builder, info.hubId, detailsEnum,
                                       detailsUnion);
}

namespace {

std::string stringFromBytes(const std::vector<int8_t> &bytes) {
  auto *cStr = ::android::chre::getStringFromByteVector(bytes);
  return cStr ? std::string(cStr) : std::string();
}

}  // namespace

AidlHubInfo HostProtocolHostV4::fbsMessageHubToAidl(
    const ::chre::fbs::MessageHubT &hub) {
  AidlHubInfo info{.hubId = hub.id};
  if (hub.details.type == MessageHubDetails::HubInfoResponse) {
    const auto &fbsContextHub = *hub.details.AsHubInfoResponse();
    AidlContextHubInfo contextHub = {
        .name = stringFromBytes(fbsContextHub.name),
        .vendor = stringFromBytes(fbsContextHub.vendor),
        .toolchain = stringFromBytes(fbsContextHub.toolchain),
        .peakMips = fbsContextHub.peak_mips,
        .maxSupportedMessageLengthBytes =
            static_cast<int32_t>(fbsContextHub.max_msg_len),
        .chrePlatformId = static_cast<int64_t>(fbsContextHub.platform_id),
        .chreApiMajorVersion = static_cast<int8_t>(
            (fbsContextHub.chre_platform_version >> 24) & 0xff),
        .chreApiMinorVersion = static_cast<int8_t>(
            (fbsContextHub.chre_platform_version >> 16) & 0xff),
        .chrePatchVersion =
            static_cast<char16_t>(fbsContextHub.chre_platform_version & 0xffff),
        .supportsReliableMessages = fbsContextHub.supports_reliable_messages};
    info.hubDetails = AidlHubInfo::HubDetails(std::move(contextHub));
  } else {
    const auto &fbsVendorHub = *hub.details.AsVendorHubInfo();
    AidlVendorHubInfo vendorHub = {
        .name = stringFromBytes(fbsVendorHub.name),
        .version = static_cast<int32_t>(fbsVendorHub.version)};
    info.hubDetails = AidlHubInfo::HubDetails(std::move(vendorHub));
  }
  return info;
}

Offset<EndpointInfo> HostProtocolHostV4::aidlToFbsEndpointInfo(
    FlatBufferBuilder &builder, const AidlEndpointInfo &info) {
  std::vector<Offset<::chre::fbs::Service>> services;
  for (const auto &service : info.services) {
    services.push_back(::chre::fbs::CreateService(
        builder, static_cast<::chre::fbs::RpcFormat>(service.format),
        addStringAsByteVector(builder, service.serviceDescriptor.c_str()),
        service.majorVersion, service.minorVersion));
  }
  auto servicesVector = builder.CreateVector(services);
  return ::chre::fbs::CreateEndpointInfo(
      builder, aidlToFbsEndpointId(builder, info.id),
      static_cast<::chre::fbs::EndpointType>(info.type),
      addStringAsByteVector(builder, info.name.c_str()), info.version,
      androidToChrePermissions(info.requiredPermissions), servicesVector);
}

AidlEndpointInfo HostProtocolHostV4::fbsEndpointInfoToAidl(
    const ::chre::fbs::EndpointInfoT &endpoint) {
  AidlEndpointInfo info = {
      .id = fbsEndpointIdToAidl(*endpoint.id),
      .type = static_cast<AidlEndpointInfo::EndpointType>(endpoint.type),
      .name = stringFromBytes(endpoint.name),
      .version = static_cast<int32_t>(endpoint.version),
      .requiredPermissions =
          chreToAndroidPermissions(endpoint.required_permissions)};
  for (const auto &service : endpoint.services) {
    info.services.push_back(
        {.format = static_cast<AidlRpcFormat>(service->format),
         .serviceDescriptor = stringFromBytes(service->descriptor),
         .majorVersion = static_cast<int32_t>(service->major_version),
         .minorVersion = static_cast<int32_t>(service->minor_version)});
  }
  return info;
}

Offset<EndpointId> HostProtocolHostV4::aidlToFbsEndpointId(
    FlatBufferBuilder &builder, const AidlEndpointId &id) {
  return ::chre::fbs::CreateEndpointId(builder, id.hubId, id.id);
}

AidlEndpointId HostProtocolHostV4::fbsEndpointIdToAidl(
    const ::chre::fbs::EndpointIdT &endpoint) {
  return AidlEndpointId{.id = endpoint.id, .hubId = endpoint.hubId};
}

}  // namespace android::hardware::contexthub::common::implementation
