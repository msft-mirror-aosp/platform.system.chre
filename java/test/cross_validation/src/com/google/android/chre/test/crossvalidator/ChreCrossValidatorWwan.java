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
import android.telephony.CellIdentityGsm;
import android.telephony.CellIdentityLte;
import android.telephony.CellIdentityNr;
import android.telephony.CellIdentityWcdma;
import android.telephony.CellInfo;
import android.telephony.CellInfoGsm;
import android.telephony.CellInfoLte;
import android.telephony.CellInfoNr;
import android.telephony.CellInfoWcdma;
import android.telephony.CellSignalStrengthGsm;
import android.telephony.CellSignalStrengthLte;
import android.telephony.CellSignalStrengthNr;
import android.telephony.CellSignalStrengthWcdma;
import android.telephony.TelephonyManager;
import android.telephony.TelephonyManager.CellInfoCallback;
import android.util.Log;

import com.google.android.chre.nanoapp.proto.ChreCrossValidationWwan;
import com.google.android.chre.nanoapp.proto.ChreTestCommon;
import com.google.android.utils.chre.SettingsUtil;
import com.google.protobuf.InvalidProtocolBufferException;

import org.junit.Assert;
import org.junit.Assume;

import java.util.Iterator;
import java.util.List;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

public class ChreCrossValidatorWwan extends ChreCrossValidatorBase implements Executor {
    private static final long AWAIT_CELL_INFO_FOR_AP_TIMEOUT_SEC = 7;
    private static final long AWAIT_RESULT_MESSAGE_TIMEOUT_SEC = 7;

    private static final long NANOAPP_ID = 0x476f6f6754000011L;

    // Maximum difference between signal strength values in dBm. A small
    // difference is allowed for data updates between AP and CHRE readings.
    private static final int SIGNAL_STRENGTH_TOLERANCE_DBM = 4;

    // Maximum difference between signal strength values in ASU.
    // ASU is calculated based on 3GPP RSSI. Refer to 3GPP 27.007 (Ver 10.3.0) Sec 8.69
    private static final int SIGNAL_STRENGTH_TOLERANCE_ASU = 4;

    /** Wwan capabilities flags listed in //system/chre/chre_api/include/chre_api/chre/wwan.h */
    private static final int WWAN_CAPABILITIES_GET_CELL_INFO = 1;

    private static final int WWAN_CAPABILITIES_GET_CELL_NEIGHBOR_INFO = 2;

    List<CellInfo> mApCellInfo;

    AtomicBoolean mReceivedCapabilites = new AtomicBoolean(false);
    AtomicBoolean mReceivedCellInfoResults = new AtomicBoolean(false);

    BlockingQueue<CellInfoOrError> mApCellInfoQueue = new ArrayBlockingQueue<>(1);

    private TelephonyManager mTelephonyManager;

    private AtomicReference<ChreCrossValidationWwan.WwanCapabilities> mWwanCapabilities =
            new AtomicReference<ChreCrossValidationWwan.WwanCapabilities>(null);

    private AtomicReference<ChreCrossValidationWwan.WwanCellInfoResult> mChreCellInfoResult =
            new AtomicReference<ChreCrossValidationWwan.WwanCellInfoResult>(null);

    private boolean mInitialAirplaneMode;
    private final SettingsUtil mSettingsUtil;

    public ChreCrossValidatorWwan(
            ContextHubManager contextHubManager,
            ContextHubInfo contextHubInfo,
            NanoAppBinary nanoAppBinary) {
        super(contextHubManager, contextHubInfo, nanoAppBinary);
        Assert.assertTrue(
                "Nanoapp given to cross validator is not the designated chre cross"
                        + " validation nanoapp.",
                nanoAppBinary.getNanoAppId() == NANOAPP_ID);

        Context context =
                androidx.test.platform.app.InstrumentationRegistry.getInstrumentation()
                        .getTargetContext();
        mSettingsUtil = new SettingsUtil(context);
        mTelephonyManager = context.getSystemService(TelephonyManager.class);
    }

