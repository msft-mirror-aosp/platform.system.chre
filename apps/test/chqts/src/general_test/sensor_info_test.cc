/*
 * Copyright (C) 2017 The Android Open Source Project
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
#include <general_test/sensor_info_test.h>

#include <shared/macros.h>
#include <shared/send_message.h>

namespace general_test {

SensorInfoTest::SensorInfoTest() : Test(CHRE_API_VERSION_1_1) {}

void SensorInfoTest::setUp(uint32_t messageSize, const void * /* message */) {
  if (messageSize != 0) {
    EXPECT_FAIL_RETURN("Expected 0 byte message, got more bytes:",
                       &messageSize);
  } else if (!chreSensorFindDefault(CHRE_SENSOR_TYPE_ACCELEROMETER,
                                    &mSensorHandle)) {
    EXPECT_FAIL_RETURN("CHRE implementation does not have an accelerometer");
  } else {
    struct chreSensorInfo info;

    if (!chreGetSensorInfo(mSensorHandle, &info)) {
      EXPECT_FAIL_RETURN("Failed to gather sensor info");
    } else {
      mCompleted = true;
      validateSensorInfo(info);
    }
  }
}

void SensorInfoTest::validateSensorInfo(
    const struct chreSensorInfo &info) const {
  if ((mApiVersion < CHRE_API_VERSION_1_1) && (info.minInterval != 0)) {
    EXPECT_FAIL_RETURN("Sensor minimum interval is non-zero");
  } else if (info.minInterval == 0) {
    EXPECT_FAIL_RETURN("Sensor minimum interval is unknown");
  } else if (!chreSensorConfigure(
                 mSensorHandle, CHRE_SENSOR_CONFIGURE_MODE_CONTINUOUS,
                 info.minInterval, CHRE_SENSOR_LATENCY_DEFAULT)) {
    EXPECT_FAIL_RETURN("Sensor failed configuration with minimum interval");
  } else if (!chreSensorConfigureModeOnly(mSensorHandle,
                                          CHRE_SENSOR_CONFIGURE_MODE_DONE)) {
    EXPECT_FAIL_RETURN("Unable to configure sensor mode to DONE");
  } else {
    nanoapp_testing::sendSuccessToHost();
  }
}

void SensorInfoTest::handleEvent(uint32_t /* senderInstanceId */,
                                 uint16_t eventType,
                                 const void * /* eventData */) {
  if (!mCompleted) {
    unexpectedEvent(eventType);
  }
}

}  // namespace general_test
