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

#ifndef CHRE_PLATFORM_SHARED_HOST_PROTOCOL_CHRE_H_
#define CHRE_PLATFORM_SHARED_HOST_PROTOCOL_CHRE_H_

#include <stdint.h>
#include <cstdint>

#include "chre/core/ble_l2cap_coc_socket_data.h"
#include "chre/core/event_loop_common.h"
#include "chre/core/nanoapp.h"
#include "chre/core/settings.h"
#include "chre/platform/shared/generated/host_messages_generated.h"
#include "chre/platform/shared/host_protocol_common.h"
#include "chre/util/dynamic_vector.h"
#include "chre/util/flatbuffers/helpers.h"
#include "chre/util/system/message_common.h"
#include "chre_api/chre/event.h"
#include "flatbuffers/flatbuffers.h"

#include "pw_allocator/unique_ptr.h"

namespace chre {

typedef flatbuffers::Offset<fbs::NanoappListEntry> NanoappListEntryOffset;

/**
 * Checks that a string encapsulated as a byte vector is null-terminated, and
 * if it is, returns a pointer to the vector's data. Otherwise returns null.
 *
 * This is similar to getStringFromByteVector in host_protocol_host.h. Ensure
 * that method's implementation is kept in sync with this.
 *
 * @param vec Target vector, can be null
 *
 * @return Pointer to the vector's data, or null
 */
const char *getStringFromByteVector(const flatbuffers::Vector<int8_t> *vec);

/**
 * These methods are called from decodeMessageFromHost() and must be implemented
 * by the code that calls it to handle parsed messages.
 */
class HostMessageHandlers {
 public:
  struct LoadNanoappCallbackData {
    uint64_t appId;
    uint32_t transactionId;
    uint16_t hostClientId;
    UniquePtr<Nanoapp> nanoapp;
    uint32_t fragmentId;
    bool sendFragmentResponse;
  };

  static void handleNanoappMessage(uint64_t appId, uint32_t messageType,
                                   uint16_t hostEndpoint,
                                   const void *messageData,
                                   size_t messageDataLen, bool isReliable,
                                   uint32_t messageSequenceNumber);

  static void handleMessageDeliveryStatus(uint32_t messageSequenceNumber,
                                          uint8_t errorCode);

  static void handleHubInfoRequest(uint16_t hostClientId);

  static void handleNanoappListRequest(uint16_t hostClientId);

  static void handlePulseRequest();

  static void handleDebugConfiguration(
      const fbs::DebugConfiguration *debugConfiguration);

  static void handleLoadNanoappRequest(
      uint16_t hostClientId, uint32_t transactionId, uint64_t appId,
      uint32_t appVersion, uint32_t appFlags, uint32_t targetApiVersion,
      const void *buffer, size_t bufferLen, const char *appFileName,
      uint32_t fragmentId, size_t appBinaryLen, bool respondBeforeStart);

  static void handleUnloadNanoappRequest(uint16_t hostClientId,
                                         uint32_t transactionId, uint64_t appId,
                                         bool allowSystemNanoappUnload);

  static void handleTimeSyncMessage(int64_t offset);

  static void handleDebugDumpRequest(uint16_t hostClientId);

  static void handleSettingChangeMessage(fbs::Setting setting,
                                         fbs::SettingState state);

  static void handleSelfTestRequest(uint16_t hostClientId);

  static void handleNanConfigurationUpdate(bool enabled);

  static void handleBtSocketOpen(uint64_t hubId,
                                 const BleL2capCocSocketData &socketData,
                                 const char *name, uint32_t psm);

 private:
  static void sendFragmentResponse(uint16_t hostClientId,
                                   uint32_t transactionId, uint32_t fragmentId,
                                   bool success);

  static void finishLoadingNanoappCallback(
      SystemCallbackType type, UniquePtr<LoadNanoappCallbackData> &&cbData);

