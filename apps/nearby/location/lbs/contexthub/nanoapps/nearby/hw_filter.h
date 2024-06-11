#ifndef LOCATION_LBS_CONTEXTHUB_NANOAPPS_NEARBY_HW_FILTER_H_
#define LOCATION_LBS_CONTEXTHUB_NANOAPPS_NEARBY_HW_FILTER_H_

#include <cstdint>

#include "chre_api/chre.h"
#include "third_party/contexthub/chre/util/include/chre/util/dynamic_vector.h"
namespace nearby {

class HwFilter {
 public:
  // Matches BLE advertisement with hardware filter and returns the result.
  static bool Match(
      const chre::DynamicVector<chreBleGenericFilter> &hardware_filters,
      const chreBleAdvertisingReport &report);

  // Checks signal strength in the BLE advertisement report against RSSI
  // threshold in the scan filter. Returns true if the signal strength is
  // higher than the RSSI threshold or the RSSI threshold is CHRE_BLE_RSSI_NONE.
  static bool CheckRssi(int8_t rssi_threshold,
                        const chreBleAdvertisingReport &report);
};

}  // namespace nearby

#endif  // LOCATION_LBS_CONTEXTHUB_NANOAPPS_NEARBY_HW_FILTER_H_