    @Override
    public void init() throws AssertionError {
        super.init();
        mInitialAirplaneMode = mSettingsUtil.isAirplaneModeOn();
        try {
            mSettingsUtil.setAirplaneMode(false);
        } catch (InterruptedException e) {
            throw new AssertionError("Could not set airplane mode to false.", e);
        }
    }

    @Override
    public void validate() throws AssertionError, InterruptedException {
        mCollectingData.set(true);
        requestCapabilities();
        waitForCapabilitesFromNanoapp();
        mCollectingData.set(false);
        verifyCapabilities();

        requestCellInfoRefresh();
        waitForCellInfoForAp();

        mCollectingData.set(true);
        requestChreCellInfo();
        waitForCellInfoResultFromNanoapp();
        mCollectingData.set(false);

        verifyResult();
    }

    @Override
    public void deinit() throws AssertionError {
        super.deinit();
        try {
            mSettingsUtil.setAirplaneMode(mInitialAirplaneMode);
        } catch (InterruptedException e) {
            throw new AssertionError("Failed to restore initial airplane mode state.", e);
        }
    }

    private void requestCapabilities() {
        createAndSendMessage(ChreCrossValidationWwan.MessageType.WWAN_CAPABILITIES_REQUEST_VALUE);
    }

    private void requestChreCellInfo() {
        createAndSendMessage(ChreCrossValidationWwan.MessageType.WWAN_CELL_INFO_REQUEST_VALUE);
    }

    private void createAndSendMessage(int type) {
        NanoAppMessage message =
                NanoAppMessage.createMessageToNanoApp(
                        mNappBinary.getNanoAppId(), type, new byte[0]);
        int result = mContextHubClient.sendMessageToNanoApp(message);
        if (result != ContextHubTransaction.RESULT_SUCCESS) {
            Assert.fail("Failed to create and send WWAN message");
        }
    }

    private void waitForCapabilitesFromNanoapp() throws InterruptedException {
        boolean success = mAwaitDataLatch.await(AWAIT_RESULT_MESSAGE_TIMEOUT_SEC, TimeUnit.SECONDS);
        Assert.assertTrue("Timeout waiting for signal: capabilites message from nanoapp", success);
        mAwaitDataLatch = new CountDownLatch(1);
        Assert.assertTrue(
                "Timed out for capabilites message from nanoapp", mReceivedCapabilites.get());
        if (mErrorStr.get() != null) {
            Assert.fail(mErrorStr.get());
        }
    }

    private void waitForCellInfoResultFromNanoapp() throws InterruptedException {
        boolean success = mAwaitDataLatch.await(AWAIT_RESULT_MESSAGE_TIMEOUT_SEC, TimeUnit.SECONDS);
        Assert.assertTrue("Timeout waiting for signal: cell info message from nanoapp", success);
        mAwaitDataLatch = new CountDownLatch(1);
        Assert.assertTrue(
                "Timed out for cell info message from nanoapp", mReceivedCellInfoResults.get());
        if (mErrorStr.get() != null) {
            Assert.fail(mErrorStr.get());
        }
    }

    private void waitForCellInfoForAp() throws InterruptedException {
        CellInfoOrError result =
                mApCellInfoQueue.poll(AWAIT_CELL_INFO_FOR_AP_TIMEOUT_SEC, TimeUnit.SECONDS);
        Assert.assertNotNull("Timed out for cell info for AP", result);

        if (result.getErrorCode() != 0 || result.getErrorDetail() != null) {
            Log.e(TAG, "AP requestCellInfoUpdate failed with detail="
                    + result.getErrorDetail().getMessage());
            Assert.fail("AP requestCellInfoUpdate failed with errorCode=" + result.getErrorCode());
        }

        mApCellInfo = result.getCellInfo();
    }