  /**
   * Helper function that loads a nanoapp into the system
   * from a buffer sent over in 1 or more fragments.
   *
   * @param hostClientId the ID of client that originated this transaction
   * @param transactionId the ID of the transaction
   * @param appId the ID of the app to load
   * @param appVersion the version of the app to load
   * @param appFlags The flags provided by the app being loaded
   * @param targetApiVersion the API version this nanoapp is targeted for
   * @param buffer the nanoapp binary data. May be only part of the nanoapp's
   *     binary if it's being sent over multiple fragments
   * @param bufferLen the size of buffer in bytes
   * @param fragmentId the identifier indicating which fragment is being loaded
   * @param appBinaryLen the full size of the nanoapp binary to be loaded
   *
   * @return void
   */
  static void loadNanoappData(uint16_t hostClientId, uint32_t transactionId,
                              uint64_t appId, uint32_t appVersion,
                              uint32_t appFlags, uint32_t targetApiVersion,
                              const void *buffer, size_t bufferLen,
                              uint32_t fragmentId, size_t appBinaryLen,
                              bool respondBeforeStart);
};

/**
 * A set of helper methods that simplify the encode/decode of FlatBuffers
 * messages used in communications with the host from CHRE.
 */
class HostProtocolChre : public HostProtocolCommon {
 public:
  /**
   * Verifies and decodes a FlatBuffers-encoded CHRE message.
   *
   * @param message Buffer containing message
   * @param messageLen Size of the message, in bytes
   * @param handlers Contains callbacks to process a decoded message
   *
   * @return bool true if the message was successfully decoded, false if it was
   *         corrupted/invalid/unrecognized
   */
  static bool decodeMessageFromHost(const void *message, size_t messageLen);

  /**
   * Refer to the context hub HAL definition for a details of these
   * parameters.
   *
   * @param builder A newly constructed ChreFlatBufferBuilder that will be
   * used to encode the message
   */
  static void encodeHubInfoResponse(
      ChreFlatBufferBuilder &builder, const char *name, const char *vendor,
      const char *toolchain, uint32_t legacyPlatformVersion,
      uint32_t legacyToolchainVersion, float peakMips, float stoppedPower,
      float sleepPower, float peakPower, uint32_t maxMessageLen,
      uint64_t platformId, uint32_t version, uint16_t hostClientId,
      bool supportsReliableMessages);

  /**
   * Supports construction of a NanoappListResponse by adding a single
   * NanoappListEntry to the response. The offset for the newly added entry is
   * maintained in the given vector until finishNanoappListResponse() is called.
   * Example usage:
   *
   *   ChreFlatBufferBuilder builder;
   *   DynamicVector<NanoappListEntryOffset> vector;
   *   for (auto app : appList) {
   *     HostProtocolChre::addNanoppListEntry(builder, vector, ...);
   *   }
   *   HostProtocolChre::finishNanoappListResponse(builder, vector);
   *
   * @param builder A ChreFlatBufferBuilder to use for encoding the message
   * @param offsetVector A vector to track the offset to the newly added
   *        NanoappListEntry, which be passed to finishNanoappListResponse()
   *        once all entries are added
   */
  static void addNanoappListEntry(
      ChreFlatBufferBuilder &builder,
      DynamicVector<NanoappListEntryOffset> &offsetVector, uint64_t appId,
      uint32_t appVersion, bool enabled, bool isSystemNanoapp,
      uint32_t appPermissions,
      const DynamicVector<struct chreNanoappRpcService> &rpcServices);

  /**
   * Finishes encoding a NanoappListResponse message after all NanoappListEntry
   * elements have already been added to the builder.
   *
   * @param builder The ChreFlatBufferBuilder used with addNanoappListEntry()
   * @param offsetVector The vector used with addNanoappListEntry()
   * @param hostClientId
   *
   * @see addNanoappListEntry()
   */
  static void finishNanoappListResponse(
      ChreFlatBufferBuilder &builder,
      DynamicVector<NanoappListEntryOffset> &offsetVector,
      uint16_t hostClientId);

