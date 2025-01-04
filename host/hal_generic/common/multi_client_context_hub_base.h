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

#ifndef ANDROID_HARDWARE_CONTEXTHUB_COMMON_MULTICLIENTS_HAL_BASE_H_
#define ANDROID_HARDWARE_CONTEXTHUB_COMMON_MULTICLIENTS_HAL_BASE_H_

#include <aidl/android/hardware/contexthub/BnContextHub.h>
#include <chre_host/generated/host_messages_generated.h>
#include <chre_host/log_message_parser.h>
#include <chre_host/metrics_reporter.h>

#include "chre_connection_callback.h"
#include "chre_host/napp_header.h"
#include "chre_host/preloaded_nanoapp_loader.h"
#include "chre_host/time_syncer.h"
#include "context_hub_v4_impl.h"
#include "debug_dump_helper.h"
#include "event_logger.h"
#include "hal_client_id.h"
#include "hal_client_manager.h"

#include <array>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace android::hardware::contexthub::common::implementation {

using namespace aidl::android::hardware::contexthub;
using namespace android::chre;
using ::ndk::ScopedAStatus;

/**
 * The base class of multiclient HAL.
 *
 * <p>A subclass should initiate mConnection, mHalClientManager and
 * mPreloadedNanoappLoader in its constructor.
 */