    private boolean chreWwanHasBasicCapabilities() {
        return ((mWwanCapabilities.get().getWwanCapabilities() & WWAN_CAPABILITIES_GET_CELL_INFO)
                != 0);
    }

    private boolean chreWwanHasNeighborCapabilities() {
        return ((mWwanCapabilities.get().getWwanCapabilities()
                        & WWAN_CAPABILITIES_GET_CELL_NEIGHBOR_INFO)
                != 0);
    }

    private void verifyCapabilities() {
        Assume.assumeTrue(
                "CHRE WWAN capabilites are insufficient. Skipping test.",
                chreWwanHasBasicCapabilities());

        Log.i(
                TAG,
                "CHRE WWAN provides neighbor info ="
                        + String.valueOf(chreWwanHasNeighborCapabilities()));
    }

    private void verifyResult() {
        verifyCellInfoCount();
        verifyCellInfoContents();
    }

    void verifyCellInfoCount() {
        ChreCrossValidationWwan.WwanCellInfoResult chreCellInfoResult = mChreCellInfoResult.get();
        List<CellInfo> apCellInfoList = mApCellInfo;

        Log.i(TAG, "AP cell_info result count= " + apCellInfoList.size());

        int chreResultCount = chreCellInfoResult.getCellInfoCount();
        Log.i(TAG, "CHRE result count=" + chreResultCount);

        if (chreCellInfoResult.getErrorCode() != 0) {
            Assert.fail("CHRE WWAN results had error=" + chreCellInfoResult.getErrorCode());
        }

        int expectedResults = apCellInfoList.size();
        if (!chreWwanHasNeighborCapabilities()) {
            Log.i(TAG, "CHRE WWAN does not support neighbor info. Limiting count.");
            // Compare simple count if neighbors are not supported
            expectedResults = Math.min(expectedResults, 1);
        }

        Assert.assertEquals(
                "Unexpected result count from CHRE cell info. Expected="
                        + expectedResults
                        + " Actual="
                        + chreResultCount,
                expectedResults,
                chreResultCount);
    }

