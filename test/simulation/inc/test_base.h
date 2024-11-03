/*
 * Copyright (C) 2021 The Android Open Source Project
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

#ifndef CHRE_SIMULATION_TEST_BASE_H_
#define CHRE_SIMULATION_TEST_BASE_H_

#include <gtest/gtest.h>
#include <cstdint>
#include <thread>

#include "chre/core/event_loop_manager.h"
#include "chre/core/nanoapp.h"
#include "chre/platform/system_time.h"
#include "chre/platform/system_timer.h"
#include "chre/util/system/message_router.h"
#include "chre/util/time.h"
#include "test_event_queue.h"

namespace chre {

// TODO(b/346903946): remove these extra debug logs once issue resolved
#define CHRE_TEST_DEBUG(fmt, ...)                                        \
  do {                                                                   \
    fprintf(stderr, "%" PRIu64 "ns %s: " fmt "\n",                       \
            SystemTime::getMonotonicTime().toRawNanoseconds(), __func__, \
            ##__VA_ARGS__);                                              \
    fprintf(stdout, "%" PRIu64 "ns %s: " fmt "\n",                       \
            SystemTime::getMonotonicTime().toRawNanoseconds(), __func__, \
            ##__VA_ARGS__);                                              \
  } while (0)

/*
 * A base class for all CHRE simulated tests.
 */
class TestBase : public testing::Test {
 protected:
  TestBase() {
    CHRE_TEST_DEBUG("Constructed %p", this);
  }
  ~TestBase() {
    CHRE_TEST_DEBUG("Destroying %p", this);
  }

  void SetUp() override;
  void TearDown() override;

  /**
   * This method can be overridden in a derived class if desired.
   *
   * @return The total runtime allowed for the entire test.
   */
  virtual uint64_t getTimeoutNs() const {
    return 5 * kOneSecondInNanoseconds;
  }

  /**
   * A convenience method to invoke waitForEvent() for the TestEventQueue
   * singleton.
   *
   * Note: Events that are intended to be delivered to a nanoapp as a result of
   * asynchronous APIs invoked in a nanoappEnd() functions may not be delivered
   * to the nanoapp through nanoappHandleEvent() (since they are already
   * unloaded by the time it receives the event), so users of the TestEventQueue
   * should not wait for such events in their test flow.
   *
   * @param eventType The event type to wait for.
   */
  void waitForEvent(uint16_t eventType) {
    TestEventQueueSingleton::get()->waitForEvent(eventType);
  }

  /**
   * A convenience method to invoke waitForEvent() for the TestEventQueue
   * singleton.
   *
   * @see waitForEvent(eventType)
   *
   * @param eventType The event type to wait for.
   * @param eventData Populated with the data attached to the event.
   */
  template <class T>
  void waitForEvent(uint16_t eventType, T *eventData) {
    TestEventQueueSingleton::get()->waitForEvent(eventType, eventData);
  }

  /**
   * Retrieves the Nanoapp instance from its ID.
   *
   * @param id Nanoapp ID
   * @return A pointer to the Nanoapp instance or nullptr if not found.
   */
  Nanoapp *getNanoappByAppId(uint64_t id) {
    uint16_t instanceId;
    EXPECT_TRUE(EventLoopManagerSingleton::get()
                    ->getEventLoop()
                    .findNanoappInstanceIdByAppId(id, &instanceId));
    Nanoapp *nanoapp = EventLoopManagerSingleton::get()
                           ->getEventLoop()
                           .findNanoappByInstanceId(instanceId);
    EXPECT_NE(nanoapp, nullptr);
    return nanoapp;
  }

  class MemberInitLogger {
   public:
    MemberInitLogger() {
      CHRE_TEST_DEBUG("Construction start");
    }
    ~MemberInitLogger() {
      CHRE_TEST_DEBUG("Destruction finished");
    }
  };

  MemberInitLogger mInitLogger;
  std::thread mChreThread;
  SystemTimer mSystemTimer;
  message::MessageRouter::MessageHub mChreMessageHub;
};

}  // namespace chre

#endif  // CHRE_SIMULATION_TEST_BASE_H_
