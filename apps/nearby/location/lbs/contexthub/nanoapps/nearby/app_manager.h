/*
 * Copyright (C) 2023 The Android Open Source Project
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

#ifndef LOCATION_LBS_CONTEXTHUB_NANOAPPS_NEARBY_APP_MANAGER_H_
#define LOCATION_LBS_CONTEXTHUB_NANOAPPS_NEARBY_APP_MANAGER_H_

#include <cstdint>

#include "location/lbs/contexthub/nanoapps/nearby/proto/nearby_extension.nanopb.h"
#include "location/lbs/contexthub/nanoapps/nearby/tracker_filter.h"
#include "location/lbs/contexthub/nanoapps/nearby/tracker_storage.h"
#include "third_party/contexthub/chre/util/include/chre/util/dynamic_vector.h"
#include "third_party/nanopb/pb.h"
#ifdef NEARBY_PROFILE
#include <ash/profile.h>
#endif

#include <chre.h>

#include "location/lbs/contexthub/nanoapps/nearby/adv_report_cache.h"
#include "location/lbs/contexthub/nanoapps/nearby/ble_scanner.h"
#include "location/lbs/contexthub/nanoapps/nearby/filter.h"
#include "location/lbs/contexthub/nanoapps/nearby/filter_extension.h"
#include "third_party/contexthub/chre/util/include/chre/util/singleton.h"
#include "third_party/contexthub/chre/util/include/chre/util/time.h"

namespace nearby {

// AppManager handles events from CHRE as well messages with host.
class AppManager : public TrackerStorageCallbackInterface {
  friend class AppManagerTest;

 public:
  AppManager();
  // Returns true if AppManager is initialized successfully.
  bool IsInitialized();

  // Handles an event from CHRE.
  void HandleEvent(uint32_t sender_instance_id, uint16_t event_type,
                   const void *event_data);

 private:
  // Handles a message from host.
  void HandleMessageFromHost(const chreMessageFromHostData *event);

  // Acknowledge a host's SET_FILTER_REQUEST to indicate success or failure.
  void RespondHostSetFilterRequest(bool success);

  // Handles config request from the host.
  void HandleHostConfigRequest(const uint8_t *message, uint32_t message_size);

  // Handles advertise reports to match filters.
  // Advertise reports will be cleared at the end of this function.
  void HandleMatchAdvReports(AdvReportCache &adv_reports);

  // If encoded byte size for filter results is larger than output buffer, sends
  // each filter result. Otherwise, sends filter results together.
  void SendBulkFilterResultsToHost(
      const chre::DynamicVector<nearby_BleFilterResult> &filter_results);

  // Serializes filter_results into stream after encoding as BleFilterResults.
  // Returns false if encoding fails.
  void SendFilterResultsToHost(
      const chre::DynamicVector<nearby_BleFilterResult> &filter_results);

  // Serializes a filter_result into stream after encoding as BleFilterResults.
  // Returns false if encoding fails.
  void SendFilterResultToHost(const nearby_BleFilterResult &filter_result);

  // Updates Filter extension with event. Returns true if event is sent
  // from an OEM service.
  bool UpdateFilterExtension(const chreMessageFromHostData *event);

  // Updates BLE scan state to start or stop based on filter configurations.
  void UpdateBleScanState();

  // Encodes filter results as BleFilterResults format. Returns true if encoding
  // was successful.
  static bool EncodeFilterResults(
      const chre::DynamicVector<nearby_BleFilterResult> &filter_results,
      pb_ostream_t *stream, size_t *msg_size);

  // Encodes a filter result as BleFilterResults format. Returns true if
  // encoding was successful.
  static bool EncodeFilterResult(const nearby_BleFilterResult &filter_result,
                                 pb_ostream_t *stream, size_t *msg_size);

  // Gets the expected encoded message size of filter results, which is
  // equivalent to msg_size output of EncodeFilterResults()
  static bool GetEncodedSizeFromFilterResults(
      const chre::DynamicVector<nearby_BleFilterResult> &filter_results,
      size_t &encoded_size);

  // Handles extended config request from the host.
  void HandleHostExtConfigRequest(const chreMessageFromHostData *event);

  // Handles extended filter config request from the host.
  bool HandleExtFilterConfig(
      const chreHostEndpointInfo &host_info,
      const nearby_extension_ExtConfigRequest_FilterConfig &config,
      nearby_extension_ExtConfigResponse *config_response);

  // Handles extended service config request from the host.
  bool HandleExtServiceConfig(
      const chreHostEndpointInfo &host_info,
      const nearby_extension_ExtConfigRequest_ServiceConfig &config,
      nearby_extension_ExtConfigResponse *config_response);

  // BLE scan keep alive timer callback.
  void OnBleScanKeepAliveTimerCallback();

  // Handles tracker filter config request from the host.
  bool HandleExtTrackerFilterConfig(
      const chreHostEndpointInfo &host_info,
      const nearby_extension_ExtConfigRequest_TrackerFilterConfig &config,
      nearby_extension_ExtConfigResponse *config_response);

  // Handles flush tracker reports request from the host.
  void HandleExtFlushTrackerReports(
      const chreHostEndpointInfo &host_info,
      const nearby_extension_ExtConfigRequest_FlushTrackerReports &config,
      nearby_extension_ExtConfigResponse *config_response);

  // Sends a tracker storage full event to host.
  void SendTrackerStorageFullEventToHost();

  // Sends tracker reports to host.
  void SendTrackerReportsToHost(
      chre::DynamicVector<TrackerReport> &tracker_reports);

  // TrackerStorageCallbackInterface API called when sending a tracker
  // storage full event to host.
  void OnTrackerStorageFullEvent() override;

  static void SendExtConfigResponseToHost(
      uint32_t request_id, uint16_t host_end_point,
      nearby_extension_ExtConfigResponse &config_response);

  static void SendFilterExtensionResultToHost(
      chre::DynamicVector<FilterExtensionResult> &filter_results);

  static const char *GetExtConfigNameFromTag(pb_size_t config_tag);
  // TODO(b/193756395): Find the optimal size or compute the size in runtime.
  // Note: the nanopb API pb_get_encoded_size
  // (https://jpa.kapsi.fi/nanopb/docs/reference.html#pb_get_encoded_size)
  // can only get the encoded message size if the message does not contained
  // repeated fields. Otherwise, the repeated fields require Callback field
  // encoders, which need a pb_ostream_t to work with, while pb_ostream_t is
  // initialized by a buffer with the size to be determined.
  // It seems possible to compute a message size with repeated field by
  // rehearsing the encoding without actually storing in memory. Explore to
  // enhance nanopb API to extend pb_get_encoded_size for repeated fields.
  static constexpr size_t kFilterResultsBufSize = 400;
  static constexpr size_t kTrackerReportsBufSize = 800;
  // Default value for Fast Pair cache to expire.
  static constexpr uint64_t kFpFilterResultExpireTimeNanoSec =
#ifdef USE_SHORT_FP_CACHE_TO
      3 * chre::kOneSecondInNanoseconds;
#else
      5 * chre::kOneSecondInNanoseconds;
#endif

  Filter filter_;
  FilterExtension filter_extension_;
  BleScanner ble_scanner_;
  TrackerFilter tracker_filter_;
  TrackerStorage tracker_storage_;

  uint16_t host_endpoint_ = 0;
  bool screen_on_ = false;
  bool fp_screen_on_sent_ = false;
  AdvReportCache adv_reports_cache_;
  chre::DynamicVector<nearby_BleFilterResult> fp_filter_cache_results_;
  chre::DynamicVector<FilterExtensionResult>
      screen_on_filter_extension_results_;
  uint64_t fp_filter_cache_time_nanosec_;
  uint64_t fp_filter_cache_expire_nanosec_ = kFpFilterResultExpireTimeNanoSec;
#ifdef NEARBY_PROFILE
  ashProfileData profile_data_;
#endif
};

// The singleton AppManager that will be initialized safely.
typedef chre::Singleton<AppManager> AppManagerSingleton;

}  // namespace nearby

#endif  // LOCATION_LBS_CONTEXTHUB_NANOAPPS_NEARBY_APP_MANAGER_H_