class MultiClientContextHubBase
    : public BnContextHub,
      public ::android::hardware::contexthub::common::implementation::
          ChreConnectionCallback,
      public ::android::hardware::contexthub::DebugDumpHelper {
 public:
  /** The entry point of death recipient for a disconnected client. */
  static void onClientDied(void *cookie);

  MultiClientContextHubBase();

  // Functions implementing IContextHub.
  ScopedAStatus getContextHubs(
      std::vector<ContextHubInfo> *contextHubInfos) override;
  ScopedAStatus loadNanoapp(int32_t contextHubId,
                            const NanoappBinary &appBinary,
                            int32_t transactionId) override;
  ScopedAStatus unloadNanoapp(int32_t contextHubId, int64_t appId,
                              int32_t transactionId) override;
  ScopedAStatus disableNanoapp(int32_t contextHubId, int64_t appId,
                               int32_t transactionId) override;
  ScopedAStatus enableNanoapp(int32_t contextHubId, int64_t appId,
                              int32_t transactionId) override;
  ScopedAStatus onSettingChanged(Setting setting, bool enabled) override;
  ScopedAStatus queryNanoapps(int32_t contextHubId) override;
  ScopedAStatus registerCallback(
      int32_t contextHubId,
      const std::shared_ptr<IContextHubCallback> &callback) override;
  ScopedAStatus sendMessageToHub(int32_t contextHubId,
                                 const ContextHubMessage &message) override;
  ScopedAStatus onHostEndpointConnected(const HostEndpointInfo &info) override;
  ScopedAStatus onHostEndpointDisconnected(char16_t in_hostEndpointId) override;
  ScopedAStatus getPreloadedNanoappIds(int32_t contextHubId,
                                       std::vector<int64_t> *result) override;
  ScopedAStatus onNanSessionStateChanged(
      const NanSessionStateUpdate &in_update) override;
  ScopedAStatus setTestMode(bool enable) override;
  ScopedAStatus sendMessageDeliveryStatusToHub(
      int32_t contextHubId,
      const MessageDeliveryStatus &messageDeliveryStatus) override;
  ScopedAStatus getHubs(std::vector<HubInfo> *hubs) override;
  ScopedAStatus getEndpoints(std::vector<EndpointInfo> *endpoints) override;
  ScopedAStatus registerEndpointHub(
      const std::shared_ptr<IEndpointCallback> &callback,
      const HubInfo &hubInfo,
      std::shared_ptr<IEndpointCommunication> *hubInterface) override;

  // Functions implementing ChreConnectionCallback.
  void handleMessageFromChre(const unsigned char *messageBuffer,
                             size_t messageLen) override;
  void onChreRestarted() override;
  void onChreDisconnected() override {
    mIsChreReady = false;
  }

  // Functions for dumping debug information.
  binder_status_t dump(int fd, const char **args, uint32_t numArgs) override;
  bool requestDebugDump() override;
  void writeToDebugFile(const char *str) override;

 protected:
  // The timeout for a reliable message.
  constexpr static std::chrono::nanoseconds kReliableMessageTimeout =
      std::chrono::seconds(1);

  // The data needed by the death client to clear states of a client.
  struct HalDeathRecipientCookie {
    MultiClientContextHubBase *hal;
    pid_t clientPid;
    HalDeathRecipientCookie(MultiClientContextHubBase *hal, pid_t pid) {
      this->hal = hal;
      this->clientPid = pid;
    }
  };

  // Contains information about a reliable message that has been received.
  struct ReliableMessageRecord {
    std::chrono::time_point<std::chrono::steady_clock> timestamp;
    int32_t messageSequenceNumber;
    HostEndpointId hostEndpointId;

    bool isExpired() const {
      return timestamp + kReliableMessageTimeout <
             std::chrono::steady_clock::now();
    }

    bool operator>(const ReliableMessageRecord &other) const {
      return timestamp > other.timestamp;
    }
  };

  void tryTimeSync(size_t numOfRetries, useconds_t retryDelayUs) {
    if (mConnection->isTimeSyncNeeded()) {
      TimeSyncer::sendTimeSyncWithRetry(mConnection.get(), numOfRetries,
                                        retryDelayUs);
    }
  }

  bool sendFragmentedLoadRequest(HalClientId clientId,
                                 FragmentedLoadRequest &fragmentedLoadRequest);

  // Functions handling various types of messages
  void handleHubInfoResponse(const ::chre::fbs::HubInfoResponseT &message);
  void onNanoappListResponse(const ::chre::fbs::NanoappListResponseT &response,
                             HalClientId clientid);
  void onNanoappLoadResponse(const ::chre::fbs::LoadNanoappResponseT &response,
                             HalClientId clientId);
  void onNanoappUnloadResponse(
      const ::chre::fbs::UnloadNanoappResponseT &response,
      HalClientId clientId);
  void onNanoappMessage(const ::chre::fbs::NanoappMessageT &message);
  void onMessageDeliveryStatus(
      const ::chre::fbs::MessageDeliveryStatusT &status);
  void onDebugDumpData(const ::chre::fbs::DebugDumpDataT &data);
  void onDebugDumpComplete(
      const ::chre::fbs::DebugDumpResponseT & /* response */);
  void onMetricLog(const ::chre::fbs::MetricLogT &metricMessage);
  void handleClientDeath(pid_t pid);
  void handleLogMessageV2(const ::chre::fbs::LogMessageV2T &logMessage);

  /**
   * Enables test mode by unloading all the nanoapps except the system nanoapps.
   * Requires the caller to hold the mTestModeMutex. This function does not
   * set mIsTestModeEnabled.
   *
   * @return true as long as we have a list of nanoapps to unload.
   */
  bool enableTestModeLocked(std::unique_lock<std::mutex> &lock);

  /**
   * Enables test mode by unloading all the nanoapps except the system nanoapps.
   *
   * @return true as long as we have a list of nanoapps to unload.
   */
  bool enableTestMode();

  /**
   * Disables test mode by reloading all the <b>preloaded</b> nanoapps except
   * system nanoapps.
   *
   * <p>Note that dynamically loaded nanoapps that are unloaded during
   * enableTestMode() are not reloaded back because HAL doesn't track the
   * location of their binaries.
   */
  void disableTestMode();

  /**
   * Queries nanoapps from the context hub with the given client ID.
   *
   * @param contextHubId The context hub ID.
   * @param clientId The client ID.
   * @return A ScopedAStatus indicating the success or failure of the query.
   */
  ScopedAStatus queryNanoappsWithClientId(int32_t contextHubId,
                                          HalClientId clientId);

  /**
   * Handles a nanoapp list response from the context hub for test mode
   * enablement.
   *
   * @param response The nanoapp list response from the context hub.
   */
  void handleTestModeNanoappQueryResponse(
      const ::chre::fbs::NanoappListResponseT &response);

  inline bool isSettingEnabled(Setting setting) {
    return mSettingEnabled.find(setting) != mSettingEnabled.end() &&
           mSettingEnabled[setting];
  }

  /**
   * Removes messages from the reliable message queue that have been received
   * by the host more than kReliableMessageTimeout ago.
   */
  void cleanupReliableMessageQueueLocked();

  HalClientManager::DeadClientUnlinker mDeadClientUnlinker;

  // HAL is the unique owner of the communication channel to CHRE.
  std::unique_ptr<ChreConnection> mConnection{};

  // HalClientManager maintains states of hal clients. Each HAL should only have
  // one instance of a HalClientManager.
  std::unique_ptr<HalClientManager> mHalClientManager{};

  // Implementation of the V4+ API. Should be instantiated by the target HAL
  // implementation.
  std::optional<ContextHubV4Impl> mV4Impl{};

  std::unique_ptr<PreloadedNanoappLoader> mPreloadedNanoappLoader{};

  std::unique_ptr<ContextHubInfo> mContextHubInfo;

  // Mutex and CV are used to get context hub info synchronously.
  std::mutex mHubInfoMutex;
  std::condition_variable mHubInfoCondition;

  // Death recipient handling clients' disconnections
  ndk::ScopedAIBinder_DeathRecipient mDeathRecipient;

  // States of settings
  std::unordered_map<Setting, bool> mSettingEnabled;
  std::optional<bool> mIsWifiAvailable;
  std::optional<bool> mIsBleAvailable;

  // A mutex to synchronize access to the list of preloaded nanoapp IDs.
  std::mutex mPreloadedNanoappIdsMutex;
  std::optional<std::vector<uint64_t>> mPreloadedNanoappIds{};

  // test mode settings
  std::mutex mTestModeMutex;
  std::condition_variable mEnableTestModeCv;
  bool mIsTestModeEnabled = false;
  std::optional<bool> mTestModeSyncUnloadResult = std::nullopt;

  // mTestModeNanoapps records the nanoapps that will be unloaded in
  // enableTestMode().
  std::optional<std::vector<uint64_t>> mTestModeNanoapps;
  // mTestModeSystemNanoapps records system nanoapps that won't be reloaded in
  // disableTestMode().
  std::optional<std::vector<uint64_t>> mTestModeSystemNanoapps;

  EventLogger mEventLogger;

  // The parser of buffered logs from CHRE
  LogMessageParser mLogger;

  // Metrics reporter that will report metrics if it is initialized to non-null.
  std::unique_ptr<MetricsReporter> mMetricsReporter;

  // Used to map message sequence number to host endpoint ID
  std::mutex mReliableMessageMutex;
  std::deque<ReliableMessageRecord> mReliableMessageQueue;

  // A thread safe flag indicating if CHRE is ready for operations.
  // Outside of the constructor, this boolean flag should only be written by
  // onChreDisconnected and onChreRestarted, the order of which should be
  // guaranteed by the CHRE's disconnection handler.
  std::atomic_bool mIsChreReady = true;

  // TODO(b/333567700): Remove when cleaning up the bug_fix_hal_reliable_message_record flag
  std::unordered_map<int32_t, HostEndpointId> mReliableMessageMap;
};
}  // namespace android::hardware::contexthub::common::implementation
#endif  // ANDROID_HARDWARE_CONTEXTHUB_COMMON_MULTICLIENTS_HAL_BASE_H_