  /**
   * Encodes a response to the host indicating CHRE is up running.
   */
  static void encodePulseResponse(ChreFlatBufferBuilder &builder);

  /**
   * Encodes a response to the host communicating the result of dynamically
   * loading a nanoapp.
   */
  static void encodeLoadNanoappResponse(ChreFlatBufferBuilder &builder,
                                        uint16_t hostClientId,
                                        uint32_t transactionId, bool success,
                                        uint32_t fragmentId);

  /**
   * Encodes a response to the host communicating the result of dynamically
   * unloading a nanoapp.
   */
  static void encodeUnloadNanoappResponse(ChreFlatBufferBuilder &builder,
                                          uint16_t hostClientId,
                                          uint32_t transactionId, bool success);

  /**
   * Encodes a nanoapp's instance ID and app ID to the host.
   */
  static void encodeNanoappTokenDatabaseInfo(ChreFlatBufferBuilder &builder,
                                             uint16_t instanceId,
                                             uint64_t appId,
                                             uint32_t tokenDatabaseOffset,
                                             size_t tokenDatabaseSize);

  /**
   * Encodes a buffer of log messages to the host.
   */
  static void encodeLogMessages(ChreFlatBufferBuilder &builder,
                                const uint8_t *logBuffer, size_t bufferSize);

  /**
   * Encodes a buffer of V2 log messages to the host.
   */
  static void encodeLogMessagesV2(ChreFlatBufferBuilder &builder,
                                  const uint8_t *logBuffer, size_t bufferSize,
                                  uint32_t numLogsDropped);

  /**
   * Encodes a string into a DebugDumpData message.
   *
   * @param debugStr Null-terminated ASCII string containing debug information
   * @param debugStrSize Size of the debugStr buffer, including null termination
   */
  static void encodeDebugDumpData(ChreFlatBufferBuilder &builder,
                                  uint16_t hostClientId, const char *debugStr,
                                  size_t debugStrSize);

  /**
   * Encodes the final response to a debug dump request.
   */
  static void encodeDebugDumpResponse(ChreFlatBufferBuilder &builder,
                                      uint16_t hostClientId, bool success,
                                      uint32_t dataCount);

  /**
   * Encodes a message requesting time sync from host.
   */
  static void encodeTimeSyncRequest(ChreFlatBufferBuilder &builder);

  /**
   * Encodes a message notifying the host that audio has been requested by a
   * nanoapp, so the low-power microphone needs to be powered on.
   */
  static void encodeLowPowerMicAccessRequest(ChreFlatBufferBuilder &builder);

  /**
   * Encodes a message notifying the host that no nanoapps are requesting audio
   * anymore, so the low-power microphone may be powered off.
   */
  static void encodeLowPowerMicAccessRelease(ChreFlatBufferBuilder &builder);

  /**
   * @param state The fbs::Setting value.
   * @param chreSetting If success, stores the corresponding
   * chre::Setting value.
   *
   * @return true if state was a valid fbs::Setting value.
   */
  static bool getSettingFromFbs(fbs::Setting setting, Setting *chreSetting);

  /**
   * @param state The fbs::SettingState value.
   * @param chreSettingEnabled If success, stores the value indicating whether
   *     the setting is enabled or not.
   *
   * @return true if state was a valid fbs::SettingState value.
   */
  static bool getSettingEnabledFromFbs(fbs::SettingState state,
                                       bool *chreSettingEnabled);

  /**
   * Encodes a message notifying the result of a self test.
   */
  static void encodeSelfTestResponse(ChreFlatBufferBuilder &builder,
                                     uint16_t hostClientId, bool success);

  /**
   * Encodes a metric message using custon-defined protocol
   */
  static void encodeMetricLog(ChreFlatBufferBuilder &builder, uint32_t metricId,
                              const uint8_t *encodedMsg, size_t metricSize);

