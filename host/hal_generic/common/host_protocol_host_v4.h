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

#include <string>
#include <vector>

#include <aidl/android/hardware/contexthub/BnContextHub.h>
#include <aidl/android/hardware/contexthub/BnEndpointCommunication.h>

#include "chre/platform/shared/host_protocol_common.h"
#include "chre_host/generated/host_messages_generated.h"
#include "flatbuffers/flatbuffers.h"

namespace android::hardware::contexthub::common::implementation {

using AidlEndpointId = ::aidl::android::hardware::contexthub::EndpointId;
using AidlEndpointInfo = ::aidl::android::hardware::contexthub::EndpointInfo;
using AidlHubInfo = ::aidl::android::hardware::contexthub::HubInfo;
using AidlMessage = ::aidl::android::hardware::contexthub::Message;
using AidlMessageDeliveryStatus =
    ::aidl::android::hardware::contexthub::MessageDeliveryStatus;
using AidlReason = ::aidl::android::hardware::contexthub::Reason;
using AidlService = ::aidl::android::hardware::contexthub::Service;

/** Helpers for converting ContextHub V4+ AIDL <-> CHRE flatbuffer messages */
class HostProtocolHostV4 : public ::chre::HostProtocolCommon {
 public:
  /**
   * Encodes a GetMessageHubsAndEndpointsRequest message
   *
   * @param builder The builder used to encode the message
   */
  static void encodeGetMessageHubsAndEndpointsRequest(
      flatbuffers::FlatBufferBuilder &builder);

  /**
   * Encodes a GetMessageHubsAndEndpointsResponse message
   *
   * @param builder The builder used to encode the message
   * @param hubs The list of known host message hubs
   * @param endpoints The list of known host endpoints
   */
  static void encodeGetMessageHubsAndEndpointsResponse(
      flatbuffers::FlatBufferBuilder &builder,
      const std::vector<AidlHubInfo> &hubs,
      const std::vector<AidlEndpointInfo> &endpoints);

  /**
   * Encodes a RegisterMessageHub message
   *
   * @param builder The builder used to encode the message
   * @param hub The host message hub being registered
   */
  static void encodeRegisterMessageHub(flatbuffers::FlatBufferBuilder &builder,
                                       const AidlHubInfo &info);

  /**
   * Encodes a UnregisterMessageHub message
   *
   * @param builder The builder used to encode the message
   * @param id The id of the host message hub being unregistered
   */
  static void encodeUnregisterMessageHub(
      flatbuffers::FlatBufferBuilder &builder, int64_t id);

  /**
   * Encodes a RegisterEndpoint message
   *
   * @param builder The builder used to encode the message
   * @param info The host endpoint being registered
   */
  static void encodeRegisterEndpoint(flatbuffers::FlatBufferBuilder &builder,
                                     const AidlEndpointInfo &info);

  /**
   * Encodes a UnregisterEndpoint message
   *
   * @param builder The builder used to encode the message
   * @param id The id of the host endpoint being unregistered
   */
  static void encodeUnregisterEndpoint(flatbuffers::FlatBufferBuilder &builder,
                                       const AidlEndpointId &id);

  /**
   * Encodes a OpenEndpointSessionRequest message
   *
   * @param builder The builder used to encode the message
   * @param hostHubId The id of the host hub originating the request
   * @param sessionId The id of the session to be created
   * @param initiator The id of the host endpoint
   * @param destination The id of the embedded endpoint
   * @param serviceDescriptor The descriptor of the service for the session
   */
  static void encodeOpenEndpointSessionRequest(
      flatbuffers::FlatBufferBuilder &builder, int64_t hostHubId,
      uint16_t sessionId, const AidlEndpointId &initiator,
      const AidlEndpointId &destination,
      const std::optional<std::string> &serviceDescriptor);

  /**
   * Encodes a EndpointSessionOpened message
   *
   * @param builder The builder used to encode the message
   * @param hostHubId The id of the host hub originating the notification
   * @param sessionId The id of the session that was accepted
   */
  static void encodeEndpointSessionOpened(
      flatbuffers::FlatBufferBuilder &builder, int64_t hostHubId,
      uint16_t sessionId);

  /**
   * Encodes a EndpointSessionClosed message
   *
   * @param builder The builder used to encode the message
   * @param hostHubId The id of the host hub originating the notification
   * @param sessionId The id of the session that was closed
   * @param reason The reason for the closure
   */
  static void encodeEndpointSessionClosed(
      flatbuffers::FlatBufferBuilder &builder, int64_t hostHubId,
      uint16_t sessionId, AidlReason reason);

  /**
   * Encodes a EndpointSessionMessage message
   *
   * @param builder The builder used to encode the message
   * @param hostHubId The id of the host hub originating the message
   * @param sessionId The id of the session over which the message was sent
   * @param message The message being sent
   */
  static void encodeEndpointSessionMessage(
      flatbuffers::FlatBufferBuilder &builder, int64_t hostHubId,
      uint16_t sessionId, const AidlMessage &message);

  /**
   * Encodes a EndpointSessionMessageDeliveryStatus message
   *
   * @param builder The builder used to encode the message
   * @param hostHubId The id of the host hub originating the notification
   * @param sessionId The id of the session over which the message was sent
   * @param status The status of the message delivery
   */
  static void encodeEndpointSessionMessageDeliveryStatus(
      flatbuffers::FlatBufferBuilder &builder, int64_t hostHubId,
      uint16_t sessionId, const AidlMessageDeliveryStatus &status);

