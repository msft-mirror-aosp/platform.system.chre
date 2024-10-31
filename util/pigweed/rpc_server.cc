/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "chre/util/pigweed/rpc_server.h"

#include <cinttypes>
#include <cstdint>

#include "chre/util/nanoapp/log.h"
#include "chre/util/pigweed/rpc_helper.h"
#include "chre_api/chre.h"
#include "pw_status/status.h"

#ifndef LOG_TAG
#define LOG_TAG "[RpcServer]"
#endif  // LOG_TAG

namespace chre {

bool RpcServer::registerServices(size_t numServices,
                                 RpcServer::Service *services) {
  // Avoid blowing up the stack with chreServices.
  constexpr size_t kMaxServices = 8;

  if (numServices > kMaxServices) {
    LOGE("Can not register more than %zu services at once", kMaxServices);
    return false;
  }

  chreNanoappRpcService chreServices[kMaxServices];

  for (size_t i = 0; i < numServices; ++i) {
    const Service &service = services[i];

    if (mServer.IsServiceRegistered(service.service)) {
      return false;
    }

    chreServices[i] = {
        .id = service.id,
        .version = service.version,
    };

    mServer.RegisterService(service.service);
  }

  return chrePublishRpcServices(chreServices, numServices);
}

void RpcServer::setPermissionForNextMessage(uint32_t permission) {
  mPermission.set(permission);
}

bool RpcServer::handleEvent(uint32_t senderInstanceId, uint16_t eventType,
                            const void *eventData) {
  switch (eventType) {
    case CHRE_EVENT_MESSAGE_FROM_HOST:
      return handleMessageFromHost(eventData);
    case CHRE_EVENT_RPC_REQUEST:
      return handleMessageFromNanoapp(senderInstanceId, eventData);
    case CHRE_EVENT_HOST_ENDPOINT_NOTIFICATION:
      handleHostClientNotification(eventData);
      return true;
    case CHRE_EVENT_NANOAPP_STOPPED:
      handleNanoappStopped(eventData);
      return true;
    default:
      return true;
  }
}

void RpcServer::close() {
  chreConfigureNanoappInfoEvents(false);
  // TODO(b/251257328): Disable all notifications at once.
  while (!mConnectedHosts.empty()) {
    chreConfigureHostEndpointNotifications(mConnectedHosts[0], false);
    mConnectedHosts.erase(0);
  }
}

bool RpcServer::handleMessageFromHost(const void *eventData) {
  auto *hostMessage = static_cast<const chreMessageFromHostData *>(eventData);

  if (hostMessage->messageType != CHRE_MESSAGE_TYPE_RPC) {
    return false;
  }

  pw::span packet(static_cast<const std::byte *>(hostMessage->message),
                  hostMessage->messageSize);

  pw::Result<uint32_t> result = pw::rpc::ExtractChannelId(packet);
  if (!result.status().ok()) {
    LOGE("Unable to extract channel ID from packet: %" PRIu8,
         static_cast<uint8_t>(result.status().code()));
    return false;
  }

  if (!validateHostChannelId(hostMessage, result.value())) {
    return false;
  }

  if (!chreConfigureHostEndpointNotifications(hostMessage->hostEndpoint,
                                              true)) {
    LOGW("Fail to register for host client updates");
  }

  size_t hostIndex = mConnectedHosts.find(hostMessage->hostEndpoint);
  if (hostIndex == mConnectedHosts.size()) {
    mConnectedHosts.push_back(hostMessage->hostEndpoint);
  }

  mHostOutput.setHostEndpoint(hostMessage->hostEndpoint);
  pw::Status status = mServer.OpenChannel(result.value(), mHostOutput);
  if (status != pw::OkStatus() && status != pw::Status::AlreadyExists()) {
    LOGE("Failed to open channel: %" PRIu8, static_cast<uint8_t>(status.code()));
    return false;
  }

  status = mServer.ProcessPacket(packet);
  if (!status.ok()) {
    LOGE("Failed to process the packet: %" PRIu8, static_cast<uint8_t>(status.code()));
    return false;
  }

  return true;
}

// TODO(b/242301032): factor code with handleMessageFromHost
bool RpcServer::handleMessageFromNanoapp(uint32_t senderInstanceId,
                                         const void *eventData) {
  const auto data = static_cast<const ChrePigweedNanoappMessage *>(eventData);
  pw::span packet(reinterpret_cast<const std::byte *>(data->msg),
                  data->msgSize);

  pw::Result<uint32_t> result = pw::rpc::ExtractChannelId(packet);
  if (!result.status().ok()) {
    LOGE("Unable to extract channel ID from packet: %" PRIu8,
         static_cast<uint8_t>(result.status().code()));
    return false;
  }

  if (!validateNanoappChannelId(senderInstanceId, result.value())) {
    return false;
  }

  chreConfigureNanoappInfoEvents(true);

  mNanoappOutput.setClient(senderInstanceId);
  pw::Status status = mServer.OpenChannel(result.value(), mNanoappOutput);
  if (status != pw::OkStatus() && status != pw::Status::AlreadyExists()) {
    LOGE("Failed to open channel: %" PRIu8, static_cast<uint8_t>(status.code()));
    return false;
  }

  status = mServer.ProcessPacket(packet);
  if (!status.ok()) {
    LOGE("Failed to process the packet: %" PRIu8, static_cast<uint8_t>(status.code()));
    return false;
  }

  return true;
}

void RpcServer::handleHostClientNotification(const void *eventData) {
  if (mConnectedHosts.empty()) {
    return;
  }

  auto notif =
      static_cast<const struct chreHostEndpointNotification *>(eventData);

  if (notif->notificationType == HOST_ENDPOINT_NOTIFICATION_TYPE_DISCONNECT) {
    size_t hostIndex = mConnectedHosts.find(notif->hostEndpointId);
    if (hostIndex != mConnectedHosts.size()) {
      mServer
          .CloseChannel(kChannelIdHostClient |
                        static_cast<uint32_t>(notif->hostEndpointId))
          .IgnoreError();
      mConnectedHosts.erase(hostIndex);
    }
  }
}

void RpcServer::handleNanoappStopped(const void *eventData) {
  auto info = static_cast<const struct chreNanoappInfo *>(eventData);

  if (info->instanceId > kRpcNanoappMaxId) {
    LOGE("Invalid nanoapp instance ID %" PRIu32, info->instanceId);
  } else if (pw::Status status = mServer.CloseChannel(info->instanceId);
             !status.ok()) {
    LOGE("Failed to close channel for nanoapp with instance ID %"
         PRIu32 ": %" PRIu8, info->instanceId,
         static_cast<uint8_t>(status.code()));
  }
}

pw::Status RpcServer::closeChannel(uint32_t id) {
  return mServer.CloseChannel(id);
}

}  // namespace chre
