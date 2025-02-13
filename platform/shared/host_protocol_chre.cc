/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "chre/platform/shared/host_protocol_chre.h"

#include <inttypes.h>
#include <string.h>
#include <cstdint>

#include "chre/core/event_loop_manager.h"
#include "chre/core/host_endpoint_manager.h"
#include "chre/core/host_message_hub_manager.h"
#include "chre/platform/log.h"
#include "chre/platform/shared/generated/host_messages_generated.h"
#include "chre/util/macros.h"
#include "chre/util/system/message_common.h"

using flatbuffers::Offset;
using flatbuffers::Vector;

namespace chre {

// This is similar to getStringFromByteVector in host_protocol_host.h. Ensure
// that method's implementation is kept in sync with this.
const char *getStringFromByteVector(const flatbuffers::Vector<int8_t> *vec) {
  constexpr int8_t kNullChar = static_cast<int8_t>('\0');
  const char *str = nullptr;

  // Check that the vector is present, non-empty, and null-terminated
  if (vec != nullptr && vec->size() > 0 &&
      (*vec)[vec->size() - 1] == kNullChar) {
    str = reinterpret_cast<const char *>(vec->Data());
  }

  return str;
}

#ifdef CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED
using ::chre::message::EndpointId;
using ::chre::message::EndpointInfo;
using ::chre::message::EndpointType;
using ::chre::message::MessageHubId;
using ::chre::message::MessageHubInfo;
using ::chre::message::Reason;
using ::chre::message::Session;
using ::chre::message::SessionId;

namespace {
HostMessageHubManager &getHostHubManager() {
  return EventLoopManagerSingleton::get()->getHostMessageHubManager();
}
}  // namespace
#endif  // CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED

bool HostProtocolChre::decodeMessageFromHost(const void *message,
                                             size_t messageLen) {
  bool success = verifyMessage(message, messageLen);
  if (!success) {
    LOGE("Dropping invalid/corrupted message from host (length %zu)",
         messageLen);
  } else {
    const fbs::MessageContainer *container = fbs::GetMessageContainer(message);
    uint16_t hostClientId = container->host_addr()->client_id();

    switch (container->message_type()) {
      case fbs::ChreMessage::NanoappMessage: {
        const auto *nanoappMsg =
            static_cast<const fbs::NanoappMessage *>(container->message());
        // Required field; verifier ensures that this is not null (though it
        // may be empty)
        const flatbuffers::Vector<uint8_t> *msgData = nanoappMsg->message();
        HostMessageHandlers::handleNanoappMessage(
            nanoappMsg->app_id(), nanoappMsg->message_type(),
            nanoappMsg->host_endpoint(), msgData->data(), msgData->size(),
            nanoappMsg->is_reliable(), nanoappMsg->message_sequence_number());
        break;
      }

      case fbs::ChreMessage::MessageDeliveryStatus: {
        const auto *status = static_cast<const fbs::MessageDeliveryStatus *>(
            container->message());
        HostMessageHandlers::handleMessageDeliveryStatus(
            status->message_sequence_number(), status->error_code());
        break;
      }

      case fbs::ChreMessage::HubInfoRequest:
        HostMessageHandlers::handleHubInfoRequest(hostClientId);
        break;

      case fbs::ChreMessage::NanoappListRequest:
        HostMessageHandlers::handleNanoappListRequest(hostClientId);
        break;

      case fbs::ChreMessage::LoadNanoappRequest: {
        const auto *request =
            static_cast<const fbs::LoadNanoappRequest *>(container->message());
        const flatbuffers::Vector<uint8_t> *appBinary = request->app_binary();
        const char *appBinaryFilename =
            getStringFromByteVector(request->app_binary_file_name());
        HostMessageHandlers::handleLoadNanoappRequest(
            hostClientId, request->transaction_id(), request->app_id(),
            request->app_version(), request->app_flags(),
            request->target_api_version(), appBinary->data(), appBinary->size(),
            appBinaryFilename, request->fragment_id(),
            request->total_app_size(), request->respond_before_start());
        break;
      }

      case fbs::ChreMessage::UnloadNanoappRequest: {
        const auto *request = static_cast<const fbs::UnloadNanoappRequest *>(
            container->message());
        HostMessageHandlers::handleUnloadNanoappRequest(
            hostClientId, request->transaction_id(), request->app_id(),
            request->allow_system_nanoapp_unload());
        break;
      }

      case fbs::ChreMessage::TimeSyncMessage: {
        const auto *request =
            static_cast<const fbs::TimeSyncMessage *>(container->message());
        HostMessageHandlers::handleTimeSyncMessage(request->offset());
        break;
      }

      case fbs::ChreMessage::DebugDumpRequest:
        HostMessageHandlers::handleDebugDumpRequest(hostClientId);
        break;

      case fbs::ChreMessage::SettingChangeMessage: {
        const auto *settingMessage =
            static_cast<const fbs::SettingChangeMessage *>(
                container->message());
        HostMessageHandlers::handleSettingChangeMessage(
            settingMessage->setting(), settingMessage->state());
        break;
      }

      case fbs::ChreMessage::SelfTestRequest: {
        HostMessageHandlers::handleSelfTestRequest(hostClientId);
        break;
      }

      case fbs::ChreMessage::HostEndpointConnected: {
        const auto *connectedMessage =
            static_cast<const fbs::HostEndpointConnected *>(
                container->message());
        struct chreHostEndpointInfo info;
        info.hostEndpointId = connectedMessage->host_endpoint();
        info.hostEndpointType = connectedMessage->type();
        if (strlen(reinterpret_cast<const char *>(
                connectedMessage->package_name()->data())) > 0) {
          info.isNameValid = true;
          memcpy(&info.packageName[0], connectedMessage->package_name()->data(),
                 MIN(connectedMessage->package_name()->size(),
                     CHRE_MAX_ENDPOINT_NAME_LEN));
          info.packageName[CHRE_MAX_ENDPOINT_NAME_LEN - 1] = '\0';
        } else {
          info.isNameValid = false;
        }
        if (strlen(reinterpret_cast<const char *>(
                connectedMessage->attribution_tag()->data())) > 0) {
          info.isTagValid = true;
          memcpy(&info.attributionTag[0],
                 connectedMessage->attribution_tag()->data(),
                 MIN(connectedMessage->attribution_tag()->size(),
                     CHRE_MAX_ENDPOINT_TAG_LEN));
          info.attributionTag[CHRE_MAX_ENDPOINT_NAME_LEN - 1] = '\0';
        } else {
          info.isTagValid = false;
        }

        EventLoopManagerSingleton::get()
            ->getHostEndpointManager()
            .postHostEndpointConnected(info);
        break;
      }

      case fbs::ChreMessage::HostEndpointDisconnected: {
        const auto *disconnectedMessage =
            static_cast<const fbs::HostEndpointDisconnected *>(
                container->message());
        EventLoopManagerSingleton::get()
            ->getHostEndpointManager()
            .postHostEndpointDisconnected(disconnectedMessage->host_endpoint());
        break;
      }

      case fbs::ChreMessage::NanConfigurationUpdate: {
        const auto *nanConfigUpdateMessage =
            static_cast<const fbs::NanConfigurationUpdate *>(
                container->message());
        HostMessageHandlers::handleNanConfigurationUpdate(
            nanConfigUpdateMessage->enabled());
        break;
      }

      case fbs::ChreMessage::DebugConfiguration: {
        const auto *debugConfiguration =
            static_cast<const fbs::DebugConfiguration *>(container->message());
        HostMessageHandlers::handleDebugConfiguration(debugConfiguration);
        break;
      }

      case fbs::ChreMessage::PulseRequest: {
        HostMessageHandlers::handlePulseRequest();
        break;
      }

      case fbs::ChreMessage::BtSocketOpen: {
        const auto *btSocketOpen =
            static_cast<const fbs::BtSocketOpen *>(container->message());
        if (btSocketOpen->channelInfo_type() !=
            fbs::ChannelInfo::LeCocChannelInfo) {
          LOGW("Unexpected BT Socket Open Channel Info Type %" PRIu8,
               static_cast<uint8_t>(btSocketOpen->channelInfo_type()));
        } else {
          const auto *leCocChannelInfo =
              static_cast<const fbs::LeCocChannelInfo *>(
                  btSocketOpen->channelInfo());
          const char *name = getStringFromByteVector(btSocketOpen->name());
          HostMessageHandlers::handleBtSocketOpen(
              static_cast<uint64_t>(btSocketOpen->hubId()),
              BleL2capCocSocketData{
                  .socketId = static_cast<uint64_t>(btSocketOpen->socketId()),
                  .endpointId =
                      static_cast<uint64_t>(btSocketOpen->endpointId()),
                  .connectionHandle = static_cast<uint16_t>(
                      btSocketOpen->aclConnectionHandle()),
                  .hostClientId = hostClientId,
                  .rxConfig =
                      L2capCocConfig{.cid = static_cast<uint16_t>(
                                         leCocChannelInfo->localCid()),
                                     .mtu = static_cast<uint16_t>(
                                         leCocChannelInfo->localMtu()),
                                     .mps = static_cast<uint16_t>(
                                         leCocChannelInfo->localMps()),
                                     .credits = static_cast<uint16_t>(
                                         leCocChannelInfo->initialRxCredits())},
                  .txConfig =
                      L2capCocConfig{.cid = static_cast<uint16_t>(
                                         leCocChannelInfo->remoteCid()),
                                     .mtu = static_cast<uint16_t>(
                                         leCocChannelInfo->remoteMtu()),
                                     .mps = static_cast<uint16_t>(
                                         leCocChannelInfo->remoteMps()),
                                     .credits = static_cast<uint16_t>(
                                         leCocChannelInfo->initialTxCredits())},
              },
              name, static_cast<uint32_t>(leCocChannelInfo->psm()));
          success = true;
        }
        break;
      }

      case fbs::ChreMessage::BtSocketCloseResponse: {
        const auto *btSocketCloseResponse =
            static_cast<const fbs::BtSocketCloseResponse *>(
                container->message());
        LOGD("Received BT Socket close response for socketId=%" PRIu64,
             btSocketCloseResponse->socketId());
        success = true;
        break;
      }

#ifdef CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED
      case fbs::ChreMessage::GetMessageHubsAndEndpointsRequest:
        getHostHubManager().reset();
        break;

      case fbs::ChreMessage::RegisterMessageHub: {
        const auto *msg =
            static_cast<const fbs::RegisterMessageHub *>(container->message());
        MessageHubInfo hub{.id = static_cast<MessageHubId>(msg->hub()->id())};
        if (msg->hub()->details_type() ==
            fbs::MessageHubDetails::VendorHubInfo) {
          hub.name = getStringFromByteVector(
              msg->hub()->details_as_VendorHubInfo()->name());
        } else {
          hub.name = getStringFromByteVector(
              msg->hub()->details_as_HubInfoResponse()->name());
        }
        getHostHubManager().registerHub(hub);
        break;
      }

      case fbs::ChreMessage::UnregisterMessageHub: {
        const auto *msg = static_cast<const fbs::UnregisterMessageHub *>(
            container->message());
        getHostHubManager().unregisterHub(msg->id());
        break;
      }

      case fbs::ChreMessage::RegisterEndpoint: {
        const auto *fbsEndpoint =
            static_cast<const fbs::RegisterEndpoint *>(container->message())
                ->endpoint();
        auto *maybeName = getStringFromByteVector(fbsEndpoint->name());
        EndpointInfo endpoint(fbsEndpoint->id()->id(),
                              maybeName ? maybeName : "",
                              fbsEndpoint->version(),
                              static_cast<EndpointType>(fbsEndpoint->type()),
                              fbsEndpoint->required_permissions());
        getHostHubManager().registerEndpoint(fbsEndpoint->id()->hubId(),
                                             endpoint);
        break;
      }

      case fbs::ChreMessage::UnregisterEndpoint: {
        const auto *msg =
            static_cast<const fbs::UnregisterEndpoint *>(container->message());
        getHostHubManager().unregisterEndpoint(msg->endpoint()->hubId(),
                                               msg->endpoint()->id());
        break;
      }

      case fbs::ChreMessage::OpenEndpointSessionRequest: {
        const auto *msg = static_cast<const fbs::OpenEndpointSessionRequest *>(
            container->message());
        getHostHubManager().openSession(
            msg->fromEndpoint()->hubId(), msg->fromEndpoint()->id(),
            msg->toEndpoint()->hubId(), msg->toEndpoint()->id(),
            msg->session_id(),
            getStringFromByteVector(msg->serviceDescriptor()));
        break;
      }

      case fbs::ChreMessage::EndpointSessionOpened: {
        const auto *msg = static_cast<const fbs::EndpointSessionOpened *>(
            container->message());
        getHostHubManager().ackSession(msg->host_hub_id(), msg->session_id());
        break;
      }

      case fbs::ChreMessage::EndpointSessionClosed: {
        const auto *msg = static_cast<const fbs::EndpointSessionClosed *>(
            container->message());
        getHostHubManager().closeSession(msg->host_hub_id(), msg->session_id(),
                                         static_cast<Reason>(msg->reason()));
        break;
      }

      case fbs::ChreMessage::EndpointSessionMessage: {
        const auto *msg = static_cast<const fbs::EndpointSessionMessage *>(
            container->message());
        pw::span<const std::byte> data = {
            reinterpret_cast<const std::byte *>(msg->data()->data()),
            msg->data()->size()};
        getHostHubManager().sendMessage(msg->host_hub_id(), msg->session_id(),
                                        data, msg->type(), msg->permissions());
        break;
      }

#endif  // CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED

      default:
        LOGW("Got invalid/unexpected message type %" PRIu8,
             static_cast<uint8_t>(container->message_type()));
        success = false;
    }
  }
  return success;
}

void HostProtocolChre::encodeHubInfoResponse(
    ChreFlatBufferBuilder &builder, const char *name, const char *vendor,
    const char *toolchain, uint32_t legacyPlatformVersion,
    uint32_t legacyToolchainVersion, float peakMips, float stoppedPower,
    float sleepPower, float peakPower, uint32_t maxMessageLen,
    uint64_t platformId, uint32_t version, uint16_t hostClientId,
    bool supportsReliableMessages) {
  auto nameOffset = addStringAsByteVector(builder, name);
  auto vendorOffset = addStringAsByteVector(builder, vendor);
  auto toolchainOffset = addStringAsByteVector(builder, toolchain);

  auto response = fbs::CreateHubInfoResponse(
      builder, nameOffset, vendorOffset, toolchainOffset, legacyPlatformVersion,
      legacyToolchainVersion, peakMips, stoppedPower, sleepPower, peakPower,
      maxMessageLen, platformId, version, supportsReliableMessages);
  finalize(builder, fbs::ChreMessage::HubInfoResponse, response.Union(),
           hostClientId);
}

void HostProtocolChre::addNanoappListEntry(
    ChreFlatBufferBuilder &builder,
    DynamicVector<Offset<fbs::NanoappListEntry>> &offsetVector, uint64_t appId,
    uint32_t appVersion, bool enabled, bool isSystemNanoapp,
    uint32_t appPermissions,
    const DynamicVector<struct chreNanoappRpcService> &rpcServices) {
  DynamicVector<Offset<fbs::NanoappRpcService>> rpcServiceList;
  for (const auto &service : rpcServices) {
    Offset<fbs::NanoappRpcService> offsetService =
        fbs::CreateNanoappRpcService(builder, service.id, service.version);
    if (!rpcServiceList.push_back(offsetService)) {
      LOGE("Couldn't push RPC service to list");
    }
  }

  auto vectorOffset =
      builder.CreateVector<Offset<fbs::NanoappRpcService>>(rpcServiceList);
  auto offset = fbs::CreateNanoappListEntry(builder, appId, appVersion, enabled,
                                            isSystemNanoapp, appPermissions,
                                            vectorOffset);

  if (!offsetVector.push_back(offset)) {
    LOGE("Couldn't push nanoapp list entry offset!");
  }
}

void HostProtocolChre::finishNanoappListResponse(
    ChreFlatBufferBuilder &builder,
    DynamicVector<Offset<fbs::NanoappListEntry>> &offsetVector,
    uint16_t hostClientId) {
  auto vectorOffset =
      builder.CreateVector<Offset<fbs::NanoappListEntry>>(offsetVector);
  auto response = fbs::CreateNanoappListResponse(builder, vectorOffset);
  finalize(builder, fbs::ChreMessage::NanoappListResponse, response.Union(),
           hostClientId);
}

void HostProtocolChre::encodePulseResponse(ChreFlatBufferBuilder &builder) {
  auto response = fbs::CreatePulseResponse(builder);
  finalize(builder, fbs::ChreMessage::PulseResponse, response.Union());
}

void HostProtocolChre::encodeLoadNanoappResponse(ChreFlatBufferBuilder &builder,
                                                 uint16_t hostClientId,
                                                 uint32_t transactionId,
                                                 bool success,
                                                 uint32_t fragmentId) {
  auto response = fbs::CreateLoadNanoappResponse(builder, transactionId,
                                                 success, fragmentId);
  finalize(builder, fbs::ChreMessage::LoadNanoappResponse, response.Union(),
           hostClientId);
}

void HostProtocolChre::encodeNanoappTokenDatabaseInfo(
    ChreFlatBufferBuilder &builder, uint16_t instanceId, uint64_t appId,
    uint32_t tokenDatabaseOffset, size_t tokenDatabaseSize) {
  auto response = fbs::CreateNanoappTokenDatabaseInfo(
      builder, instanceId, appId, tokenDatabaseOffset, tokenDatabaseSize);
  finalize(builder, fbs::ChreMessage::NanoappTokenDatabaseInfo,
           response.Union());
}

void HostProtocolChre::encodeUnloadNanoappResponse(
    ChreFlatBufferBuilder &builder, uint16_t hostClientId,
    uint32_t transactionId, bool success) {
  auto response =
      fbs::CreateUnloadNanoappResponse(builder, transactionId, success);
  finalize(builder, fbs::ChreMessage::UnloadNanoappResponse, response.Union(),
           hostClientId);
}

void HostProtocolChre::encodeLogMessages(ChreFlatBufferBuilder &builder,
                                         const uint8_t *logBuffer,
                                         size_t bufferSize) {
  auto logBufferOffset = builder.CreateVector(
      reinterpret_cast<const int8_t *>(logBuffer), bufferSize);
  auto message = fbs::CreateLogMessage(builder, logBufferOffset);
  finalize(builder, fbs::ChreMessage::LogMessage, message.Union());
}

void HostProtocolChre::encodeLogMessagesV2(ChreFlatBufferBuilder &builder,
                                           const uint8_t *logBuffer,
                                           size_t bufferSize,
                                           uint32_t numLogsDropped) {
  auto logBufferOffset = builder.CreateVector(
      reinterpret_cast<const int8_t *>(logBuffer), bufferSize);
  auto message =
      fbs::CreateLogMessageV2(builder, logBufferOffset, numLogsDropped);
  finalize(builder, fbs::ChreMessage::LogMessageV2, message.Union());
}

void HostProtocolChre::encodeDebugDumpData(ChreFlatBufferBuilder &builder,
                                           uint16_t hostClientId,
                                           const char *debugStr,
                                           size_t debugStrSize) {
  auto debugStrOffset = builder.CreateVector(
      reinterpret_cast<const int8_t *>(debugStr), debugStrSize);
  auto message = fbs::CreateDebugDumpData(builder, debugStrOffset);
  finalize(builder, fbs::ChreMessage::DebugDumpData, message.Union(),
           hostClientId);
}

void HostProtocolChre::encodeDebugDumpResponse(ChreFlatBufferBuilder &builder,
                                               uint16_t hostClientId,
                                               bool success,
                                               uint32_t dataCount) {
  auto response = fbs::CreateDebugDumpResponse(builder, success, dataCount);
  finalize(builder, fbs::ChreMessage::DebugDumpResponse, response.Union(),
           hostClientId);
}

void HostProtocolChre::encodeTimeSyncRequest(ChreFlatBufferBuilder &builder) {
  auto request = fbs::CreateTimeSyncRequest(builder);
  finalize(builder, fbs::ChreMessage::TimeSyncRequest, request.Union());
}

void HostProtocolChre::encodeLowPowerMicAccessRequest(
    ChreFlatBufferBuilder &builder) {
  auto request = fbs::CreateLowPowerMicAccessRequest(builder);
  finalize(builder, fbs::ChreMessage::LowPowerMicAccessRequest,
           request.Union());
}

void HostProtocolChre::encodeLowPowerMicAccessRelease(
    ChreFlatBufferBuilder &builder) {
  auto request = fbs::CreateLowPowerMicAccessRelease(builder);
  finalize(builder, fbs::ChreMessage::LowPowerMicAccessRelease,
           request.Union());
}

void HostProtocolChre::encodeSelfTestResponse(ChreFlatBufferBuilder &builder,
                                              uint16_t hostClientId,
                                              bool success) {
  auto response = fbs::CreateSelfTestResponse(builder, success);
  finalize(builder, fbs::ChreMessage::SelfTestResponse, response.Union(),
           hostClientId);
}

void HostProtocolChre::encodeMetricLog(ChreFlatBufferBuilder &builder,
                                       uint32_t metricId,
                                       const uint8_t *encodedMsg,
                                       size_t metricSize) {
  auto encodedMessage = builder.CreateVector(
      reinterpret_cast<const int8_t *>(encodedMsg), metricSize);
  auto message = fbs::CreateMetricLog(builder, metricId, encodedMessage);
  finalize(builder, fbs::ChreMessage::MetricLog, message.Union());
}

void HostProtocolChre::encodeNanConfigurationRequest(
    ChreFlatBufferBuilder &builder, bool enable) {
  auto request = fbs::CreateNanConfigurationRequest(builder, enable);
  finalize(builder, fbs::ChreMessage::NanConfigurationRequest, request.Union());
}

void HostProtocolChre::encodeBtSocketOpenResponse(
    ChreFlatBufferBuilder &builder, uint16_t hostClientId, uint64_t socketId,
    bool success, const char *reason) {
  auto reasonOffset = addStringAsByteVector(builder, reason);
  auto socketOpenResponse = fbs::CreateBtSocketOpenResponse(
      builder, socketId,
      success ? fbs::BtSocketOpenStatus::SUCCESS
              : fbs::BtSocketOpenStatus::FAILURE,
      reasonOffset);
  finalize(builder, fbs::ChreMessage::BtSocketOpenResponse,
           socketOpenResponse.Union(), hostClientId);
}

void HostProtocolChre::encodeBtSocketClose(ChreFlatBufferBuilder &builder,
                                           uint16_t hostClientId,
                                           uint64_t socketId,
                                           const char *reason) {
  auto reasonOffset = addStringAsByteVector(builder, reason);
  auto socketClose = fbs::CreateBtSocketClose(builder, socketId, reasonOffset);
  finalize(builder, fbs::ChreMessage::BtSocketClose, socketClose.Union(),
           hostClientId);
}

bool HostProtocolChre::getSettingFromFbs(fbs::Setting setting,
                                         Setting *chreSetting) {
  bool success = true;
  switch (setting) {
    case fbs::Setting::LOCATION:
      *chreSetting = Setting::LOCATION;
      break;
    case fbs::Setting::WIFI_AVAILABLE:
      *chreSetting = Setting::WIFI_AVAILABLE;
      break;
    case fbs::Setting::AIRPLANE_MODE:
      *chreSetting = Setting::AIRPLANE_MODE;
      break;
    case fbs::Setting::MICROPHONE:
      *chreSetting = Setting::MICROPHONE;
      break;
    case fbs::Setting::BLE_AVAILABLE:
      *chreSetting = Setting::BLE_AVAILABLE;
      break;
    default:
      LOGE("Unknown setting %" PRIu8, static_cast<uint8_t>(setting));
      success = false;
  }

  return success;
}

bool HostProtocolChre::getSettingEnabledFromFbs(fbs::SettingState state,
                                                bool *chreSettingEnabled) {
  bool success = true;
  switch (state) {
    case fbs::SettingState::DISABLED:
      *chreSettingEnabled = false;
      break;
    case fbs::SettingState::ENABLED:
      *chreSettingEnabled = true;
      break;
    default:
      LOGE("Unknown state %" PRIu8, static_cast<uint8_t>(state));
      success = false;
  }

  return success;
}

void HostProtocolChre::encodeGetMessageHubsAndEndpointsResponse(
    ChreFlatBufferBuilder &builder) {
  auto msg = fbs::CreateGetMessageHubsAndEndpointsResponse(builder);
  finalize(builder, fbs::ChreMessage::GetMessageHubsAndEndpointsResponse,
           msg.Union());
}

void HostProtocolChre::encodeRegisterMessageHub(
    ChreFlatBufferBuilder &builder, const message::MessageHubInfo &hub) {
  auto vendorHub = fbs::CreateVendorHubInfo(
      builder, addStringAsByteVector(builder, hub.name));
  auto fbsHub = fbs::CreateMessageHub(builder, hub.id,
                                      fbs::MessageHubDetails::VendorHubInfo,
                                      vendorHub.Union());
  auto msg = fbs::CreateRegisterMessageHub(builder, fbsHub);
  finalize(builder, fbs::ChreMessage::RegisterMessageHub, msg.Union());
}

void HostProtocolChre::encodeRegisterEndpoint(
    ChreFlatBufferBuilder &builder, message::MessageHubId hub,
    const message::EndpointInfo &endpoint) {
  auto id = fbs::CreateEndpointId(builder, hub, endpoint.id);
  auto info = fbs::CreateEndpointInfo(
      builder, id, static_cast<fbs::EndpointType>(endpoint.type),
      addStringAsByteVector(builder, endpoint.name), endpoint.version,
      endpoint.requiredPermissions);
  auto msg = fbs::CreateRegisterEndpoint(builder, info);
  finalize(builder, fbs::ChreMessage::RegisterEndpoint, msg.Union());
}

void HostProtocolChre::encodeUnregisterEndpoint(ChreFlatBufferBuilder &builder,
                                                message::MessageHubId hub,
                                                message::EndpointId endpoint) {
  auto id = fbs::CreateEndpointId(builder, hub, endpoint);
  auto msg = fbs::CreateUnregisterEndpoint(builder, id);
  finalize(builder, fbs::ChreMessage::UnregisterEndpoint, msg.Union());
}

void HostProtocolChre::encodeOpenEndpointSessionRequest(
    ChreFlatBufferBuilder &builder, const message::Session &session) {
  auto fromEndpoint = fbs::CreateEndpointId(
      builder, session.initiator.messageHubId, session.initiator.endpointId);
  auto toEndpoint = fbs::CreateEndpointId(builder, session.peer.messageHubId,
                                          session.peer.endpointId);
  auto msg = fbs::CreateOpenEndpointSessionRequest(
      builder, session.peer.messageHubId, session.sessionId, fromEndpoint,
      toEndpoint, addStringAsByteVector(builder, session.serviceDescriptor));
  finalize(builder, fbs::ChreMessage::OpenEndpointSessionRequest, msg.Union());
}

void HostProtocolChre::encodeEndpointSessionOpened(
    ChreFlatBufferBuilder &builder, message::MessageHubId hub,
    message::SessionId session) {
  auto msg = fbs::CreateEndpointSessionOpened(builder, hub, session);
  finalize(builder, fbs::ChreMessage::EndpointSessionOpened, msg.Union());
}

void HostProtocolChre::encodeEndpointSessionClosed(
    ChreFlatBufferBuilder &builder, message::MessageHubId hub,
    message::SessionId session, message::Reason reason) {
  auto msg = fbs::CreateEndpointSessionClosed(builder, hub, session,
                                              static_cast<fbs::Reason>(reason));
  finalize(builder, fbs::ChreMessage::EndpointSessionClosed, msg.Union());
}

void HostProtocolChre::encodeEndpointSessionMessage(
    ChreFlatBufferBuilder &builder, message::MessageHubId hub,
    message::SessionId session, pw::UniquePtr<std::byte[]> &&data,
    uint32_t type, uint32_t permissions) {
  auto dataVec = builder.CreateVector(reinterpret_cast<uint8_t *>(data.get()),
                                      data.size());
  auto msg = fbs::CreateEndpointSessionMessage(builder, hub, session, type,
                                               permissions, dataVec);
  finalize(builder, fbs::ChreMessage::EndpointSessionMessage, msg.Union());
}

}  // namespace chre
