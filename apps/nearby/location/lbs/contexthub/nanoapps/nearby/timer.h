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

#ifndef LOCATION_LBS_CONTEXTHUB_NANOAPPS_NEARBY_TIMER_H_
#define LOCATION_LBS_CONTEXTHUB_NANOAPPS_NEARBY_TIMER_H_

#include <cstdint>

#include "chre_api/chre.h"

namespace nearby {

class Timer {
 public:
  // Constructs timer.
  explicit Timer(bool is_one_shot) : is_one_shot_(is_one_shot) {}

  // Sets timer duration in milliseconds.
  void SetDurationMs(uint32_t duration_ms) {
    duration_ms_ = duration_ms;
  }

  // Starts timer.
  bool StartTimer();

  // Stops timer.
  bool StopTimer();

  // Returns timer id.
  const uint32_t *GetTimerId() {
    return &timer_id_;
  }

 private:
  uint32_t timer_id_ = CHRE_TIMER_INVALID;
  uint32_t duration_ms_ = 0;
  bool is_one_shot_;
};

}  // namespace nearby

#endif  // LOCATION_LBS_CONTEXTHUB_NANOAPPS_NEARBY_TIMER_H_
