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

#include <cstdint>
#include <mutex>

#include "chre/platform/linux/system_time.h"

#include "gtest/gtest.h"
#include "inc/test_util.h"
#include "test_base.h"
#include "test_event.h"
#include "test_event_queue.h"
#include "test_util.h"

using ::chre::platform_linux::overrideMonotonicTime;
using ::chre::platform_linux::SystemTimeOverride;

namespace chre {
namespace {

class DelayEventTest : public TestBase {};

CREATE_CHRE_TEST_EVENT(DELAY_EVENT, 0);

constexpr Seconds kDelayEventInterval(2);
std::mutex gMutex;

class DelayEventNanoapp : public TestNanoapp {
 public:
  bool start() override {
    return true;
  }

  void handleEvent(uint32_t, uint16_t eventType,
                   const void *eventData) override {
    switch (eventType) {
      case CHRE_EVENT_TEST_EVENT: {
        auto event = static_cast<const TestEvent *>(eventData);
        switch (event->type) {
          case DELAY_EVENT: {
            std::lock_guard<std::mutex> lock(gMutex);

            if (!hasSeenDelayEvent) {
              overrideMonotonicTime(SystemTime::getMonotonicTime() +
                                    kDelayEventInterval);
              hasSeenDelayEvent = true;
            }
            TestEventQueueSingleton::get()->pushEvent(DELAY_EVENT);
            break;
          }
        }
      }
    }
  }

 private:
  bool hasSeenDelayEvent = false;
};

TEST_F(DelayEventTest, DelayedEventIsFlagged) {
  constexpr uint32_t kDelayEventCount = 3;
  SystemTimeOverride override(0);
  uint64_t appId = loadNanoapp(MakeUnique<DelayEventNanoapp>());

  testing::internal::CaptureStdout();
  {
    std::lock_guard<std::mutex> lock(gMutex);
    for (uint32_t i = 0; i < kDelayEventCount; ++i) {
      overrideMonotonicTime(SystemTime::getMonotonicTime() + Nanoseconds(1));
      sendEventToNanoapp(appId, DELAY_EVENT);
    }
  }

  for (uint32_t i = 0; i < kDelayEventCount; ++i) {
    waitForEvent(DELAY_EVENT);
  }

  std::string output = testing::internal::GetCapturedStdout();
  EXPECT_NE(output.find("Delayed event"), std::string::npos);
}

}  // namespace
}  // namespace chre