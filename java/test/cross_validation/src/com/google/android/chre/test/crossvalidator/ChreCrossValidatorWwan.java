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

package com.google.android.chre.test.crossvalidator;

import android.content.Context;
import android.hardware.location.ContextHubInfo;
import android.hardware.location.ContextHubManager;
import android.hardware.location.ContextHubTransaction;
import android.hardware.location.NanoAppBinary;
import android.hardware.location.NanoAppMessage;
import android.telephony.CellInfo;
import android.telephony.TelephonyManager;
import android.util.Log;

import com.google.android.chre.nanoapp.proto.ChreCrossValidationWwan;
import com.google.android.chre.nanoapp.proto.ChreTestCommon;
import com.google.android.utils.chre.SettingsUtil;
import com.google.protobuf.InvalidProtocolBufferException;

import org.junit.Assert;
import org.junit.Assume;

import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

public class ChreCrossValidatorWwan extends ChreCrossValidatorBase {
    private static final long AWAIT_RESULT_MESSAGE_TIMEOUT_SEC = 7;

    private static final long NANOAPP_ID = 0x476f6f6754000011L;

    /**
     * Wwan capabilities flags listed in
     * //system/chre/chre_api/include/chre_api/chre/wwan.h
     */
    private static final int WWAN_CAPABILITIES_GET_CELL_INFO = 1;
    private static final int WWAN_CAPABILITIES_GET_CELL_NEIGHBOR_INFO = 2;

    AtomicBoolean mReceivedCapabilites = new AtomicBoolean(false);
    AtomicBoolean mReceivedCellInfoResults = new AtomicBoolean(false);

    private TelephonyManager mTelephonyManager;

    private AtomicReference<ChreCrossValidationWwan.WwanCapabilities> mWwanCapabilities =
            new AtomicReference<ChreCrossValidationWwan.WwanCapabilities>(null);

    private AtomicReference<ChreCrossValidationWwan.WwanCellInfoResult> mChreCellInfoResult =
            new AtomicReference<ChreCrossValidationWwan.WwanCellInfoResult>(null);

    private boolean mInitialAirplaneMode;
    private final SettingsUtil mSettingsUtil;

    public ChreCrossValidatorWwan(
            ContextHubManager contextHubManager, ContextHubInfo contextHubInfo,
            NanoAppBinary nanoAppBinary) {
        super(contextHubManager, contextHubInfo, nanoAppBinary);
        Assert.assertTrue("Nanoapp given to cross validator is not the designated chre cross"
                + " validation nanoapp.",
                nanoAppBinary.getNanoAppId() == NANOAPP_ID);

        Context context =
                androidx.test.platform.app.InstrumentationRegistry
                .getInstrumentation().getTargetContext();
        mSettingsUtil = new SettingsUtil(context);
        mTelephonyManager = context.getSystemService(TelephonyManager.class);
    }

    @Override public void init() throws AssertionError {
        super.init();
        mInitialAirplaneMode = mSettingsUtil.isAirplaneModeOn();
        try {
            mSettingsUtil.setAirplaneMode(false);
        } catch (InterruptedException e) {
            throw new AssertionError("Could not set airplane mode to false.", e);
        }
    }

    @Override public void validate() throws AssertionError, InterruptedException {
        mCollectingData.set(true);
        requestCapabilities();
        waitForCapabilitesFromNanoapp();
        mCollectingData.set(false);
        verifyCapabilities();

        mCollectingData.set(true);
        requestCellInfo();
        waitForCellInfoResultFromNanoapp();
        mCollectingData.set(false);
        verifyResult();
    }

    @Override public void deinit() throws AssertionError {
        super.deinit();
        try {
            mSettingsUtil.setAirplaneMode(mInitialAirplaneMode);
        } catch (InterruptedException e) {
            throw new AssertionError("Failed to restore initial airplane mode state.", e);
        }
    }

    private void requestCapabilities() {
        createAndSendMessage(
                ChreCrossValidationWwan.MessageType.WWAN_CAPABILITIES_REQUEST_VALUE);
    }

    private void requestCellInfo() {
        createAndSendMessage(
                ChreCrossValidationWwan.MessageType.WWAN_CELL_INFO_REQUEST_VALUE);
    }

    private void createAndSendMessage(int type) {
        NanoAppMessage message =  NanoAppMessage.createMessageToNanoApp(
                mNappBinary.getNanoAppId(), type, new byte[0]);
        int result = mContextHubClient.sendMessageToNanoApp(message);
        if (result != ContextHubTransaction.RESULT_SUCCESS) {
            Assert.fail("Failed to create and send WWAN message");
        }
    }

    private void waitForCapabilitesFromNanoapp() throws InterruptedException {
        boolean success =
                mAwaitDataLatch.await(AWAIT_RESULT_MESSAGE_TIMEOUT_SEC, TimeUnit.SECONDS);
        Assert.assertTrue(
                "Timeout waiting for signal: capabilites message from nanoapp",
                success);
        mAwaitDataLatch = new CountDownLatch(1);
        Assert.assertTrue("Timed out for capabilites message from nanoapp",
                mReceivedCapabilites.get());
        if (mErrorStr.get() != null) {
            Assert.fail(mErrorStr.get());
        }
    }

