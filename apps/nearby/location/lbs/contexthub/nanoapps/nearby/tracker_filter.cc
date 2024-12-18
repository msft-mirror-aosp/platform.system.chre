#include "location/lbs/contexthub/nanoapps/nearby/tracker_filter.h"

#include <inttypes.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

#include "chre_api/chre.h"
#include "location/lbs/contexthub/nanoapps/nearby/byte_array.h"
#include "location/lbs/contexthub/nanoapps/nearby/hw_filter.h"
#include "location/lbs/contexthub/nanoapps/nearby/nearby_extension.h"
#include "location/lbs/contexthub/nanoapps/nearby/proto/nearby_extension.nanopb.h"
#include "location/lbs/contexthub/nanoapps/nearby/tracker_storage.h"
#include "third_party/contexthub/chre/util/include/chre/util/dynamic_vector.h"
#include "third_party/contexthub/chre/util/include/chre/util/nanoapp/log.h"
#include "third_party/nanopb/pb.h"
#include "third_party/nanopb/pb_encode.h"

#define LOG_TAG "[NEARBY][TRACKER_FILTER]"

namespace nearby {

const size_t kChreBleGenericFilterDataSize = 29;
constexpr nearby_extension_TrackerReport kEmptyTrackerReport =
    nearby_extension_TrackerReport_init_zero;

void TrackerFilter::Update(
    const chreHostEndpointInfo &host_info,
    const nearby_extension_ExtConfigRequest_TrackerFilterConfig &filter_config,
    chre::DynamicVector<chreBleGenericFilter> *generic_filters,
    nearby_extension_ExtConfigResponse *config_response) {
  host_info_ = host_info;
  LOGD("Update tracker filters %u from %s", filter_config.hardware_filter_count,
       host_info_.packageName);
  config_response->has_result = true;
  config_response->result = CHREX_NEARBY_RESULT_OK;
  // Sets scan filter configuration and returns hardware filters to
  // generic_filters used in ble scanner's scan settings.
  chre::DynamicVector<chreBleGenericFilter> hardware_filters;
  for (int i = 0; i < filter_config.hardware_filter_count; i++) {
    const nearby_extension_ChreBleGenericFilter &hw_filter =
        filter_config.hardware_filter[i];
    chreBleGenericFilter generic_filter;
    generic_filter.type = hw_filter.type;
    generic_filter.len = static_cast<uint8_t>(hw_filter.len);
    memcpy(generic_filter.data, hw_filter.data, kChreBleGenericFilterDataSize);
    memcpy(generic_filter.dataMask, hw_filter.data_mask,
           kChreBleGenericFilterDataSize);
    generic_filters->push_back(generic_filter);
    hardware_filters.push_back(generic_filter);
  }
  scan_filter_config_.hardware_filters = std::move(hardware_filters);
  scan_filter_config_.rssi_threshold = filter_config.rssi_threshold;
  scan_filter_config_.active_interval_ms = filter_config.active_interval_ms;
  scan_filter_config_.active_window_ms = filter_config.active_window_ms;
  ConfigureActiveState();
  ConfigureScanControlTimers();
  // Sets batch configuration
  batch_config_.sample_interval_ms = filter_config.sample_interval_ms;
  batch_config_.max_tracker_count = filter_config.max_tracker_count;
  batch_config_.notify_threshold_tracker_count =
      filter_config.notify_threshold_tracker_count;
  batch_config_.max_history_count = filter_config.max_history_count;
  batch_config_.lost_timeout_ms = filter_config.lost_timeout_ms;
  batch_config_.opportunistic_flush_threshold_time_ms =
      filter_config.opportunistic_flush_threshold_time_ms;
}

void TrackerFilter::ConfigureActiveState() {
  if (!scan_filter_config_.hardware_filters.empty()) {
    SetActiveState();
  } else {
    ClearActiveState();
  }
}

void TrackerFilter::ConfigureScanControlTimers() {
  // The timer based scan is only enabled when the hardware scan filters are not
  // empty and the active window and interval are valid. The active interval
  // must be greater than the active window so that the timer based scan can
  // function properly.
  if (!scan_filter_config_.hardware_filters.empty() &&
      scan_filter_config_.active_window_ms > 0) {
    if (scan_filter_config_.active_interval_ms <=
        scan_filter_config_.active_window_ms) {
      LOGE("Invalid active interval %" PRIu32
           " ms, must be greater than active window %" PRIu32 " ms.",
           scan_filter_config_.active_interval_ms,
           scan_filter_config_.active_window_ms);
      return;
    }
    // Sets active interval and window timer duration.
    active_interval_timer_.SetDurationMs(
        scan_filter_config_.active_interval_ms);
    active_window_timer_.SetDurationMs(scan_filter_config_.active_window_ms);
    // Starts active interval and window timers.
    if (active_interval_timer_.StartTimer()) {
      active_window_timer_.StartTimer();
    }
  } else if (scan_filter_config_.hardware_filters.empty()) {
    active_interval_timer_.StopTimer();
  }
}

void TrackerFilter::MatchAndSave(
    const chre::DynamicVector<chreBleAdvertisingReport> &ble_adv_reports,
    TrackerStorage &tracker_storage) {
  for (const auto &report : ble_adv_reports) {
    if (HwFilter::CheckRssi(scan_filter_config_.rssi_threshold, report) &&
        HwFilter::Match(scan_filter_config_.hardware_filters, report)) {
      tracker_storage.Push(report, batch_config_);
    }
  }
}

bool TrackerFilter::EncodeTrackerReport(TrackerReport &tracker_report,
                                        ByteArray data_buf,
                                        size_t *encoded_size) {
  nearby_extension_TrackerReport filter_result = kEmptyTrackerReport;
  filter_result.has_report = true;
  nearby_extension_ChreBleAdvertisingReport &report_proto =
      filter_result.report;
  report_proto.has_timestamp = true;
  report_proto.timestamp =
      tracker_report.header.timestamp +
      static_cast<uint64_t>(chreGetEstimatedHostTimeOffset());
  report_proto.has_event_type_and_data_status = true;
  report_proto.event_type_and_data_status =
      tracker_report.header.eventTypeAndDataStatus;
  report_proto.has_address_type = true;
  report_proto.address_type =
      static_cast<nearby_extension_ChreBleAdvertisingReport_AddressType>(
          tracker_report.header.addressType);
  report_proto.has_address = true;
  for (size_t i = 0; i < CHRE_BLE_ADDRESS_LEN; i++) {
    report_proto.address[i] = tracker_report.header.address[i];
  }
  report_proto.has_tx_power = true;
  report_proto.tx_power = tracker_report.header.txPower;
  report_proto.has_rssi = true;
  report_proto.rssi = tracker_report.header.rssi;
  report_proto.has_data_length = true;
  report_proto.data_length = tracker_report.header.dataLength;
  if (tracker_report.header.dataLength > 0) {
    report_proto.has_data = true;
  }
  for (size_t i = 0; i < tracker_report.header.dataLength; i++) {
    report_proto.data[i] = tracker_report.data[i];
  }
  size_t idx = 0;
  for (const auto &history : tracker_report.historian) {
    nearby_extension_TrackerHistory &history_proto =
        filter_result.historian[idx];
    history_proto.has_found_count = true;
    history_proto.found_count = history.found_count;
    history_proto.has_first_found_time_ms = true;
    history_proto.first_found_time_ms = history.first_found_time_ms;
    history_proto.has_last_found_time_ms = true;
    history_proto.last_found_time_ms = history.last_found_time_ms;
    history_proto.has_lost_time_ms = true;
    history_proto.lost_time_ms = history.lost_time_ms;
    history_proto.has_state = true;
    history_proto.state =
        history.state == TrackerState::kPresent
            ? nearby_extension_TrackerHistory_TrackerState_PRESENT
            : nearby_extension_TrackerHistory_TrackerState_ABSENT;
    ++idx;
  }
  filter_result.historian_count = static_cast<pb_size_t>(idx);
  if (!pb_get_encoded_size(encoded_size, nearby_extension_TrackerReport_fields,
                           &filter_result)) {
    LOGE("Failed to get batch filter result size.");
    return false;
  }
  pb_ostream_t ostream = pb_ostream_from_buffer(data_buf.data, data_buf.length);
  if (!pb_encode(&ostream, nearby_extension_TrackerReport_fields,
                 &filter_result)) {
    LOGE("Unable to encode protobuf for BatchFilterResult, error %s",
         PB_GET_ERROR(&ostream));
    return false;
  }
  return true;
}

}  // namespace nearby
