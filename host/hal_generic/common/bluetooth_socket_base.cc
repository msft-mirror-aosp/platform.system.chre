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

#include "bluetooth_socket_base.h"
#include <cstdint>

#include "chre/platform/shared/host_protocol_common.h"
#include "chre_connection.h"
#include "chre_host/generated/host_messages_generated.h"
#include "chre_host/host_protocol_host.h"
#include "chre_host/log.h"
#include "flatbuffers/flatbuffers.h"

namespace aidl::android::hardware::bluetooth::socket::impl {

using ::android::chre::getStringFromByteVector;
using ndk::ScopedAStatus;

ScopedAStatus BluetoothSocketBase::registerCallback(
    const std::shared_ptr<IBluetoothSocketCallback> &callback) {
  mCallback = callback;
  return ScopedAStatus::ok();
}

ScopedAStatus BluetoothSocketBase::getSocketCapabilities(
    SocketCapabilities *result) {
  // TODO(b/379135403): Query the low power processor for these values
  result->leCocCapabilities.numberOfSupportedSockets = 3;
  result->leCocCapabilities.mtu = 1000;
  result->rfcommCapabilities.numberOfSupportedSockets = 0;
  result->rfcommCapabilities.maxFrameSize = 23;
  return ScopedAStatus::ok();
}

ScopedAStatus BluetoothSocketBase::opened(const SocketContext &context) {
  LOGD("Host opened BT offload socket ID=%" PRIu64, context.socketId);
  flatbuffers::FlatBufferBuilder builder(1028);
  if (context.channelInfo.getTag() != ChannelInfo::Tag::leCocChannelInfo) {
    LOGE("Got open request for unsupported socket type %" PRId32,
         context.channelInfo.getTag());
    sendOpenedCompleteMessage(context.socketId, Status::FAILURE,
                              "Unsupported socket type");
    return ScopedAStatus::ok();
  }
  auto socketName = ::chre::HostProtocolCommon::addStringAsByteVector(
      builder, context.name.c_str());
  const auto &socketChannelInfo =
      context.channelInfo.get<ChannelInfo::Tag::leCocChannelInfo>();
  auto leCocChannelInfo = ::chre::fbs::CreateLeCocChannelInfo(
      builder, socketChannelInfo.localCid, socketChannelInfo.remoteCid,
      socketChannelInfo.psm, socketChannelInfo.localMtu,
      socketChannelInfo.remoteMtu, socketChannelInfo.localMps,
      socketChannelInfo.remoteMps, socketChannelInfo.initialRxCredits,
      socketChannelInfo.initialTxCredits);

  auto socketOpen = ::chre::fbs::CreateBtSocketOpen(
      builder, context.socketId, socketName, context.aclConnectionHandle,
      ::chre::fbs::ChannelInfo::LeCocChannelInfo, leCocChannelInfo.Union(),
      context.endpointId.hubId, context.endpointId.id);
  ::chre::HostProtocolCommon::finalize(
      builder, ::chre::fbs::ChreMessage::BtSocketOpen, socketOpen.Union());

  if (!mConnection->sendRawMessage(builder.GetBufferPointer(),
                                   builder.GetSize())) {
    LOGE("Failed to send BT socket opened message");
    sendOpenedCompleteMessage(context.socketId, Status::FAILURE,
                              "Failed to send BT socket opened message");
  }
  return ScopedAStatus::ok();
}

ScopedAStatus BluetoothSocketBase::closed(int64_t socketId) {
  LOGD("Host closed BT offload socket ID=%" PRIu64, socketId);
  flatbuffers::FlatBufferBuilder builder(64);
  auto socketCloseResponse =
      ::chre::fbs::CreateBtSocketCloseResponse(builder, socketId);
  ::chre::HostProtocolCommon::finalize(
      builder, ::chre::fbs::ChreMessage::BtSocketCloseResponse,
      socketCloseResponse.Union());

  if (!mConnection->sendRawMessage(builder.GetBufferPointer(),
                                   builder.GetSize())) {
    LOGE("Failed to send BT socket closed message");
  }
  return ScopedAStatus::ok();
}

void BluetoothSocketBase::handleBtSocketOpenResponse(
    const ::chre::fbs::BtSocketOpenResponseT &response) {
  std::string reason = std::string(getStringFromByteVector(response.reason));
  LOGD("Got BT Socket open response, socket ID=%" PRIu64
       ", status=%d, reason=%s",
       response.socketId, response.status, reason.c_str());
  sendOpenedCompleteMessage(
      response.socketId,
      response.status == ::chre::fbs::BtSocketOpenStatus::SUCCESS
          ? Status::SUCCESS
          : Status::FAILURE,
      reason);
}

void BluetoothSocketBase::handleBtSocketClose(
    const ::chre::fbs::BtSocketCloseT &message) {
  std::string reason = std::string(getStringFromByteVector(message.reason));
  LOGD("Got BT Socket close, socket ID=%" PRIu64 ", reason=%s",
       message.socketId, reason.c_str());
  if (mCallback == nullptr) {
    LOGE("Received socket close message with no registered callback");
    return;
  }
  mCallback->close(message.socketId, reason);
}

void BluetoothSocketBase::sendOpenedCompleteMessage(int64_t socketId,
                                                    Status status,
                                                    std::string reason) {
  if (mCallback == nullptr) {
    LOGE("Sending socket opened complete with no registered callback");
    return;
  }
  mCallback->openedComplete(socketId, status, reason);
}

}  // namespace aidl::android::hardware::bluetooth::socket::impl
