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

#include "chre/core/ble_request.h"

#include <inttypes.h>

#include "chre/platform/fatal_error.h"
#include "chre/util/memory.h"

namespace chre {

namespace {

bool filtersMatch(const chreBleGenericFilter &filter,
                  const chreBleGenericFilter &otherFilter) {
  return filter.len == otherFilter.len && filter.type == otherFilter.type &&
         (memcmp(filter.data, otherFilter.data, filter.len) == 0) &&
         (memcmp(filter.dataMask, otherFilter.dataMask, filter.len) == 0);
}

}  // namespace

BleRequest::BleRequest() : BleRequest(0 /* instanceId */, false /* enable */) {}

BleRequest::BleRequest(uint16_t instanceId, bool enable)
    : BleRequest(instanceId, enable, CHRE_BLE_SCAN_MODE_BACKGROUND,
                 0 /* reportDelayMs */, nullptr /* filter */) {}

BleRequest::BleRequest(uint16_t instanceId, bool enable, chreBleScanMode mode,
                       uint32_t reportDelayMs,
                       const chreBleScanFilterV1_9 *filter)
    : mReportDelayMs(reportDelayMs),
      mInstanceId(instanceId),
      mMode(mode),
      mEnabled(enable),
      mRssiThreshold(CHRE_BLE_RSSI_THRESHOLD_NONE),
      mStatus(RequestStatus::PENDING_REQ) {
  if (filter != nullptr) {
    mRssiThreshold = filter->rssiThreshold;
    if (filter->genericFilterCount > 0) {
      if (!mGenericFilters.resize(filter->genericFilterCount)) {
        FATAL_ERROR("Unable to reserve filter count");
      }
      for (size_t i = 0; i < filter->genericFilterCount; i++) {
        mGenericFilters[i] = filter->genericFilters[i];
      }
    }
  }
}

BleRequest::BleRequest(BleRequest &&other) {
  *this = std::move(other);
}

BleRequest &BleRequest::operator=(BleRequest &&other) {
  mInstanceId = other.mInstanceId;
  mMode = other.mMode;
  mReportDelayMs = other.mReportDelayMs;
  mRssiThreshold = other.mRssiThreshold;
  mGenericFilters = std::move(other.mGenericFilters);
  mEnabled = other.mEnabled;
  mStatus = other.mStatus;
  return *this;
}

bool BleRequest::mergeWith(const BleRequest &request) {
  // Only merge parameters of enabled requests.
  if (!request.mEnabled) {
    return false;
  }
  bool attributesChanged = false;
  // Replace disabled request parameters.
  if (!mEnabled) {
    mEnabled = true;
    mMode = request.mMode;
    mReportDelayMs = request.mReportDelayMs;
    mRssiThreshold = request.mRssiThreshold;
    attributesChanged = true;
  } else {
    if (mMode < request.mMode) {
      mMode = request.mMode;
      attributesChanged = true;
    }
    if (mReportDelayMs > request.mReportDelayMs) {
      mReportDelayMs = request.mReportDelayMs;
      attributesChanged = true;
    }
    if (mRssiThreshold > request.mRssiThreshold) {
      mRssiThreshold = request.mRssiThreshold;
      attributesChanged = true;
    }
  }
  const DynamicVector<chreBleGenericFilter> &otherFilters =
      request.mGenericFilters;
  for (const chreBleGenericFilter &otherFilter : otherFilters) {
    bool addFilter = true;
    for (const chreBleGenericFilter &filter : mGenericFilters) {
      if (filtersMatch(filter, otherFilter)) {
        addFilter = false;
        break;
      }
    }
    if (addFilter) {
      attributesChanged = true;
      if (!mGenericFilters.push_back(otherFilter)) {
        FATAL_ERROR("Unable to merge filters");
      }
    }
  }
  return attributesChanged;
}

bool BleRequest::isEquivalentTo(const BleRequest &request) {
  const DynamicVector<chreBleGenericFilter> &otherFilters =
      request.mGenericFilters;
  bool isEquivalent = (mEnabled && request.mEnabled && mMode == request.mMode &&
                       mReportDelayMs == request.mReportDelayMs &&
                       mRssiThreshold == request.mRssiThreshold &&
                       mGenericFilters.size() == otherFilters.size());
  if (isEquivalent) {
    for (size_t i = 0; i < otherFilters.size(); i++) {
      if (!filtersMatch(mGenericFilters[i], otherFilters[i])) {
        isEquivalent = false;
        break;
      }
    }
  }
  return isEquivalent;
}

uint16_t BleRequest::getInstanceId() const {
  return mInstanceId;
}

chreBleScanMode BleRequest::getMode() const {
  return mMode;
}

uint32_t BleRequest::getReportDelayMs() const {
  return mReportDelayMs;
}

int8_t BleRequest::getRssiThreshold() const {
  return mRssiThreshold;
}

RequestStatus BleRequest::getRequestStatus() const {
  return mStatus;
}

void BleRequest::setRequestStatus(RequestStatus status) {
  mStatus = status;
}

const DynamicVector<chreBleGenericFilter> &BleRequest::getGenericFilters()
    const {
  return mGenericFilters;
}

chreBleScanFilterV1_9 BleRequest::getScanFilter() const {
  return chreBleScanFilterV1_9{
      mRssiThreshold, static_cast<uint8_t>(mGenericFilters.size()),
      mGenericFilters.data(), 0 /* broadcasterAddressFilterCount */,
      nullptr /* broadcasterAddressFilters */};
}

bool BleRequest::isEnabled() const {
  return mEnabled;
}

void BleRequest::logStateToBuffer(DebugDumpWrapper &debugDump,
                                  bool isPlatformRequest) const {
  if (!isPlatformRequest) {
    debugDump.print("  instanceId=%" PRIu16 " status=%" PRIu8, mInstanceId,
                    static_cast<uint8_t>(mStatus));
  }
  debugDump.print(" %s", mEnabled ? " enable" : " disable\n");
  if (mEnabled) {
    debugDump.print(
        " mode=%" PRIu8 " reportDelayMs=%" PRIu32 " rssiThreshold=%" PRId8,
        static_cast<uint8_t>(mMode), mReportDelayMs, mRssiThreshold);
    if (isPlatformRequest) {
      debugDump.print(" genericFilters=[");
      for (const chreBleGenericFilter &filter : mGenericFilters) {
        debugDump.print("(type=%" PRIx8, filter.type);
        if (filter.len > 0) {
          debugDump.print(" data=%s dataMask=%s len=%" PRIu8 "), ",
                          &filter.data[0], &filter.dataMask[0], filter.len);
        } else {
          debugDump.print("), ");
        }
      }
      debugDump.print("]\n");
    } else {
      debugDump.print(" genericFilterCount=%" PRIu8 "\n",
                      static_cast<uint8_t>(mGenericFilters.size()));
    }
  }
}

}  // namespace chre