  /**
   * Encodes a NAN configuration request.
   *
   * @param builder An instance of the CHRE Flatbuffer builder.
   * @param enable Boolean to indicate the enable/disable operation being
   *        requested.
   */
  static void encodeNanConfigurationRequest(ChreFlatBufferBuilder &builder,
                                            bool enable);

  /**
   * Encodes a BT socket open response.
   *
   * @param builder An instance of the CHRE Flatbuffer builder.
   * @param hostClientId Host client identifier.
   * @param success Whether the socket open request was successful.
   * @param reason Failure reason if success is false.
   * @param socketId BT socket identifier.
   */
  static void encodeBtSocketOpenResponse(ChreFlatBufferBuilder &builder,
                                         uint16_t hostClientId,
                                         uint64_t socketId, bool success,
                                         const char *reason);

  /**
   * Encodes a BT socket close request.
   *
   * @param builder An instance of the CHRE Flatbuffer builder.
   * @param hostClientId Host client identifier.
   * @param reason Reason socket is being closed.
   * @param socketId BT socket identifier.
   */
  static void encodeBtSocketClose(ChreFlatBufferBuilder &builder,
                                  uint16_t hostClientId, uint64_t socketId,
                                  const char *reason);

  /**
   * Encodes the response acking a GetMessageHubsAndEndpointsRequest.
   *
   * @param builder Builder which assembles and stores the message.
   */
  static void encodeGetMessageHubsAndEndpointsResponse(
      ChreFlatBufferBuilder &builder);

  /**
   * Encodes a new embedded hub notification.
   *
   * @param builder Builder which assembles and stores the message.
   * @param hub Details of the new hub.
   */
  static void encodeRegisterMessageHub(ChreFlatBufferBuilder &builder,
                                       const message::MessageHubInfo &hub);

  /**
   * Encodes a new embedded endpoint notification.
   *
   * @param builder Builder which assembles and stores the message.
   * @param hub Id of the hub hosting the new endpoint.
   * @param endpoint Details of the new endpoint.
   */
  static void encodeRegisterEndpoint(ChreFlatBufferBuilder &builder,
                                     message::MessageHubId hub,
                                     const message::EndpointInfo &endpoint);

  /**
   * Encodes a request to open a new session with host endpoint.
   *
   * @param builder Builder which assembles and stores the message.
   * @param session Details of the new session.
   */
  static void encodeOpenEndpointSessionRequest(ChreFlatBufferBuilder &builder,
                                               const message::Session &session);

  /**
   * Encodes a notification that a session has been opened.
   *
   * @param builder Builder which assembles and stores the message.
   * @param hub Id of the destination host hub.
   * @param session Id of the session that was opened.
   */
  static void encodeEndpointSessionOpened(ChreFlatBufferBuilder &builder,
                                          message::MessageHubId hub,
                                          message::SessionId session);

  /**
   * Encodes a notification that a session has been closed.
   *
   * @param builder Builder which assembles and stores the message.
   * @param hub Id of the destination host hub.
   * @param session Id of the session that was closed.
   * @param reason Reason the session was closed (or rejected).
   */
  static void encodeEndpointSessionClosed(ChreFlatBufferBuilder &builder,
                                          message::MessageHubId hub,
                                          message::SessionId session,
                                          message::Reason reason);

  /**
   * Encodes a message sent within an endpoint session.
   *
   * @param builder Builder which assembles and stores the message.
   * @param hub Id of the destination host hub.
   * @param session Id of the session.
   * @param data The message data.
   * @param type The type of the message.
   * @param permissions The permissions required to receive the message.
   */
  static void encodeEndpointSessionMessage(ChreFlatBufferBuilder &builder,
                                           message::MessageHubId hub,
                                           message::SessionId session,
                                           pw::UniquePtr<std::byte[]> &&data,
                                           uint32_t type, uint32_t permissions);
};

}  // namespace chre

#endif  // CHRE_PLATFORM_SHARED_HOST_PROTOCOL_CHRE_H_