  /**
   * Decodes a GetMessageHubsAndEndpointsResponse message
   *
   * @param msg The message
   * @param hubs (out) The list of embedded message hubs
   * @param endpoints (out) The list of embedded endpoints
   */
  static void decodeGetMessageHubsAndEndpointsResponse(
      const ::chre::fbs::GetMessageHubsAndEndpointsResponseT &msg,
      std::vector<AidlHubInfo> &hubs, std::vector<AidlEndpointInfo> &endpoints);

  /**
   * Decodes a RegisterMessageHub message
   *
   * @param msg The message
   * @param info (out) The details of the new embedded message hub
   */
  static void decodeRegisterMessageHub(
      const ::chre::fbs::RegisterMessageHubT &msg, AidlHubInfo &info);

  /**
   * Decodes a UnregisterMessageHub message
   *
   * @param msg The message
   * @param id (out) The id of the unregistered embedded hub
   */
  static void decodeUnregisterMessageHub(
      const ::chre::fbs::UnregisterMessageHubT &msg, int64_t &id);

  /**
   * Decodes a RegisterEndpoint message
   *
   * @param msg The message
   * @param info (out) The details of the new embedded endpoint
   */
  static void decodeRegisterEndpoint(const ::chre::fbs::RegisterEndpointT &msg,
                                     AidlEndpointInfo &info);

  /**
   * Decodes a AddServiceToEndpoint message
   *
   * @param msg The message
   * @param info (out) The id of the new embedded endpoint
   * @param service (out) The service being added
   */
  static void decodeAddServiceToEndpoint(
      const ::chre::fbs::AddServiceToEndpointT &msg, AidlEndpointId &id,
      AidlService &service);

  /**
   * Decodes a EndpointReady message
   *
   * @param msg The message
   * @param info (out) The id of the newly ready embedded endpoint
   */
  static void decodeEndpointReady(const ::chre::fbs::EndpointReadyT &msg,
                                  AidlEndpointId &id);

  /**
   * Decodes a UnregisterEndpoint message
   *
   * @param msg The message
   * @param info (out) The id of the unregistered embedded endpoint
   */
  static void decodeUnregisterEndpoint(
      const ::chre::fbs::UnregisterEndpointT &msg, AidlEndpointId &id);

  /**
   * Decodes a OpenEndpointSessionRequest message
   *
   * @param msg The message
   * @param hubId (out) The destination host message hub id
   * @param sessionId (out) The id of the session being requested
   * @param hostEndpoint (out) The destination host endpoint id
   * @param embeddedEndpoint (out) The initating embedded endpoint id
   * @param serviceDescriptor (out) The service requested for the session
   */
  static void decodeOpenEndpointSessionRequest(
      const ::chre::fbs::OpenEndpointSessionRequestT &msg, int64_t &hubId,
      uint16_t &sessionId, AidlEndpointId &hostEndpoint,
      AidlEndpointId &embeddedEndpoint,
      std::optional<std::string> &serviceDescriptor);

  /**
   * Decodes a EndpointSessionOpened message
   *
   * @param msg The message
   * @param hubId (out) The id of the destination host message hub
   * @param sessionId (out) The id of the accepted session
   */
  static void decodeEndpointSessionOpened(
      const ::chre::fbs::EndpointSessionOpenedT &msg, int64_t &hubId,
      uint16_t &sessionId);

  /**
   * Decodes a EndpointSessionClosed message
   *
   * @param msg The message
   * @param hubId (out) The id of the destination host message hub
   * @param sessionId (out) The id of the closed session
   */
  static void decodeEndpointSessionClosed(
      const ::chre::fbs::EndpointSessionClosedT &msg, int64_t &hubId,
      uint16_t &sessionId, AidlReason &reason);

  /**
   * Decodes a EndpointSessionMessage message
   *
   * @param msg The wrapper message
   * @param hubId (out) The id of the destination host message hub
   * @param sessionId (out) The id of the session hosting the message
   * @param message (out) The containined message
   */
  static void decodeEndpointSessionMessage(
      const ::chre::fbs::EndpointSessionMessageT &msg, int64_t &hubId,
      uint16_t &sessionId, AidlMessage &message);

  /**
   * Decodes a EndpointSessionMessageDeliveryStatus message
   *
   * @param msg The wrapper message
   * @param hubId (out) The id of the destination host message hub
   * @param sessionId (out) The id of the session hosting the message
   * @param status (out) The result of a message sent over sessionId
   */
  static void decodeEndpointSessionMessageDeliveryStatus(
      const ::chre::fbs::EndpointSessionMessageDeliveryStatusT &msg,
      int64_t &hubId, uint16_t &sessionId, AidlMessageDeliveryStatus &status);

 private:
  static flatbuffers::Offset<::chre::fbs::MessageHub> aidlToFbsMessageHub(
      flatbuffers::FlatBufferBuilder &builder, const AidlHubInfo &info);

  static AidlHubInfo fbsMessageHubToAidl(const ::chre::fbs::MessageHubT &hub);

  static flatbuffers::Offset<::chre::fbs::EndpointInfo> aidlToFbsEndpointInfo(
      flatbuffers::FlatBufferBuilder &builder, const AidlEndpointInfo &info);

  static AidlEndpointInfo fbsEndpointInfoToAidl(
      const ::chre::fbs::EndpointInfoT &endpoint);

  static flatbuffers::Offset<::chre::fbs::EndpointId> aidlToFbsEndpointId(
      flatbuffers::FlatBufferBuilder &builder, const AidlEndpointId &id);

  static AidlEndpointId fbsEndpointIdToAidl(
      const ::chre::fbs::EndpointIdT &endpoint);
};

}  // namespace android::hardware::contexthub::common::implementation