    private void waitForCellInfoResultFromNanoapp() throws InterruptedException {
        boolean success =
                mAwaitDataLatch.await(AWAIT_RESULT_MESSAGE_TIMEOUT_SEC, TimeUnit.SECONDS);
        Assert.assertTrue("Timeout waiting for signal: cell info message from nanoapp", success);
        mAwaitDataLatch = new CountDownLatch(1);
        Assert.assertTrue("Timed out for cell info message from nanoapp",
                mReceivedCellInfoResults.get());
        if (mErrorStr.get() != null) {
            Assert.fail(mErrorStr.get());
        }
    }

    private boolean chreWwanHasBasicCapabilities() {
        return ((mWwanCapabilities.get().getWwanCapabilities()
                & WWAN_CAPABILITIES_GET_CELL_INFO) != 0);
    }

    private boolean chreWwanHasNeighborCapabilities() {
        return ((mWwanCapabilities.get().getWwanCapabilities()
                & WWAN_CAPABILITIES_GET_CELL_NEIGHBOR_INFO) != 0);
    }

    private void verifyCapabilities() {
        Assume.assumeTrue("CHRE WWAN capabilites are insufficient. Skipping test.",
                chreWwanHasBasicCapabilities());

        Log.i(TAG, "CHRE WWAN provides neighbor info ="
                + String.valueOf(chreWwanHasNeighborCapabilities()));
    }

    private void verifyResult() {
        ChreCrossValidationWwan.WwanCellInfoResult result = mChreCellInfoResult.get();

        List<CellInfo> cellInfo = mTelephonyManager.getAllCellInfo();
        Log.i(TAG, "Got the following number of cell towers: " + cellInfo.size());

        int resultCount = result.getCellInfoCount();
        Log.i(TAG, "Result count=" + resultCount);

        if (result.getErrorCode() != 0) {
            Assert.fail("CHRE WWAN results had error=" + result.getErrorCode());
        }

        int expectedResults = cellInfo.size();
        if (!chreWwanHasNeighborCapabilities()) {
            // Compare simple count if neighbors are not supported
            expectedResults = Math.min(expectedResults, 1);
        }

        Assert.assertEquals("Unexpected result count from CHRE cell info. Expected="
                + expectedResults + " Actual=" + resultCount,
                expectedResults, resultCount);

        // TODO(b/365803243): Compare cell info contents.
        // TODO(b/382533059): Identify and close any CellInfo race conditions
    }

    @Override
    protected void parseDataFromNanoAppMessage(NanoAppMessage message) {
        switch (message.getMessageType()) {
            case ChreCrossValidationWwan.MessageType.WWAN_NANOAPP_ERROR_VALUE:
                parseNanoappError(message);
                break;
            case ChreCrossValidationWwan.MessageType.WWAN_CAPABILITIES_VALUE:
                parseCapabilities(message);
                break;
            case ChreCrossValidationWwan.MessageType.WWAN_CELL_INFO_RESULTS_VALUE:
                parseCellInfoResults(message);
                break;
            default:
                setErrorStr("Received message with unexpected type: "
                        + message.getMessageType());
        }
        // Each message should countdown the latch no matter success or fail
        mAwaitDataLatch.countDown();
    }

    private void parseNanoappError(NanoAppMessage message) {
        ChreTestCommon.TestResult testResult = null;
        try {
            testResult = ChreTestCommon.TestResult.parseFrom(message.getMessageBody());
        } catch (InvalidProtocolBufferException e) {
            setErrorStr("Error parsing protobuff: " + e);
            return;
        }
        boolean success = getSuccessFromTestResult(testResult);
        if (!success) {
            setErrorStr("Nanoapp error: " + testResult.getErrorMessage().toStringUtf8());
        }
    }

    private void parseCapabilities(NanoAppMessage message) {
        ChreCrossValidationWwan.WwanCapabilities capabilities = null;
        try {
            capabilities = ChreCrossValidationWwan.WwanCapabilities.parseFrom(
                    message.getMessageBody());
        } catch (InvalidProtocolBufferException e) {
            setErrorStr("Error parsing protobuff: " + e);
            return;
        }
        mWwanCapabilities.set(capabilities);
        mReceivedCapabilites.set(true);
    }

    private void parseCellInfoResults(NanoAppMessage message) {
        ChreCrossValidationWwan.WwanCellInfoResult result = null;
        try {
            result = ChreCrossValidationWwan.WwanCellInfoResult.parseFrom(
                    message.getMessageBody());
        } catch (InvalidProtocolBufferException e) {
            setErrorStr("Error parsing protobuff: " + e);
            return;
        }
        mChreCellInfoResult.set(result);
        mReceivedCellInfoResults.set(true);
    }

    /**
     * @return The boolean indicating test result success or failure from TestResult proto message.
     */
    private boolean getSuccessFromTestResult(ChreTestCommon.TestResult testResult) {
        return testResult.getCode() == ChreTestCommon.TestResult.Code.PASSED;
    }

    // Unused. Required to extend ChreCrossValidatorBase.
    @Override
    protected void unregisterApDataListener() {}
}
