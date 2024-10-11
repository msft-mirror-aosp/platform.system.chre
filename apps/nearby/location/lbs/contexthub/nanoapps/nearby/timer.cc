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

#include "location/lbs/contexthub/nanoapps/nearby/timer.h"

#include <chre.h>

#include "third_party/contexthub/chre/util/include/chre/util/nanoapp/log.h"
#include "third_party/contexthub/chre/util/include/chre/util/time.h"

#define LOG_TAG "[NEARBY][TIMER]"

namespace nearby {

bool Timer::StartTimer() {
  if (duration_ms_ == 0) {
    LOGD("Timer is not started. Timer duration is 0.");
    return false;
  }
  if (!is_one_shot_ && timer_id_ != CHRE_TIMER_INVALID) {
    chreTimerCancel(timer_id_);
    timer_id_ = CHRE_TIMER_INVALID;
  }
  timer_id_ = chreTimerSet(duration_ms_ * chre::kOneMillisecondInNanoseconds,
                           &timer_id_, /*oneShot=*/is_one_shot_);
  if (timer_id_ == CHRE_TIMER_INVALID) {
    LOGE("Error in configuring timer.");
    return false;
  }
  return true;
}

bool Timer::StopTimer() {
  if (timer_id_ == CHRE_TIMER_INVALID) {
    LOGD("Timer is already stopped.");
    return false;
  }
  if (!chreTimerCancel(timer_id_)) {
    LOGW(
        "Error in stopping timer. For a one-shot timer, it may have just "
        "fired or expired.");
    return false;
  }
  timer_id_ = CHRE_TIMER_INVALID;
  return true;
}

}  // namespace nearby
