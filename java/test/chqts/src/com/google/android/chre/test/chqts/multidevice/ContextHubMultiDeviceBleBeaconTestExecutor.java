/*
 * Copyright (C) 2023 The Android Open Source Project
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

package com.google.android.chre.test.chqts.multidevice;

import android.hardware.location.NanoAppBinary;

import com.google.android.chre.test.chqts.ContextHubBleTestExecutor;
import com.google.android.utils.chre.ChreApiTestUtil;

import java.util.Arrays;
import java.util.List;
import java.util.concurrent.Future;

import dev.chre.rpc.proto.ChreApiTest;

public class ContextHubMultiDeviceBleBeaconTestExecutor extends ContextHubBleTestExecutor {
    private static final int NUM_EVENTS_TO_GATHER = 10;

    private static final long TIMEOUT_IN_S = 30;

    private static final long TIMEOUT_IN_NS = TIMEOUT_IN_S * 1000000000L;

    /**
     * The minimum offset in bytes of a BLE advertisement report which includes the length
     * and type of the report.
     */
    private static final int BLE_ADVERTISEMENT_DATA_HEADER_OFFSET = 2;

    public ContextHubMultiDeviceBleBeaconTestExecutor(NanoAppBinary nanoapp) {
        super(nanoapp);
    }

    /**
     * Gathers BLE advertisement events from the nanoapp for TIMEOUT_IN_NS or up to
     * NUM_EVENTS_TO_GATHER events. This function returns true if all
     * chreBleAdvertisingReport's contain advertisments for Google Eddystone and
     * there is at least one advertisement, otherwise it returns false.
     */
    public boolean gatherAndVerifyChreBleAdvertisementsForGoogleEddystone() throws Exception {
        List<ChreApiTest.GeneralEventsMessage> events = gatherChreBleEvents();
        if (events == null) {
            return false;
        }

        for (ChreApiTest.GeneralEventsMessage event: events) {
            if (!event.hasChreBleAdvertisementEvent()) {
                continue;
            }

            ChreApiTest.ChreBleAdvertisementEvent bleAdvertisementEvent =
                    event.getChreBleAdvertisementEvent();
            for (int i = 0; i < bleAdvertisementEvent.getReportsCount(); ++i) {
                ChreApiTest.ChreBleAdvertisingReport report = bleAdvertisementEvent.getReports(i);
                byte[] data = report.getData().toByteArray();
                if (data == null || data.length < BLE_ADVERTISEMENT_DATA_HEADER_OFFSET) {
                    continue;
                }

                if (searchForGoogleEddystoneAdvertisement(data)) {
                    return true;
                }
            }
        }
        return false;
    }

    /**
     * Gathers BLE advertisement events from the nanoapp for TIMEOUT_IN_NS or up to
     * NUM_EVENTS_TO_GATHER events. This function returns true if all
     * chreBleAdvertisingReport's contain advertisments with CHRE test manufacturer ID and
     * there is at least one advertisement, otherwise it returns false.
     */
    public boolean gatherAndVerifyChreBleAdvertisementsWithManufacturerData() throws Exception {
        List<ChreApiTest.GeneralEventsMessage> events = gatherChreBleEvents();
        if (events == null) {
            return false;
        }

        for (ChreApiTest.GeneralEventsMessage event: events) {
            if (!event.hasChreBleAdvertisementEvent()) {
                continue;
            }

            ChreApiTest.ChreBleAdvertisementEvent bleAdvertisementEvent =
                    event.getChreBleAdvertisementEvent();
            for (int i = 0; i < bleAdvertisementEvent.getReportsCount(); ++i) {
                ChreApiTest.ChreBleAdvertisingReport report = bleAdvertisementEvent.getReports(i);
                byte[] data = report.getData().toByteArray();
                if (data == null || data.length < BLE_ADVERTISEMENT_DATA_HEADER_OFFSET) {
                    continue;
                }

                if (searchForManufacturerAdvertisement(data)) {
                    return true;
                }
            }
        }
        return false;
    }

    /**
     * Gathers CHRE BLE advertisement events.
     */
    private List<ChreApiTest.GeneralEventsMessage> gatherChreBleEvents() throws Exception {
        Future<List<ChreApiTest.GeneralEventsMessage>> eventsFuture =
                new ChreApiTestUtil().gatherEvents(
                        mRpcClients.get(0),
                        Arrays.asList(CHRE_EVENT_BLE_ADVERTISEMENT),
                        NUM_EVENTS_TO_GATHER,
                        TIMEOUT_IN_NS);
        List<ChreApiTest.GeneralEventsMessage> events = eventsFuture.get();
        return events;
    }

    /**
     * Starts a BLE scan with the Google Eddystone filter.
     */
    public void chreBleStartScanSyncWithGoogleEddystoneFilter() throws Exception {
        chreBleStartScanSync(getGoogleEddystoneScanFilter());
    }

    /**
     * Starts a BLE scan with test manufacturer data.
     */
    public void chreBleStartScanSyncWithManufacturerData() throws Exception {
        chreBleStartScanSync(getManufacturerDataScanFilterChre());
    }

     /**
     * Returns true if the data contains an advertisement for Google Eddystone,
     * otherwise returns false.
     */
    private boolean searchForGoogleEddystoneAdvertisement(byte[] data) {
        if (data.length < 2) {
            return false;
        }

        for (int j = 0; j < data.length - 1; ++j) {
            if (Byte.compare(data[j], (byte) 0xAA) == 0
                    && Byte.compare(data[j + 1], (byte) 0xFE) == 0) {
                return true;
            }
        }
        return false;
    }

    /**
     * Returns true if the data contains an advertisement for CHRE test manufacturer data,
     * otherwise returns false.
     */
    private boolean searchForManufacturerAdvertisement(byte[] data) {
        if (data.length < 2) {
            return false;
        }

        for (int j = 0; j < data.length - 1; ++j) {
            if (Byte.compare(data[j], (byte) 0xEE) == 0
                    && Byte.compare(data[j + 1], (byte) 0xEE) == 0) {
                return true;
            }
        }

        return false;
    }
}
