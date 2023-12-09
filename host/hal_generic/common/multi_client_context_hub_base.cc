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

#include "multi_client_context_hub_base.h"

#include <chre/platform/shared/host_protocol_common.h>
#include <chre_host/generated/host_messages_generated.h>
#include <chre_host/log.h>
#include "chre/event.h"
#include "chre_host/config_util.h"
#include "chre_host/fragmented_load_transaction.h"
#include "chre_host/host_protocol_host.h"
#include "hal_error.h"
#include "permissions_util.h"

namespace android::hardware::contexthub::common::implementation {

using ::android::chre::FragmentedLoadTransaction;
using ::android::chre::getStringFromByteVector;
using ::ndk::ScopedAStatus;
namespace fbs = ::chre::fbs;

namespace {
constexpr uint32_t kDefaultHubId = 0;

// timeout for calling getContextHubs(), which is synchronous
constexpr auto kHubInfoQueryTimeout = std::chrono::seconds(5);
// timeout for enable/disable test mode, which is synchronous
constexpr std::chrono::duration ktestModeTimeOut = std::chrono::seconds(5);

bool isValidContextHubId(uint32_t hubId) {
  if (hubId != kDefaultHubId) {
    LOGE("Invalid context hub ID %" PRId32, hubId);
    return false;
  }
  return true;
}

bool getFbsSetting(const Setting &setting, fbs::Setting *fbsSetting) {
  bool foundSetting = true;
  switch (setting) {
    case Setting::LOCATION:
      *fbsSetting = fbs::Setting::LOCATION;
      break;
    case Setting::AIRPLANE_MODE:
      *fbsSetting = fbs::Setting::AIRPLANE_MODE;
      break;
    case Setting::MICROPHONE:
      *fbsSetting = fbs::Setting::MICROPHONE;
      break;
    default:
      foundSetting = false;
      LOGE("Setting update with invalid enum value %hhu", setting);
      break;
  }
  return foundSetting;
}

chre::fbs::SettingState toFbsSettingState(bool enabled) {
  return enabled ? chre::fbs::SettingState::ENABLED
                 : chre::fbs::SettingState::DISABLED;
}

// functions that extract different version numbers
inline constexpr int8_t extractChreApiMajorVersion(uint32_t chreVersion) {
  return static_cast<int8_t>(chreVersion >> 24);
}
inline constexpr int8_t extractChreApiMinorVersion(uint32_t chreVersion) {
  return static_cast<int8_t>(chreVersion >> 16);
}
inline constexpr uint16_t extractChrePatchVersion(uint32_t chreVersion) {
  return static_cast<uint16_t>(chreVersion);
}

// functions that help to generate ScopedAStatus from different values.
inline ScopedAStatus fromServiceError(HalError errorCode) {
  return ScopedAStatus::fromServiceSpecificError(
      static_cast<int32_t>(errorCode));
}
inline ScopedAStatus fromResult(bool result) {
  return result ? ScopedAStatus::ok()
                : fromServiceError(HalError::OPERATION_FAILED);
}
}  // anonymous namespace

MultiClientContextHubBase::MultiClientContextHubBase() {
  mDeathRecipient = ndk::ScopedAIBinder_DeathRecipient(
      AIBinder_DeathRecipient_new(onClientDied));
  AIBinder_DeathRecipient_setOnUnlinked(
      mDeathRecipient.get(), /*onUnlinked= */ [](void *cookie) {
        LOGI("Callback is unlinked. Releasing the death recipient cookie.");
        delete static_cast<HalDeathRecipientCookie *>(cookie);
      });
  mDeadClientUnlinker =
      [&deathRecipient = mDeathRecipient](
          const std::shared_ptr<IContextHubCallback> &callback,
          void *deathRecipientCookie) {
        return AIBinder_unlinkToDeath(callback->asBinder().get(),
                                      deathRecipient.get(),
                                      deathRecipientCookie) == STATUS_OK;
      };
}

ScopedAStatus MultiClientContextHubBase::getContextHubs(
    std::vector<ContextHubInfo> *contextHubInfos) {
  std::unique_lock<std::mutex> lock(mHubInfoMutex);
  if (mContextHubInfo == nullptr) {
    fbs::HubInfoResponseT response;
    flatbuffers::FlatBufferBuilder builder;
    HostProtocolHost::encodeHubInfoRequest(builder);
    if (!mConnection->sendMessage(builder)) {
      LOGE("Failed to send a message to CHRE to get context hub info.");
      return fromServiceError(HalError::OPERATION_FAILED);
    }
    mHubInfoCondition.wait_for(lock, kHubInfoQueryTimeout,
                               [this]() { return mContextHubInfo != nullptr; });
  }
  if (mContextHubInfo != nullptr) {
    contextHubInfos->push_back(*mContextHubInfo);
    return ScopedAStatus::ok();
  }
  LOGE("Unable to get a valid context hub info for PID %d",
       AIBinder_getCallingPid());
  return fromServiceError(HalError::INVALID_RESULT);
}

ScopedAStatus MultiClientContextHubBase::loadNanoapp(
    int32_t contextHubId, const NanoappBinary &appBinary,
    int32_t transactionId) {
  if (!isValidContextHubId(contextHubId)) {
    return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
  }
  LOGI("Loading nanoapp 0x%" PRIx64, appBinary.nanoappId);
  uint32_t targetApiVersion = (appBinary.targetChreApiMajorVersion << 24) |
                              (appBinary.targetChreApiMinorVersion << 16);
  auto transaction = std::make_unique<FragmentedLoadTransaction>(
      transactionId, appBinary.nanoappId, appBinary.nanoappVersion,
      appBinary.flags, targetApiVersion, appBinary.customBinary,
      mConnection->getLoadFragmentSizeBytes());
  pid_t pid = AIBinder_getCallingPid();
  if (!mHalClientManager->registerPendingLoadTransaction(
          pid, std::move(transaction))) {
    return fromResult(false);
  }
  auto clientId = mHalClientManager->getClientId(pid);
  auto request = mHalClientManager->getNextFragmentedLoadRequest();

  if (request.has_value() &&
      sendFragmentedLoadRequest(clientId, request.value())) {
    mEventLogger.logNanoappLoad(appBinary, /* success= */ true);
    return ScopedAStatus::ok();
  }
  LOGE("Failed to send the first load request for nanoapp 0x%" PRIx64,
       appBinary.nanoappId);
  mHalClientManager->resetPendingLoadTransaction();
  // TODO(b/284481035): The result should be logged after the async response is
  //  received.
  mEventLogger.logNanoappLoad(appBinary, /* success= */ false);
  return fromResult(false);
}

bool MultiClientContextHubBase::sendFragmentedLoadRequest(
    HalClientId clientId, FragmentedLoadRequest &request) {
  flatbuffers::FlatBufferBuilder builder(128 + request.binary.size());
  HostProtocolHost::encodeFragmentedLoadNanoappRequest(
      builder, request, /* respondBeforeStart= */ false);
  HostProtocolHost::mutateHostClientId(builder.GetBufferPointer(),
                                       builder.GetSize(), clientId);
  return mConnection->sendMessage(builder);
}

ScopedAStatus MultiClientContextHubBase::unloadNanoapp(int32_t contextHubId,
                                                       int64_t appId,
                                                       int32_t transactionId) {
  if (!isValidContextHubId(contextHubId)) {
    return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
  }
  pid_t pid = AIBinder_getCallingPid();
  if (!mHalClientManager->registerPendingUnloadTransaction(pid,
                                                           transactionId)) {
    return fromResult(false);
  }
  HalClientId clientId = mHalClientManager->getClientId(pid);
  flatbuffers::FlatBufferBuilder builder(64);
  HostProtocolHost::encodeUnloadNanoappRequest(
      builder, transactionId, appId, /* allowSystemNanoappUnload= */ false);
  HostProtocolHost::mutateHostClientId(builder.GetBufferPointer(),
                                       builder.GetSize(), clientId);

  bool result = mConnection->sendMessage(builder);
  if (!result) {
    mHalClientManager->resetPendingUnloadTransaction(clientId, transactionId);
  }
  // TODO(b/284481035): The result should be logged after the async response is
  //  received.
  mEventLogger.logNanoappUnload(appId, result);
  return fromResult(result);
}

ScopedAStatus MultiClientContextHubBase::disableNanoapp(
    int32_t /* contextHubId */, int64_t appId, int32_t /* transactionId */) {
  LOGW("Attempted to disable app ID 0x%016" PRIx64 ", but not supported",
       appId);
  return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus MultiClientContextHubBase::enableNanoapp(
    int32_t /* contextHubId */, int64_t appId, int32_t /* transactionId */) {
  LOGW("Attempted to enable app ID 0x%016" PRIx64 ", but not supported", appId);
  return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus MultiClientContextHubBase::onSettingChanged(Setting setting,
                                                          bool enabled) {
  mSettingEnabled[setting] = enabled;
  fbs::Setting fbsSetting;
  bool isWifiOrBtSetting =
      (setting == Setting::WIFI_MAIN || setting == Setting::WIFI_SCANNING ||
       setting == Setting::BT_MAIN || setting == Setting::BT_SCANNING);
  if (!isWifiOrBtSetting && getFbsSetting(setting, &fbsSetting)) {
    flatbuffers::FlatBufferBuilder builder(64);
    HostProtocolHost::encodeSettingChangeNotification(
        builder, fbsSetting, toFbsSettingState(enabled));
    mConnection->sendMessage(builder);
  }

  bool isWifiMainEnabled = isSettingEnabled(Setting::WIFI_MAIN);
  bool isWifiScanEnabled = isSettingEnabled(Setting::WIFI_SCANNING);
  bool isAirplaneModeEnabled = isSettingEnabled(Setting::AIRPLANE_MODE);

  // Because the airplane mode impact on WiFi is not standardized in Android,
  // we write a specific handling in the Context Hub HAL to inform CHRE.
  // The following definition is a default one, and can be adjusted
  // appropriately if necessary.
  bool isWifiAvailable = isAirplaneModeEnabled
                             ? (isWifiMainEnabled)
                             : (isWifiMainEnabled || isWifiScanEnabled);
  if (!mIsWifiAvailable.has_value() || (isWifiAvailable != mIsWifiAvailable)) {
    flatbuffers::FlatBufferBuilder builder(64);
    HostProtocolHost::encodeSettingChangeNotification(
        builder, fbs::Setting::WIFI_AVAILABLE,
        toFbsSettingState(isWifiAvailable));
    mConnection->sendMessage(builder);
    mIsWifiAvailable = isWifiAvailable;
  }

  // The BT switches determine whether we can BLE scan which is why things are
  // mapped like this into CHRE.
  bool isBtMainEnabled = isSettingEnabled(Setting::BT_MAIN);
  bool isBtScanEnabled = isSettingEnabled(Setting::BT_SCANNING);
  bool isBleAvailable = isBtMainEnabled || isBtScanEnabled;
  if (!mIsBleAvailable.has_value() || (isBleAvailable != mIsBleAvailable)) {
    flatbuffers::FlatBufferBuilder builder(64);
    HostProtocolHost::encodeSettingChangeNotification(
        builder, fbs::Setting::BLE_AVAILABLE,
        toFbsSettingState(isBleAvailable));
    mConnection->sendMessage(builder);
    mIsBleAvailable = isBleAvailable;
  }

  return ScopedAStatus::ok();
}

ScopedAStatus MultiClientContextHubBase::queryNanoapps(int32_t contextHubId) {
  if (!isValidContextHubId(contextHubId)) {
    return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
  }
  flatbuffers::FlatBufferBuilder builder(64);
  HostProtocolHost::encodeNanoappListRequest(builder);
  HostProtocolHost::mutateHostClientId(
      builder.GetBufferPointer(), builder.GetSize(),
      mHalClientManager->getClientId(AIBinder_getCallingPid()));
  return fromResult(mConnection->sendMessage(builder));
}

ScopedAStatus MultiClientContextHubBase::getPreloadedNanoappIds(
    int32_t contextHubId, std::vector<int64_t> *out_preloadedNanoappIds) {
  if (contextHubId != kDefaultHubId) {
    LOGE("Invalid ID %" PRId32, contextHubId);
    return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
  }
  if (out_preloadedNanoappIds == nullptr) {
    return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
  }
  std::unique_lock<std::mutex> lock(mPreloadedNanoappIdsMutex);
  if (!mPreloadedNanoappIds.has_value()) {
    mPreloadedNanoappIds = std::vector<uint64_t>{};
    mPreloadedNanoappLoader->getPreloadedNanoappIds(*mPreloadedNanoappIds);
  }
  for (const auto &nanoappId : mPreloadedNanoappIds.value()) {
    out_preloadedNanoappIds->emplace_back(static_cast<uint64_t>(nanoappId));
  }
  return ScopedAStatus::ok();
}

ScopedAStatus MultiClientContextHubBase::registerCallback(
    int32_t contextHubId,
    const std::shared_ptr<IContextHubCallback> &callback) {
  if (!isValidContextHubId(contextHubId)) {
    return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
  }
  if (callback == nullptr) {
    LOGE("Callback of context hub HAL must not be null");
    return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
  }
  pid_t pid = AIBinder_getCallingPid();
  auto *cookie = new HalDeathRecipientCookie(this, pid);
  if (AIBinder_linkToDeath(callback->asBinder().get(), mDeathRecipient.get(),
                           cookie) != STATUS_OK) {
    LOGE("Failed to link a client binder (pid=%d) to the death recipient", pid);
    delete cookie;
    return fromResult(false);
  }
  // If AIBinder_linkToDeath is successful the cookie will be released by the
  // callback of binder unlinking (callback overridden).
  if (!mHalClientManager->registerCallback(pid, callback, cookie)) {
    LOGE("Unable to register a client (pid=%d) callback", pid);
    return fromResult(false);
  }
  return ScopedAStatus::ok();
}

ScopedAStatus MultiClientContextHubBase::sendMessageToHub(
    int32_t contextHubId, const ContextHubMessage &message) {
  if (!isValidContextHubId(contextHubId)) {
    return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
  }
  HostEndpointId hostEndpointId = message.hostEndPoint;
  if (!mHalClientManager->mutateEndpointIdFromHostIfNeeded(
          AIBinder_getCallingPid(), hostEndpointId)) {
    return fromResult(false);
  }
  flatbuffers::FlatBufferBuilder builder(1024);
  HostProtocolHost::encodeNanoappMessage(
      builder, message.nanoappId, message.messageType, hostEndpointId,
      message.messageBody.data(), message.messageBody.size());
  bool success = mConnection->sendMessage(builder);
  mEventLogger.logMessageToNanoapp(message, success);
  return fromResult(success);
}

ScopedAStatus MultiClientContextHubBase::onHostEndpointConnected(
    const HostEndpointInfo &info) {
  uint8_t type;
  switch (info.type) {
    case HostEndpointInfo::Type::APP:
      type = CHRE_HOST_ENDPOINT_TYPE_APP;
      break;
    case HostEndpointInfo::Type::NATIVE:
      type = CHRE_HOST_ENDPOINT_TYPE_NATIVE;
      break;
    case HostEndpointInfo::Type::FRAMEWORK:
      type = CHRE_HOST_ENDPOINT_TYPE_FRAMEWORK;
      break;
    default:
      LOGE("Unsupported host endpoint type %" PRIu32, info.type);
      return fromServiceError(HalError::INVALID_ARGUMENT);
  }

  uint16_t endpointId = info.hostEndpointId;
  pid_t pid = AIBinder_getCallingPid();
  if (!mHalClientManager->registerEndpointId(pid, info.hostEndpointId) ||
      !mHalClientManager->mutateEndpointIdFromHostIfNeeded(pid, endpointId)) {
    return fromServiceError(HalError::INVALID_ARGUMENT);
  }
  flatbuffers::FlatBufferBuilder builder(64);
  HostProtocolHost::encodeHostEndpointConnected(
      builder, endpointId, type, info.packageName.value_or(std::string()),
      info.attributionTag.value_or(std::string()));
  return fromResult(mConnection->sendMessage(builder));
}

ScopedAStatus MultiClientContextHubBase::onHostEndpointDisconnected(
    char16_t in_hostEndpointId) {
  HostEndpointId hostEndpointId = in_hostEndpointId;
  pid_t pid = AIBinder_getCallingPid();
  bool isSuccessful = false;
  if (mHalClientManager->removeEndpointId(pid, hostEndpointId) &&
      mHalClientManager->mutateEndpointIdFromHostIfNeeded(pid,
                                                          hostEndpointId)) {
    flatbuffers::FlatBufferBuilder builder(64);
    HostProtocolHost::encodeHostEndpointDisconnected(builder, hostEndpointId);
    isSuccessful = mConnection->sendMessage(builder);
  }
  if (!isSuccessful) {
    LOGW("Unable to remove host endpoint id %" PRIu16, in_hostEndpointId);
  }
  return ScopedAStatus::ok();
}

ScopedAStatus MultiClientContextHubBase::onNanSessionStateChanged(
    const NanSessionStateUpdate & /*in_update*/) {
  // TODO(271471342): Add support for NAN session management.
  return ndk::ScopedAStatus::ok();
}

ScopedAStatus MultiClientContextHubBase::setTestMode(bool enable) {
  return fromResult(enable ? enableTestMode() : disableTestMode());
}

bool MultiClientContextHubBase::enableTestMode() {
  std::unique_lock<std::mutex> lock(mTestModeMutex);
  if (mIsTestModeEnabled) {
    return true;
  }
  mTestModeNanoapps.reset();
  if (!queryNanoapps(kDefaultHubId).isOk()) {
    LOGE("Failed to get a list of loaded nanoapps.");
    mTestModeNanoapps.emplace();
    return false;
  }
  mEnableTestModeCv.wait_for(lock, ktestModeTimeOut,
                             [&]() { return mTestModeNanoapps.has_value(); });
  for (const auto &appId : *mTestModeNanoapps) {
    if (!unloadNanoapp(kDefaultHubId, appId, mTestModeTransactionId).isOk()) {
      LOGE("Failed to unload nanoapp 0x%" PRIx64 " to enable the test mode.",
           appId);
      return false;
    }
    mTestModeSyncUnloadResult.reset();
    mEnableTestModeCv.wait_for(lock, ktestModeTimeOut, [&]() {
      return mTestModeSyncUnloadResult.has_value();
    });
    if (!*mTestModeSyncUnloadResult) {
      LOGE("Failed to unload nanoapp 0x%" PRIx64 " to enable the test mode.",
           appId);
      return false;
    }
  }
  mIsTestModeEnabled = true;
  return true;
}

bool MultiClientContextHubBase::disableTestMode() {
  std::unique_lock<std::mutex> lock(mTestModeMutex);
  if (!mIsTestModeEnabled) {
    return true;
  }
  if (mTestModeNanoapps.has_value() && !mTestModeNanoapps->empty()) {
    if (!mPreloadedNanoappLoader->loadPreloadedNanoapps(*mTestModeNanoapps)) {
      LOGE("Failed to reload the nanoapps to disable the test mode.");
      return false;
    }
  }
  mTestModeNanoapps.emplace();
  mTestModeTransactionId = static_cast<int32_t>(kDefaultTestModeTransactionId);
  mIsTestModeEnabled = false;
  return true;
}

void MultiClientContextHubBase::handleMessageFromChre(
    const unsigned char *messageBuffer, size_t messageLen) {
  if (!::chre::HostProtocolCommon::verifyMessage(messageBuffer, messageLen)) {
    LOGE("Invalid message received from CHRE.");
    return;
  }
  std::unique_ptr<fbs::MessageContainerT> container =
      fbs::UnPackMessageContainer(messageBuffer);
  fbs::ChreMessageUnion &message = container->message;
  HalClientId clientId = container->host_addr->client_id();

  switch (container->message.type) {
    case fbs::ChreMessage::HubInfoResponse: {
      handleHubInfoResponse(*message.AsHubInfoResponse());
      break;
    }
    case fbs::ChreMessage::NanoappListResponse: {
      onNanoappListResponse(*message.AsNanoappListResponse(), clientId);
      break;
    }
    case fbs::ChreMessage::LoadNanoappResponse: {
      onNanoappLoadResponse(*message.AsLoadNanoappResponse(), clientId);
      break;
    }
    case fbs::ChreMessage::TimeSyncRequest: {
      if (mConnection->isTimeSyncNeeded()) {
        TimeSyncer::sendTimeSync(mConnection.get());
      } else {
        LOGW("Received an unexpected time sync request from CHRE.");
      }
      break;
    }
    case fbs::ChreMessage::UnloadNanoappResponse: {
      onNanoappUnloadResponse(*message.AsUnloadNanoappResponse(), clientId);
      break;
    }
    case fbs::ChreMessage::NanoappMessage: {
      onNanoappMessage(*message.AsNanoappMessage());
      break;
    }
    case fbs::ChreMessage::DebugDumpData: {
      onDebugDumpData(*message.AsDebugDumpData());
      break;
    }
    case fbs::ChreMessage::DebugDumpResponse: {
      onDebugDumpComplete(*message.AsDebugDumpResponse());
      break;
    }
    default:
      LOGW("Got unexpected message type %" PRIu8,
           static_cast<uint8_t>(message.type));
  }
}

void MultiClientContextHubBase::handleHubInfoResponse(
    const fbs::HubInfoResponseT &response) {
  std::unique_lock<std::mutex> lock(mHubInfoMutex);
  mContextHubInfo = std::make_unique<ContextHubInfo>();
  mContextHubInfo->name = getStringFromByteVector(response.name);
  mContextHubInfo->vendor = getStringFromByteVector(response.vendor);
  mContextHubInfo->toolchain = getStringFromByteVector(response.toolchain);
  mContextHubInfo->id = kDefaultHubId;
  mContextHubInfo->peakMips = response.peak_mips;
  mContextHubInfo->maxSupportedMessageLengthBytes = response.max_msg_len;
  mContextHubInfo->chrePlatformId = response.platform_id;
  uint32_t version = response.chre_platform_version;
  mContextHubInfo->chreApiMajorVersion = extractChreApiMajorVersion(version);
  mContextHubInfo->chreApiMinorVersion = extractChreApiMinorVersion(version);
  mContextHubInfo->chrePatchVersion = extractChrePatchVersion(version);
  mContextHubInfo->supportedPermissions = kSupportedPermissions;
  mHubInfoCondition.notify_all();
}

void MultiClientContextHubBase::onDebugDumpData(
    const ::chre::fbs::DebugDumpDataT &data) {
  auto str = std::string(reinterpret_cast<const char *>(data.debug_str.data()),
                         data.debug_str.size());
  debugDumpAppend(str);
}

void MultiClientContextHubBase::onDebugDumpComplete(
    const ::chre::fbs::DebugDumpResponseT &response) {
  if (!response.success) {
    LOGE("Dumping debug information fails");
  }
  if (checkDebugFd()) {
    const std::string &dump = mEventLogger.dump();
    writeToDebugFile(dump.c_str());
    writeToDebugFile("\n-- End of CHRE/ASH debug info --\n");
  }
  debugDumpComplete();
}

void MultiClientContextHubBase::onNanoappListResponse(
    const fbs::NanoappListResponseT &response, HalClientId clientId) {
  std::shared_ptr<IContextHubCallback> callback =
      mHalClientManager->getCallback(clientId);
  if (callback == nullptr) {
    return;
  }
  std::vector<NanoappInfo> appInfoList;
  for (const auto &nanoapp : response.nanoapps) {
    if (nanoapp == nullptr || nanoapp->is_system) {
      continue;
    }
    NanoappInfo appInfo;
    appInfo.nanoappId = nanoapp->app_id;
    appInfo.nanoappVersion = nanoapp->version;
    appInfo.enabled = nanoapp->enabled;
    appInfo.permissions = chreToAndroidPermissions(nanoapp->permissions);

    std::vector<NanoappRpcService> rpcServices;
    for (const auto &service : nanoapp->rpc_services) {
      NanoappRpcService aidlService;
      aidlService.id = service->id;
      aidlService.version = service->version;
      rpcServices.emplace_back(aidlService);
    }
    appInfo.rpcServices = rpcServices;
    appInfoList.push_back(appInfo);
  }
  {
    std::unique_lock<std::mutex> lock(mTestModeMutex);
    if (!mTestModeNanoapps.has_value()) {
      mTestModeNanoapps.emplace();
      for (const auto &appInfo : appInfoList) {
        mTestModeNanoapps->insert(appInfo.nanoappId);
      }
      mEnableTestModeCv.notify_all();
    }
  }

  callback->handleNanoappInfo(appInfoList);
}

void MultiClientContextHubBase::onNanoappLoadResponse(
    const fbs::LoadNanoappResponseT &response, HalClientId clientId) {
  LOGD("Received nanoapp load response for client %" PRIu16
       " transaction %" PRIu32 " fragment %" PRIu32,
       clientId, response.transaction_id, response.fragment_id);
  if (mPreloadedNanoappLoader->isPreloadOngoing()) {
    mPreloadedNanoappLoader->onLoadNanoappResponse(response, clientId);
    return;
  }
  if (!mHalClientManager->isPendingLoadTransactionExpected(
          clientId, response.transaction_id, response.fragment_id)) {
    LOGW("Received a response for client %" PRIu16 " transaction %" PRIu32
         " fragment %" PRIu32
         " that doesn't match the existing transaction. Skipped.",
         clientId, response.transaction_id, response.fragment_id);
    return;
  }
  if (response.success) {
    auto nextFragmentedRequest =
        mHalClientManager->getNextFragmentedLoadRequest();
    if (nextFragmentedRequest.has_value()) {
      // nextFragmentedRequest will only have a value if the pending transaction
      // matches the response and there are more fragments to send. Hold off on
      // calling the callback in this case.
      LOGD("Sending next FragmentedLoadRequest for client %" PRIu16
           ": (transaction: %" PRIu32 ", fragment %zu)",
           clientId, nextFragmentedRequest->transactionId,
           nextFragmentedRequest->fragmentId);
      sendFragmentedLoadRequest(clientId, nextFragmentedRequest.value());
      return;
    }
  } else {
    LOGE("Loading nanoapp fragment for client %" PRIu16 " transaction %" PRIu32
         " fragment %" PRIu32 " failed",
         clientId, response.transaction_id, response.fragment_id);
    mHalClientManager->resetPendingLoadTransaction();
  }
  // At this moment the current pending transaction should either have no more
  // fragment to send or the response indicates its last nanoapp fragment fails
  // to get loaded.
  if (auto callback = mHalClientManager->getCallback(clientId);
      callback != nullptr) {
    callback->handleTransactionResult(response.transaction_id,
                                      /* in_success= */ response.success);
  }
}

void MultiClientContextHubBase::onNanoappUnloadResponse(
    const fbs::UnloadNanoappResponseT &response, HalClientId clientId) {
  if (mHalClientManager->resetPendingUnloadTransaction(
          clientId, response.transaction_id)) {
    {
      std::unique_lock<std::mutex> lock(mTestModeMutex);
      if (response.transaction_id == mTestModeTransactionId) {
        mTestModeSyncUnloadResult.emplace(response.success);
        mEnableTestModeCv.notify_all();
        return;
      }
    }
    if (auto callback = mHalClientManager->getCallback(clientId);
        callback != nullptr) {
      callback->handleTransactionResult(response.transaction_id,
                                        /* in_success= */ response.success);
    }
  }
}

void MultiClientContextHubBase::onNanoappMessage(
    const ::chre::fbs::NanoappMessageT &message) {
  mEventLogger.logMessageFromNanoapp(message);
  ContextHubMessage outMessage;
  outMessage.nanoappId = message.app_id;
  outMessage.hostEndPoint = message.host_endpoint;
  outMessage.messageType = message.message_type;
  outMessage.messageBody = message.message;
  outMessage.permissions = chreToAndroidPermissions(message.permissions);
  auto messageContentPerms =
      chreToAndroidPermissions(message.message_permissions);
  // broadcast message is sent to every connected endpoint
  if (message.host_endpoint == CHRE_HOST_ENDPOINT_BROADCAST) {
    mHalClientManager->sendMessageForAllCallbacks(outMessage,
                                                  messageContentPerms);
  } else if (auto callback = mHalClientManager->getCallbackForEndpoint(
                 message.host_endpoint);
             callback != nullptr) {
    outMessage.hostEndPoint =
        HalClientManager::convertToOriginalEndpointId(message.host_endpoint);
    callback->handleContextHubMessage(outMessage, messageContentPerms);
  }
}

void MultiClientContextHubBase::onClientDied(void *cookie) {
  auto *info = static_cast<HalDeathRecipientCookie *>(cookie);
  info->hal->handleClientDeath(info->clientPid);
}

void MultiClientContextHubBase::handleClientDeath(pid_t clientPid) {
  LOGI("Process %d is dead. Cleaning up.", clientPid);
  if (auto endpoints = mHalClientManager->getAllConnectedEndpoints(clientPid)) {
    for (auto endpointId : *endpoints) {
      LOGI("Sending message to remove endpoint 0x%" PRIx16, endpointId);
      if (!mHalClientManager->mutateEndpointIdFromHostIfNeeded(clientPid,
                                                               endpointId)) {
        continue;
      }
      flatbuffers::FlatBufferBuilder builder(64);
      HostProtocolHost::encodeHostEndpointDisconnected(builder, endpointId);
      mConnection->sendMessage(builder);
    }
  }
  mHalClientManager->handleClientDeath(clientPid);
}

void MultiClientContextHubBase::onChreRestarted() {
  mIsWifiAvailable.reset();
  mEventLogger.logContextHubRestart();
  mHalClientManager->handleChreRestart();
}

binder_status_t MultiClientContextHubBase::dump(int fd,
                                                const char ** /* args */,
                                                uint32_t /* numArgs */) {
  // debugDumpStart waits for the dump to finish before returning.
  debugDumpStart(fd);
  return STATUS_OK;
}

bool MultiClientContextHubBase::requestDebugDump() {
  flatbuffers::FlatBufferBuilder builder;
  HostProtocolHost::encodeDebugDumpRequest(builder);
  return mConnection->sendMessage(builder);
}

void MultiClientContextHubBase::writeToDebugFile(const char *str) {
  if (!::android::base::WriteStringToFd(std::string(str), getDebugFd())) {
    LOGW("Failed to write %zu bytes to debug dump fd", strlen(str));
  }
}
}  // namespace android::hardware::contexthub::common::implementation
