/*
 * Copyright (C) 2020 The Android Open Source Project
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

#ifndef CHRE_PLATFORM_AOC_SYSTEM_TIMER_BASE_H_
#define CHRE_PLATFORM_AOC_SYSTEM_TIMER_BASE_H_

#include <cstdint>

#include "chre/platform/assert.h"
#include "chre/util/time_impl.h"

#include "efw/include/timer.h"

#include "FreeRTOS.h"
#include "task.h"

namespace chre {

/**
 * The AOC platform base class for the SystemTimer. The AOC implementation uses
 * a EFW timer.
 */
class SystemTimerBase {
 protected:
  //! The timer handle that is generated by Timer::EventAdd.
  void *mTimerHandle;

  // TODO(b/168645313): Share the same dispatch thread amongst all timer
  // instances.
  //! A FreeRTOS Thread to dispatch timer callbacks
  TaskHandle_t mTimerCbDispatchThreadHandle = nullptr;

  //! Stack size (in words) of the timer callback dispatch thread
  static constexpr uint32_t kStackDepthWords = configMINIMAL_STACK_SIZE;

  //! Task stack associated with this timer.
  StackType_t mTaskStack[kStackDepthWords];

  //! FreeRTOS struct to hold the TCB of the timer dispatch thread
  StaticTask_t mTaskBuffer;

  //! Tracks whether the timer has been initialized correctly.
  bool mInitialized = false;

  //! A static method that is invoked by the underlying EFW timer.
  static bool systemTimerNotifyCallback(void *context);

  //! This function implements the timer callback dispatch thread.
  // It blocks until it's woken up by the underlying system timer's ISR,
  // then executes the CHRE timer callback from the dispatch thread context.
  static void timerCallbackDispatch(void *context);
};

static_assert(configSUPPORT_STATIC_ALLOCATION == 1,
              "Static task allocation must be supported!");

}  // namespace chre

#endif  // CHRE_PLATFORM_AOC_SYSTEM_TIMER_BASE_H_
