#ifndef LOCATION_LBS_CONTEXTHUB_NANOAPPS_NEARBY_TRACKER_STORAGE_H_
#define LOCATION_LBS_CONTEXTHUB_NANOAPPS_NEARBY_TRACKER_STORAGE_H_

#include <cstddef>
#include <cstdint>
#include <utility>

#include "chre_api/chre.h"
#include "third_party/contexthub/chre/util/include/chre/util/dynamic_vector.h"
#include "third_party/contexthub/chre/util/include/chre/util/unique_ptr.h"

namespace nearby {

// The callback interface for tracker storage events.
class TrackerStorageCallbackInterface {
 public:
  virtual ~TrackerStorageCallbackInterface() = default;

  // Is called when sending a batch storage full event.
  virtual void OnTrackerStorageFullEvent() = 0;
};

struct TrackerBatchConfig {
  // Minimum sampling interval to update tracker history.
  uint32_t sample_interval_ms;
  // Maximum number of tracker reports that can be stored in storage.
  uint32_t max_tracker_count;
  // Notification threshold of the number of tracker reports, which should be
  // equal to or smaller than max_tracker_count.
  uint32_t notify_threshold_tracker_count;
  // Maximum number of tracker histories that can be stored in tracker report.
  uint32_t max_history_count;
  // Timeout for tracker history to be considered lost.
  uint32_t lost_timeout_ms;
  // Time based threshold for opportunistic flush of tracker reports.
  uint32_t opportunistic_flush_threshold_time_ms;
};

enum class TrackerState {
  kPresent,
  kAbsent,
};

struct TrackerHistory {
  // Constructor to set the current time and default values.
  explicit TrackerHistory(uint32_t current_time_ms)
      : found_count(1),
        first_found_time_ms(current_time_ms),
        last_found_time_ms(current_time_ms),
        last_radio_discovery_time_ms(current_time_ms),
        lost_time_ms(0),
        state(TrackerState::kPresent) {}
  // The number of times the tracker report was found at each sampling interval
  // when in the Present state.
  uint32_t found_count;
  // The time when the tracker report was first discovered when it was not in
  // the present state, and the time when the tracker history was created.
  uint32_t first_found_time_ms;
  // The most recent time when the tracker report was discovered for each
  // sampling period in the Present state.
  uint32_t last_found_time_ms;
  // The most recent time when the tracker report was discovered by the LE
  // radio, regardless of the sampling period or the tracker state.
  uint32_t last_radio_discovery_time_ms;
  // The time at which the tracker report was lost. Only valid when the tracker
  // state is Absent.
  uint32_t lost_time_ms;
  // The latest state of the tracker history.
  TrackerState state;
};

struct TrackerReport {
  // Default constructor.
  TrackerReport() = default;

  // Move constructor.
  TrackerReport(TrackerReport &&other) {
    if (&other == this) {
      return;
    }
    header = other.header;
    data = std::move(other.data);
    historian = std::move(other.historian);
  }
  // Header of advertisement for the key report.
  chreBleAdvertisingReport header;
  // Data of advertisement for the key report.
  chre::UniquePtr<uint8_t[]> data;
  // Tracker history for the key report.
  chre::DynamicVector<TrackerHistory> historian;
};

class TrackerStorage {
 public:
  // Constructs tracker storage.
  TrackerStorage() = default;

  // Adds advertise report to tracker storage.
  void Push(const chreBleAdvertisingReport &report,
            const TrackerBatchConfig &config);

  // Updates the tracker history for present and absent trackers in the storage.
  void Refresh(const TrackerBatchConfig &config);

  // Clears tracker storage.
  void Clear() {
    tracker_reports_.clear();
  }

  // Return tracker batch reports in storage.
  chre::DynamicVector<TrackerReport> &GetBatchReports() {
    return tracker_reports_;
  }

  // Sets tracker storage event callback.
  void SetCallback(TrackerStorageCallbackInterface *callback) {
    callback_ = callback;
  }

 private:
  // Default size reserved for tracker history in creating a new tracker report.
  static constexpr size_t kDefaultTrackerHistorySize = 2;

  // Tracker batch reports.
  // TODO(b/341757839): Optimize tracker storage memory using
  // chre::SegmentedQueue to minimize heap fragmentation.
  chre::DynamicVector<TrackerReport> tracker_reports_;

  // Tracker storage event callback.
  TrackerStorageCallbackInterface *callback_ = nullptr;

  // Updates tracker report in pushing advertisement.
  void UpdateTrackerReport(TrackerReport &tracker_report,
                           const TrackerBatchConfig &config,
                           const chreBleAdvertisingReport &report);

  // Adds a new tracker report to tracker storage.
  void AddTrackerReport(const chreBleAdvertisingReport &report,
                        const TrackerBatchConfig &config);

  // Adds or updates advertising data for tracker report.
  // For a newly added tracker report, it will allocate memory for advertising
  // data, and copy the advertising data from the advertising report.
  // For an existing tracker report, it will check if the advertising data is
  // different from the previous one. If the length is the same but the payload
  // is different, it will update the tracker report by copying the advertising
  // data from the advertising report. If the length is different, it will
  // update the tracker report by re-allocating memory for advertising data, and
  // copying the advertising data from the advertising report.
  // If the advertising data is the same as the previous one, it will not do
  // anything.
  void AddOrUpdateAdvertisingData(TrackerReport &tracker_report,
                                  const chreBleAdvertisingReport &report);

  // Returns whether advertising address is same.
  bool IsEqualAddress(const TrackerReport &tracker_report,
                      const chreBleAdvertisingReport &report) const;

  // Returns current time in milliseconds.
  uint32_t GetCurrentTimeMs() const;
};

}  // namespace nearby

#endif  // LOCATION_LBS_CONTEXTHUB_NANOAPPS_NEARBY_TRACKER_STORAGE_H_
