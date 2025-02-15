/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include <general_test/basic_ble_test.h>
#include <shared/macros.h>
#include <shared/send_message.h>

#include "chre/util/nanoapp/ble.h"
#include "chre/util/nanoapp/log.h"
#include "chre/util/time.h"
#include "chre_api/chre.h"

#define LOG_TAG "[GeneralTest][Ble]"
/*
 * Test to check expected functionality of the CHRE BLE APIs.
 */
namespace general_test {

using chre::createBleScanFilterForKnownBeacons;
using chre::ble_constants::kNumScanFilters;

namespace {
const uint32_t gFlushCookie = 0;
constexpr uint32_t kGoodReservedValue = 0;
constexpr uint8_t kMaxReportAdvertisingSid = 0x0f;
}  // namespace

void testScanSessionAsync(bool supportsBatching, bool supportsFiltering) {
  uint32_t reportDelayMs = supportsBatching ? 1000 : 0;

  struct chreBleScanFilter filter;
  chreBleGenericFilter uuidFilters[kNumScanFilters];
  if (supportsFiltering) {
    createBleScanFilterForKnownBeacons(filter, uuidFilters, kNumScanFilters);
  }

  if (!chreBleStartScanAsync(CHRE_BLE_SCAN_MODE_FOREGROUND /* mode */,
                             reportDelayMs,
                             supportsFiltering ? &filter : nullptr)) {
    EXPECT_FAIL_RETURN("Failed to start a BLE scan in the foreground");
  }
}

BasicBleTest::BasicBleTest()
    : Test(CHRE_API_VERSION_1_7),
      mFlushWasCalled(false),
      mSupportsBatching(false) {}

void BasicBleTest::setUp(uint32_t messageSize, const void * /* message */) {
  if (messageSize != 0) {
    EXPECT_FAIL_RETURN("Expected 0 byte message, got more bytes:",
                       &messageSize);
  }

  mSupportsBatching =
      isCapabilitySet(CHRE_BLE_CAPABILITIES_SCAN_RESULT_BATCHING);
  mSupportsFiltering =
      isFilterCapabilitySet(CHRE_BLE_FILTER_CAPABILITIES_SERVICE_DATA) &&
      isFilterCapabilitySet(CHRE_BLE_FILTER_CAPABILITIES_RSSI);

  if (!isCapabilitySet(CHRE_BLE_CAPABILITIES_SCAN)) {
    mTestSuccessMarker.markStageAndSuccessOnFinish(BASIC_BLE_TEST_STAGE_SCAN);
    mTestSuccessMarker.markStageAndSuccessOnFinish(BASIC_BLE_TEST_STAGE_FLUSH);
    return;
  }

  testScanSessionAsync(mSupportsBatching, mSupportsFiltering);
  if (!mSupportsBatching) {
    mTestSuccessMarker.markStageAndSuccessOnFinish(BASIC_BLE_TEST_STAGE_FLUSH);
  }
}

void BasicBleTest::handleBleAsyncResult(const chreAsyncResult *result) {
  if (result == nullptr) {
    EXPECT_FAIL_RETURN("Received null BLE async result");
    return;
  }
  if (!result->success) {
    LOGE("Received unsuccessful BLE async result, error code %" PRIu8,
         result->errorCode);
    EXPECT_FAIL_RETURN("Received unsuccessful BLE async result");
    return;
  }

  switch (result->requestType) {
    case CHRE_BLE_REQUEST_TYPE_START_SCAN:
      // Wait one second to allow any advertisement events to propagate
      // and be verified by handleAdvertisementEvent.
      if (chreTimerSet(chre::kOneSecondInNanoseconds, nullptr, true) ==
          CHRE_TIMER_INVALID) {
        EXPECT_FAIL_RETURN(
            "Failed to start a timer after BLE started scanning");
      }
      break;
    case CHRE_BLE_REQUEST_TYPE_FLUSH:
      if (result->cookie != &gFlushCookie) {
        EXPECT_FAIL_RETURN("Cookie values do not match");
      }
      break;
    case CHRE_BLE_REQUEST_TYPE_STOP_SCAN:
      mTestSuccessMarker.markStageAndSuccessOnFinish(BASIC_BLE_TEST_STAGE_SCAN);
      break;
    default:
      EXPECT_FAIL_RETURN("Unexpected request type");
      break;
  }
}

void BasicBleTest::handleAdvertisementEvent(
    const chreBleAdvertisementEvent *event) {
  if (event == nullptr) {
    EXPECT_FAIL_RETURN("Invalid chreBleAdvertisementEvent");
  } else if (event->reserved != kGoodReservedValue) {
    EXPECT_FAIL_RETURN("chreBleAdvertisementEvent: reserved != 0");
  } else {
    for (uint16_t i = 0; i < event->numReports; ++i) {
      const struct chreBleAdvertisingReport &report = event->reports[i];
      if (report.advertisingSid != CHRE_BLE_ADI_NONE &&
          report.advertisingSid > kMaxReportAdvertisingSid) {
        EXPECT_FAIL_RETURN(
            "chreBleAdvertisingReport: advertisingSid is invalid");
      } else if (report.reserved != kGoodReservedValue) {
        EXPECT_FAIL_RETURN("chreBleAdvertisingReport: reserved is invalid");
      }
    }
  }
}

void BasicBleTest::handleTimerEvent() {
  if (mSupportsBatching) {
    if (!chreBleFlushAsync(&gFlushCookie)) {
      EXPECT_FAIL_RETURN("Failed to BLE flush");
    }
    mFlushWasCalled = true;
  } else {
    if (chreBleFlushAsync(&gFlushCookie)) {
      EXPECT_FAIL_RETURN(
          "chreBleFlushAsync should return false if batching is not supported");
    }

    if (!chreBleStopScanAsync()) {
      EXPECT_FAIL_RETURN("Failed to stop a BLE scan session");
    }
  }
}

void BasicBleTest::handleEvent(uint32_t /* senderInstanceId */,
                               uint16_t eventType, const void *eventData) {
  switch (eventType) {
    case CHRE_EVENT_BLE_ASYNC_RESULT:
      handleBleAsyncResult(static_cast<const chreAsyncResult *>(eventData));
      break;
    case CHRE_EVENT_BLE_FLUSH_COMPLETE:
      if (!mFlushWasCalled) {
        EXPECT_FAIL_RETURN(
            "Received CHRE_EVENT_BLE_FLUSH_COMPLETE event when "
            "chreBleFlushAsync was not called");
      }
      if (!chreBleStopScanAsync()) {
        EXPECT_FAIL_RETURN("Failed to stop a BLE scan session");
      }
      mTestSuccessMarker.markStageAndSuccessOnFinish(
          BASIC_BLE_TEST_STAGE_FLUSH);
      break;
    case CHRE_EVENT_BLE_ADVERTISEMENT:
      handleAdvertisementEvent(
          static_cast<const chreBleAdvertisementEvent *>(eventData));
      break;
    case CHRE_EVENT_BLE_BATCH_COMPLETE:
      // Ignore the event only if we support batching.
      // Otherwise, it is an unexpected event.
      if (!mSupportsBatching) {
        unexpectedEvent(eventType);
      }
      break;
    case CHRE_EVENT_TIMER:
      handleTimerEvent();
      break;
    default:
      unexpectedEvent(eventType);
      break;
  }
}

}  // namespace general_test
