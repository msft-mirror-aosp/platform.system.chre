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
#ifndef CHPP_WIFI_TYPES_H_
#define CHPP_WIFI_TYPES_H_

// This file was automatically generated by chre_api_to_chpp.py
// Date: 2020-09-16 17:30:38 UTC
// Source: chre_api/include/chre_api/chre/wifi.h @ commit 8aff727a

// DO NOT modify this file directly, as those changes will be lost the next
// time the script is executed

#include <stdbool.h>
#include <stdint.h>

#include "chpp/app.h"
#include "chpp/macros.h"
#include "chpp/services/common_types.h"
#include "chre_api/chre/version.h"
#include "chre_api/chre/wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

CHPP_PACKED_START

//! See {@link chreWifiScanResult} for details
struct ChppWifiScanResult {
  uint32_t ageMs;
  uint16_t capabilityInfo;
  uint8_t ssidLen;
  uint8_t ssid[32];
  uint8_t bssid[6];
  uint8_t flags;
  int8_t rssi;
  uint8_t band;
  uint32_t primaryChannel;
  uint32_t centerFreqPrimary;
  uint32_t centerFreqSecondary;
  uint8_t channelWidth;
  uint8_t securityMode;
  uint8_t radioChain;
  int8_t rssiChain0;
  int8_t rssiChain1;
  uint8_t reserved[7];  // Input ignored; always set to 0
} CHPP_PACKED_ATTR;

//! See {@link chreWifiScanEvent} for details
struct ChppWifiScanEvent {
  uint8_t version;  // Input ignored; always set to CHRE_WIFI_SCAN_EVENT_VERSION
  uint8_t resultCount;
  uint8_t resultTotal;
  uint8_t eventIndex;
  uint8_t scanType;
  uint8_t ssidSetSize;
  uint16_t scannedFreqListLen;
  uint64_t referenceTime;
  struct ChppOffset scannedFreqList;  // References scannedFreqListLen instances
                                      // of struct ChppOffset
  struct ChppOffset
      results;  // References resultCount instances of struct ChppOffset
  uint8_t radioChainPref;
} CHPP_PACKED_ATTR;

//! See {@link chreWifiSsidListItem} for details
struct ChppWifiSsidListItem {
  uint8_t ssidLen;
  uint8_t ssid[32];
} CHPP_PACKED_ATTR;

//! See {@link chreWifiScanParams} for details
struct ChppWifiScanParams {
  uint8_t scanType;
  uint32_t maxScanAgeMs;
  uint16_t frequencyListLen;
  struct ChppOffset frequencyList;  // References frequencyListLen instances of
                                    // struct ChppOffset
  uint8_t ssidListLen;
  struct ChppOffset
      ssidList;  // References ssidListLen instances of struct ChppOffset
  uint8_t radioChainPref;
} CHPP_PACKED_ATTR;

//! CHPP app header plus struct ChppWifiScanEventWithHeader
struct ChppWifiScanEventWithHeader {
  struct ChppAppHeader header;
  struct ChppWifiScanEvent payload;
} CHPP_PACKED_ATTR;

//! CHPP app header plus struct ChppWifiScanParamsWithHeader
struct ChppWifiScanParamsWithHeader {
  struct ChppAppHeader header;
  struct ChppWifiScanParams payload;
} CHPP_PACKED_ATTR;

CHPP_PACKED_END

// Encoding functions (CHRE --> CHPP)

/**
 * Converts from given CHRE structure to serialized CHPP type.
 *
 * @param in Fully-formed CHRE structure.
 * @param out Upon success, will point to a buffer allocated with chppMalloc().
 * It is the responsibility of the caller to set the values of the CHPP app
 * layer header, and to free the buffer when it is no longer needed via
 * chppFree() or CHPP_FREE_AND_NULLIFY().
 * @param outSize Upon success, will be set to the size of the output buffer, in
 * bytes.
 *
 * @return true on success, false if memory allocation failed.
 */
bool chppWifiScanEventFromChre(const struct chreWifiScanEvent *in,
                               struct ChppWifiScanEventWithHeader **out,
                               size_t *outSize);

/**
 * Converts from given CHRE structure to serialized CHPP type.
 *
 * @param in Fully-formed CHRE structure.
 * @param out Upon success, will point to a buffer allocated with chppMalloc().
 * It is the responsibility of the caller to set the values of the CHPP app
 * layer header, and to free the buffer when it is no longer needed via
 * chppFree() or CHPP_FREE_AND_NULLIFY().
 * @param outSize Upon success, will be set to the size of the output buffer, in
 * bytes.
 *
 * @return true on success, false if memory allocation failed.
 */
bool chppWifiScanParamsFromChre(const struct chreWifiScanParams *in,
                                struct ChppWifiScanParamsWithHeader **out,
                                size_t *outSize);

// Decoding functions (CHPP --> CHRE)

/**
 * Converts from serialized CHPP structure to a CHRE type.
 *
 * @param in Fully-formed CHPP structure.
 * @param in Size of the CHPP structure in bytes.
 *
 * @return If successful, a pointer to a CHRE structure allocated with
 * chppMalloc(). If unsuccessful, null. It is the responsibility of the caller
 * to free the buffer when it is no longer needed via chppFree() or
 * CHPP_FREE_AND_NULLIFY().
 */
struct chreWifiScanEvent *chppWifiScanEventToChre(
    const struct ChppWifiScanEvent *in, size_t inSize);

/**
 * Converts from serialized CHPP structure to a CHRE type.
 *
 * @param in Fully-formed CHPP structure.
 * @param in Size of the CHPP structure in bytes.
 *
 * @return If successful, a pointer to a CHRE structure allocated with
 * chppMalloc(). If unsuccessful, null. It is the responsibility of the caller
 * to free the buffer when it is no longer needed via chppFree() or
 * CHPP_FREE_AND_NULLIFY().
 */
struct chreWifiScanParams *chppWifiScanParamsToChre(
    const struct ChppWifiScanParams *in, size_t inSize);

#ifdef __cplusplus
}
#endif

#endif  // CHPP_WIFI_TYPES_H_