    void verifyCellInfoContents() {
        List<CellInfo> apCellInfoList = mApCellInfo;
        // Match all CHRE cell infos to AP cell infos
        // AP cell info entries are removed from the list as they are matched.
        for (ChreCrossValidationWwan.WwanCellInfo cCi :
                mChreCellInfoResult.get().getCellInfoList()) {
            switch (cCi.getCellInfoType()) {
                case ChreCrossValidationWwan.WwanCellInfoType.WWAN_CELL_INFO_TYPE_NR:
                    Assert.assertTrue(
                            "Could not find matching Nr CellInfo",
                            matchAndRemoveCellInfoNr(cCi, apCellInfoList));
                    break;
                case ChreCrossValidationWwan.WwanCellInfoType.WWAN_CELL_INFO_TYPE_LTE:
                    Assert.assertTrue(
                            "Could not find matching Lte CellInfo",
                            matchAndRemoveCellInfoLte(cCi, apCellInfoList));
                    break;
                case ChreCrossValidationWwan.WwanCellInfoType.WWAN_CELL_INFO_TYPE_GSM:
                    Assert.assertTrue(
                            "Could not find matching Gsm CellInfo",
                            matchAndRemoveCellInfoGsm(cCi, apCellInfoList));
                    break;
                case ChreCrossValidationWwan.WwanCellInfoType.WWAN_CELL_INFO_TYPE_WCDMA:
                    Assert.assertTrue(
                            "Could not find matching Wcdma CellInfo",
                            matchAndRemoveCellInfoWcdma(cCi, apCellInfoList));
                    break;
                default:
                    Assert.fail(
                            "Can't match CHRE cell info of unknown type: "
                                    + cCi.getCellInfoType().name());
            }
        }
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
                setErrorStr("Received message with unexpected type: " + message.getMessageType());
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
            capabilities =
                    ChreCrossValidationWwan.WwanCapabilities.parseFrom(message.getMessageBody());
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
            result = ChreCrossValidationWwan.WwanCellInfoResult.parseFrom(message.getMessageBody());
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

    String chreCellInfoHeaderToString(ChreCrossValidationWwan.WwanCellInfo cellInfo) {
        String value = "CellInfoType=" + cellInfo.getCellInfoType().name();
        value += ", TimestampNs=" + cellInfo.getTimestampNs();
        value += ", TimestampType=" + cellInfo.getTimestampType().name();
        value += ", IsRegistered=" + cellInfo.getIsRegistered();
        value += ", ";
        return value;
    }

    String chreCellInfoNrToString(ChreCrossValidationWwan.WwanCellInfo cellInfo) {
        ChreCrossValidationWwan.CellInfoNr cellInfoNr = cellInfo.getNr();
        ChreCrossValidationWwan.CellIdentityNr cellIdentityNr = cellInfoNr.getCellIdentity();
        ChreCrossValidationWwan.SignalStrengthNr signalStrengthNr = cellInfoNr.getSignalStrength();
        String value = chreCellInfoHeaderToString(cellInfo);
        value += "Mcc=" + cellIdentityNr.getMcc();
        value += ", Mnc=" + cellIdentityNr.getMnc();
        value += ", Nci=" + cellIdentityNr.getNci();
        value += ", Pci=" + cellIdentityNr.getPci();
        value += ", Tac=" + cellIdentityNr.getTac();
        value += ", NrArfcn=" + cellIdentityNr.getNrarfcn();
        value += ", SsRsrp=" + signalStrengthNr.getSsRsrp();
        value += ", SsRsrq=" + signalStrengthNr.getSsRsrq();
        value += ", SsSinr=" + signalStrengthNr.getSsSinr();
        value += ", CsiRsrp=" + signalStrengthNr.getCsiRsrp();
        value += ", CsiRsrq=" + signalStrengthNr.getCsiRsrq();
        value += ", CsiSinr=" + signalStrengthNr.getCsiSinr();
        return value;
    }

    String chreCellInfoLteToString(ChreCrossValidationWwan.WwanCellInfo cellInfo) {
        ChreCrossValidationWwan.CellInfoLte cellInfoLte = cellInfo.getLte();
        ChreCrossValidationWwan.CellIdentityLte cellIdentityLte = cellInfoLte.getCellIdentity();
        ChreCrossValidationWwan.SignalStrengthLte signalStrengthLte =
                cellInfoLte.getSignalStrength();
        String value = chreCellInfoHeaderToString(cellInfo);
        value += "Mcc=" + cellIdentityLte.getMcc();
        value += ", Mnc=" + cellIdentityLte.getMnc();
        value += ", Ci=" + cellIdentityLte.getCi();
        value += ", Pci=" + cellIdentityLte.getPci();
        value += ", Tac=" + cellIdentityLte.getTac();
        value += ", Earfcn=" + cellIdentityLte.getEarfcn();
        value += ", Signal_strength=" + signalStrengthLte.getSignalStrength();
        value += ", Rsrp=" + signalStrengthLte.getRsrp();
        value += ", Rsrq=" + signalStrengthLte.getRsrq();
        value += ", Rssnr=" + signalStrengthLte.getRssnr();
        value += ", Cqi=" + signalStrengthLte.getCqi();
        value += ", TimingAdvance=" + signalStrengthLte.getTimingAdvance();
        return value;
    }

    String chreCellInfoGsmToString(ChreCrossValidationWwan.WwanCellInfo cellInfo) {
        ChreCrossValidationWwan.CellInfoGsm cellInfoGsm = cellInfo.getGsm();
        ChreCrossValidationWwan.CellIdentityGsm cellIdentityGsm = cellInfoGsm.getCellIdentity();
        ChreCrossValidationWwan.SignalStrengthGsm signalStrengthGsm =
                cellInfoGsm.getSignalStrength();
        String value = chreCellInfoHeaderToString(cellInfo);
        value += "Mcc=" + cellIdentityGsm.getMcc();
        value += ", Mnc=" + cellIdentityGsm.getMnc();
        value += ", Lac=" + cellIdentityGsm.getLac();
        value += ", Cid=" + cellIdentityGsm.getCid();
        value += ", Arfcn=" + cellIdentityGsm.getArfcn();
        value += ", Bsic=" + cellIdentityGsm.getBsic();
        value += ", Signal_strength=" + signalStrengthGsm.getSignalStrength();
        value += ", Bit_error_rate=" + signalStrengthGsm.getBitErrorRate();
        value += ", Timing_advance=" + signalStrengthGsm.getTimingAdvance();
        return value;
    }

    String chreCellInfoWcdmaToString(ChreCrossValidationWwan.WwanCellInfo cellInfo) {
        ChreCrossValidationWwan.CellInfoWcdma cellInfoWcdma = cellInfo.getWcdma();
        ChreCrossValidationWwan.CellIdentityWcdma cellIdentityWcdma =
                cellInfoWcdma.getCellIdentity();
        ChreCrossValidationWwan.SignalStrengthWcdma signalStrengthWcdma =
                cellInfoWcdma.getSignalStrength();
        String value = chreCellInfoHeaderToString(cellInfo);
        value += "Mcc=" + cellIdentityWcdma.getMcc();
        value += ", Mnc=" + cellIdentityWcdma.getMnc();
        value += ", Lac=" + cellIdentityWcdma.getLac();
        value += ", Cid=" + cellIdentityWcdma.getCid();
        value += ", Psc=" + cellIdentityWcdma.getPsc();
        value += ", Uarfcn=" + cellIdentityWcdma.getUarfcn();
        value += ", Signal_strength=" + signalStrengthWcdma.getSignalStrength();
        value += ", Bit_error_rate=" + signalStrengthWcdma.getBitErrorRate();
        return value;
    }

    boolean matchAndRemoveCellInfoNr(
            ChreCrossValidationWwan.WwanCellInfo chreCellInfo, List<CellInfo> apCellInfoList) {
        Log.i(TAG, "CHRE cell info: " + chreCellInfoNrToString(chreCellInfo));
        Iterator<CellInfo> apCellInfoIterator = apCellInfoList.iterator();
        while (apCellInfoIterator.hasNext()) {
            CellInfo apCi = apCellInfoIterator.next();
            if (apCi instanceof CellInfoNr) {
                CellInfoNr apCellInfoNr = (CellInfoNr) apCi;
                if (compareCellIdentityNr(chreCellInfo, apCellInfoNr)
                        && compareSignalStrengthNr(
                                chreCellInfo.getNr().getSignalStrength(),
                                (CellSignalStrengthNr) apCellInfoNr.getCellSignalStrength())) {
                    apCellInfoIterator.remove();
                    return true;
                }
            }
        }
        return false;
    }

    boolean matchAndRemoveCellInfoLte(
            ChreCrossValidationWwan.WwanCellInfo chreCellInfo, List<CellInfo> apCellInfoList) {
        Log.i(TAG, "CHRE cell info: " + chreCellInfoLteToString(chreCellInfo));
        Iterator<CellInfo> apCellInfoIterator = apCellInfoList.iterator();
        while (apCellInfoIterator.hasNext()) {
            CellInfo apCi = apCellInfoIterator.next();
            if (apCi instanceof CellInfoLte) {
                CellInfoLte apCellInfoLte = (CellInfoLte) apCi;
                if (compareCellIdentityLte(chreCellInfo, apCellInfoLte)
                        && compareSignalStrengthLte(
                                chreCellInfo.getLte().getSignalStrength(),
                                (CellSignalStrengthLte) apCellInfoLte.getCellSignalStrength())) {
                    apCellInfoIterator.remove();
                    return true;
                }
            }
        }
        return false;
    }

    boolean matchAndRemoveCellInfoGsm(
            ChreCrossValidationWwan.WwanCellInfo chreCellInfo, List<CellInfo> apCellInfoList) {
        Log.i(TAG, "CHRE cell info: " + chreCellInfoGsmToString(chreCellInfo));
        Iterator<CellInfo> apCellInfoIterator = apCellInfoList.iterator();
        while (apCellInfoIterator.hasNext()) {
            CellInfo apCi = apCellInfoIterator.next();
            if (apCi instanceof CellInfoGsm) {
                CellInfoGsm apCellInfoGsm = (CellInfoGsm) apCi;
                if (compareCellIdentityGsm(chreCellInfo, apCellInfoGsm)
                        && compareSignalStrengthGsm(
                                chreCellInfo.getGsm().getSignalStrength(),
                                (CellSignalStrengthGsm) apCellInfoGsm.getCellSignalStrength())) {
                    apCellInfoIterator.remove();
                    return true;
                }
            }
        }
        return false;
    }

    boolean matchAndRemoveCellInfoWcdma(
            ChreCrossValidationWwan.WwanCellInfo chreCellInfo, List<CellInfo> apCellInfoList) {
        Log.i(TAG, "CHRE cell info: " + chreCellInfoWcdmaToString(chreCellInfo));
        Iterator<CellInfo> apCellInfoIterator = apCellInfoList.iterator();
        while (apCellInfoIterator.hasNext()) {
            CellInfo apCi = apCellInfoIterator.next();
            if (apCi instanceof CellInfoWcdma) {
                CellInfoWcdma apCellInfoWcdma = (CellInfoWcdma) apCi;
                if (compareCellIdentityWcdma(chreCellInfo, apCellInfoWcdma)
                        && compareSignalStrengthWcdma(
                                chreCellInfo.getWcdma().getSignalStrength(),
                                (CellSignalStrengthWcdma)
                                        apCellInfoWcdma.getCellSignalStrength())) {
                    apCellInfoIterator.remove();
                    return true;
                }
            }
        }
        return false;
    }

    boolean compareCellIdentityNr(
            ChreCrossValidationWwan.WwanCellInfo chreCellInfoNr, CellInfoNr apCellInfoNr) {

        CellIdentityNr apCellIdentityNr = (CellIdentityNr) apCellInfoNr.getCellIdentity();
        ChreCrossValidationWwan.CellIdentityNr chreCellIdentityNr =
                chreCellInfoNr.getNr().getCellIdentity();

        // pci, nrarfcn, and is_registered must always be valid
        if (chreCellIdentityNr.getPci() != apCellIdentityNr.getPci()
                || chreCellIdentityNr.getNrarfcn() != apCellIdentityNr.getNrarfcn()
                || chreCellInfoNr.getIsRegistered() != apCellInfoNr.isRegistered()) {
            return false;
        }

        // mcc, mnc, nci must be valid for registered cells
        if (apCellInfoNr.isRegistered()) {
            if (chreCellIdentityNr.getMcc() != Integer.parseInt(apCellIdentityNr.getMccString())
                    || chreCellIdentityNr.getMnc()
                            != Integer.parseInt(apCellIdentityNr.getMncString())
                    || chreCellIdentityNr.getNci() != apCellIdentityNr.getNci()) {
                return false;
            }
        }

        return true;
    }

    boolean compareCellIdentityLte(
            ChreCrossValidationWwan.WwanCellInfo chreCellInfoLte, CellInfoLte apCellInfoLte) {
        CellIdentityLte apCellIdentityLte = (CellIdentityLte) apCellInfoLte.getCellIdentity();
        ChreCrossValidationWwan.CellIdentityLte chreCellIdentityLte =
                chreCellInfoLte.getLte().getCellIdentity();

        if (chreCellInfoLte.getIsRegistered() != apCellInfoLte.isRegistered()
                || chreCellIdentityLte.getMcc()
                        != Integer.parseInt(apCellIdentityLte.getMccString())
                || chreCellIdentityLte.getMnc()
                        != Integer.parseInt(apCellIdentityLte.getMncString())
                || chreCellIdentityLte.getCi() != apCellIdentityLte.getCi()
                || chreCellIdentityLte.getPci() != apCellIdentityLte.getPci()
                || chreCellIdentityLte.getTac() != apCellIdentityLte.getTac()
                || chreCellIdentityLte.getEarfcn() != apCellIdentityLte.getEarfcn()) {
            return false;
        }

        return true;
    }

    boolean compareCellIdentityGsm(
            ChreCrossValidationWwan.WwanCellInfo chreCellInfoGsm, CellInfoGsm apCellInfoGsm) {
        CellIdentityGsm apCellIdentityGsm = (CellIdentityGsm) apCellInfoGsm.getCellIdentity();
        ChreCrossValidationWwan.CellIdentityGsm chreCellIdentityGsm =
                chreCellInfoGsm.getGsm().getCellIdentity();
        if (chreCellInfoGsm.getIsRegistered() != apCellInfoGsm.isRegistered()
                || chreCellIdentityGsm.getMcc()
                        != Integer.parseInt(apCellIdentityGsm.getMccString())
                || chreCellIdentityGsm.getMnc()
                        != Integer.parseInt(apCellIdentityGsm.getMncString())
                || chreCellIdentityGsm.getLac() != apCellIdentityGsm.getLac()
                || chreCellIdentityGsm.getCid() != apCellIdentityGsm.getCid()
                || chreCellIdentityGsm.getArfcn() != apCellIdentityGsm.getArfcn()
                || chreCellIdentityGsm.getBsic() != apCellIdentityGsm.getBsic()) {
            return false;
        }

        return true;
    }

    boolean compareCellIdentityWcdma(
            ChreCrossValidationWwan.WwanCellInfo chreCellInfoWcdma, CellInfoWcdma apCellInfoWcdma) {
        CellIdentityWcdma apCellIdentityWcdma =
                (CellIdentityWcdma) apCellInfoWcdma.getCellIdentity();
        ChreCrossValidationWwan.CellIdentityWcdma chreCellIdentityWcdma =
                chreCellInfoWcdma.getWcdma().getCellIdentity();
        if (chreCellInfoWcdma.getIsRegistered() != apCellInfoWcdma.isRegistered()
                || chreCellIdentityWcdma.getMcc()
                        != Integer.parseInt(apCellIdentityWcdma.getMccString())
                || chreCellIdentityWcdma.getMnc()
                        != Integer.parseInt(apCellIdentityWcdma.getMncString())
                || chreCellIdentityWcdma.getLac() != apCellIdentityWcdma.getLac()
                || chreCellIdentityWcdma.getCid() != apCellIdentityWcdma.getCid()
                || chreCellIdentityWcdma.getPsc() != apCellIdentityWcdma.getPsc()
                || chreCellIdentityWcdma.getUarfcn() != apCellIdentityWcdma.getUarfcn()) {
            return false;
        }

        return true;
    }

    boolean compareSignalStrengthCommon(int chreStrength, int apStrength, int tolerance) {
        // Check that the difference is within the expected range.
        int signalDifference = chreStrength - apStrength;

        if (Math.abs(signalDifference) > tolerance) {
            Log.e(
                    TAG,
                    "Mismatch in signal strength. AP="
                            + apStrength
                            + " CHRE="
                            + chreStrength
                            + " Difference="
                            + signalDifference
                            + " Tolerance="
                            + tolerance);
            return false;
        }

        return true;
    }

    boolean compareSignalStrengthNr(
            ChreCrossValidationWwan.SignalStrengthNr chreSignalStrengthNr,
            CellSignalStrengthNr apSignalStrengthNr) {
        // Note that the CHRE value is inverted by default
        int chreSsRsrp = (-1 * chreSignalStrengthNr.getSsRsrp());
        int apSsRsrp = apSignalStrengthNr.getSsRsrp();
        return compareSignalStrengthCommon(chreSsRsrp, apSsRsrp, SIGNAL_STRENGTH_TOLERANCE_DBM);
    }

    boolean compareSignalStrengthLte(
            ChreCrossValidationWwan.SignalStrengthLte chreSignalStrengthLte,
            CellSignalStrengthLte apSignalStrengthLte) {
        // Note that the CHRE value is inverted by default
        int chreRsrp = (-1 * chreSignalStrengthLte.getRsrp());
        int apRsrp = apSignalStrengthLte.getRsrp();
        return compareSignalStrengthCommon(chreRsrp, apRsrp, SIGNAL_STRENGTH_TOLERANCE_DBM);
    }

    boolean compareSignalStrengthGsm(
            ChreCrossValidationWwan.SignalStrengthGsm chreSignalStrengthGsm,
            CellSignalStrengthGsm apSignalStrengthGsm) {
        // CHRE signal_strength and AP rssi are not directly comparable
        int chreAsu = chreSignalStrengthGsm.getSignalStrength();
        int apAsu = apSignalStrengthGsm.getAsuLevel();
        return compareSignalStrengthCommon(chreAsu, apAsu, SIGNAL_STRENGTH_TOLERANCE_ASU);
    }

    boolean compareSignalStrengthWcdma(
            ChreCrossValidationWwan.SignalStrengthWcdma chreSignalStrengthWcdma,
            CellSignalStrengthWcdma apSignalStrengthWcdma) {
        // CHRE signal_strength and AP rssi are not directly comparable
        int chreAsu = chreSignalStrengthWcdma.getSignalStrength();
        int apAsu = apSignalStrengthWcdma.getAsuLevel();
        return compareSignalStrengthCommon(chreAsu, apAsu, SIGNAL_STRENGTH_TOLERANCE_ASU);
    }

    class CellInfoOrError {
        private List<CellInfo> mCellInfo;
        private int mErrorCode;
        private Throwable mDetail;

        CellInfoOrError(List<CellInfo> cellInfo) {
            this.mCellInfo = cellInfo;
            this.mErrorCode = 0;
            this.mDetail = null;
        }

        CellInfoOrError(int errorCode, Throwable detail) {
            this.mCellInfo = null;
            this.mErrorCode = errorCode;
            this.mDetail = detail;
        }

        public List<CellInfo> getCellInfo() {
            return mCellInfo;
        }

        public int getErrorCode() {
            return mErrorCode;
        }

        public Throwable getErrorDetail() {
            return mDetail;
        }
    };

    void requestCellInfoRefresh() {
        CellInfoCallback callback =
                new CellInfoCallback() {
                    @Override
                    public synchronized void onCellInfo(List<CellInfo> cellInfo) {
                        Log.i(TAG, "Received cell info for AP. onCellInfo invoked");
                        for (CellInfo apCi : cellInfo) {
                            Log.i(TAG, "AP cell info: " + apCi.toString());
                        }
                        mApCellInfoQueue.add(new CellInfoOrError(cellInfo));
                    }

                    @Override
                    public synchronized void onError(int errorCode, Throwable detail) {
                        mApCellInfoQueue.add(new CellInfoOrError(errorCode, detail));
                    }
                };

        mTelephonyManager.requestCellInfoUpdate(this, callback);
    }

    /** Run the given runnable. Required to support requestCellInfoUpdate */
    public void execute(Runnable r) {
        r.run();
    }

    // Unused. Required to extend ChreCrossValidatorBase.
    @Override
    protected void unregisterApDataListener() {}
}
