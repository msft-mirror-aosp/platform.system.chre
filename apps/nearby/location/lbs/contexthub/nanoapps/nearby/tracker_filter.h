#ifndef LOCATION_LBS_CONTEXTHUB_NANOAPPS_NEARBY_TRACKER_FILTER_H_
#define LOCATION_LBS_CONTEXTHUB_NANOAPPS_NEARBY_TRACKER_FILTER_H_

#include <cstdint>

#include "chre_api/chre.h"
#include "location/lbs/contexthub/nanoapps/nearby/byte_array.h"
#include "location/lbs/contexthub/nanoapps/nearby/proto/nearby_extension.nanopb.h"
#include "location/lbs/contexthub/nanoapps/nearby/tracker_storage.h"
#include "third_party/contexthub/chre/util/include/chre/util/dynamic_vector.h"

namespace nearby {

struct TrackerScanFilterConfig {
  chre::DynamicVector<chreBleGenericFilter> hardware_filters;
  int8_t rssi_threshold = CHRE_BLE_RSSI_THRESHOLD_NONE;
};

class TrackerFilter {
 public:
  // Updates scan filter and batch configurations.
  // Returns generic_filters, which can be used to restart BLE scan through
  // BleScanner::UpdateTrackerFilters() and BleScanner::Restart(). Refers to
  // AppManager::HandleExtTrackerFilterConfig. Regarding to the ownership of
  // hardware filters, generic_filters are saved in two places - TrackerFilter
  // and BleScanner. TrackerFilter uses the saved generic_filters for matching
  // advertisements and BleScanner uses the saved generic_filters for
  // reconfiguring scan configuration whenever hardsare scan filters are
  // updated. If config_response->result is not CHREX_NEARBY_RESULT_OK, the
  // returned generic_filters should be ignored.
  void Update(const chreHostEndpointInfo &host_info,
              const nearby_extension_ExtConfigRequest_TrackerFilterConfig
                  &filter_config,
              chre::DynamicVector<chreBleGenericFilter> *generic_filters,
              nearby_extension_ExtConfigResponse *config_response);

  // Matches BLE advertisements and pushes the matched advertisements to
  // tracker storage.
  void MatchAndSave(
      const chre::DynamicVector<chreBleAdvertisingReport> &ble_adv_reports,
      TrackerStorage &tracker_storage);

  // Whether tracker filter is empty. Currently, we're checking only hardware
  // scan filters used for tracker filter.
  bool IsEmpty() const {
    return scan_filter_config_.hardware_filters.empty();
  }

  // Returns host end point for tracker filter.
  uint16_t GetHostEndPoint() const {
    return host_info_.hostEndpointId;
  }

  // Returns batch configuration for tracker filter.
  TrackerBatchConfig GetBatchConfig() const {
    return batch_config_;
  }

  // Encodes a single tracker report into data_buf.
  static bool EncodeTrackerReport(TrackerReport &tracker_report,
                                  ByteArray data_buf, size_t *encoded_size);

 private:
  TrackerScanFilterConfig scan_filter_config_;
  TrackerBatchConfig batch_config_;
  chreHostEndpointInfo host_info_;
};

}  // namespace nearby

#endif  // LOCATION_LBS_CONTEXTHUB_NANOAPPS_NEARBY_TRACKER_FILTER_H_
