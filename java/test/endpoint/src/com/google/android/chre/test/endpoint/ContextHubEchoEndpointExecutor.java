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
package com.google.android.chre.test.endpoint;

import android.content.Context;
import android.hardware.contexthub.HubDiscoveryInfo;
import android.hardware.contexthub.HubEndpoint;
import android.hardware.contexthub.HubEndpointDiscoveryCallback;
import android.hardware.contexthub.HubEndpointInfo;
import android.hardware.contexthub.HubEndpointLifecycleCallback;
import android.hardware.contexthub.HubEndpointMessageCallback;
import android.hardware.contexthub.HubEndpointSession;
import android.hardware.contexthub.HubEndpointSessionResult;
import android.hardware.contexthub.HubMessage;
import android.hardware.contexthub.HubServiceInfo;
import android.hardware.location.ContextHubInfo;
import android.hardware.location.ContextHubManager;
import android.hardware.location.ContextHubTransaction;
import android.hardware.location.NanoAppBinary;
import android.hardware.location.NanoAppState;
import android.util.Log;
import android.util.Pair;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.test.InstrumentationRegistry;

import com.google.android.utils.chre.ChreTestUtil;

import org.junit.Assert;
import org.junit.Assume;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.Executor;
import java.util.concurrent.ScheduledThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.function.Consumer;

/**
 * A test to validate endpoint connection and messaging with an service on the device. The device
 * tested in this class is expected to register a test echo service, which must behave as a loopback
 * service which echoes back a message sent to it with identical payload.
 */
public class ContextHubEchoEndpointExecutor {
    private static final String TAG = "ContextHubEchoEndpointExecutor";

    /** The service descriptor for an echo service. */
    private static final String ECHO_SERVICE_DESCRIPTOR =
            "android.hardware.contexthub.test.EchoService";

    private static final int ECHO_SERVICE_MAJOR_VERSION = 1;
    private static final int ECHO_SERVICE_MINOR_VERSION = 0;

    private static final int TIMEOUT_MESSAGE_SECONDS = 5;

    private static final int TIMEOUT_SESSION_OPEN_SECONDS = 5;
    private static final int TIMEOUT_DISCOVERY_SECONDS = 5;

    @NonNull private final ContextHubManager mContextHubManager;
    @Nullable private final ContextHubInfo mContextHubInfo;

    /** The nanoapp binary which publishes a test echo service */
    @Nullable private final NanoAppBinary mEchoServiceNanoappBinary;

    /** The ID of the above nanoapp */
    private static final long ECHO_SERVICE_NANOAPP_ID = 0x476f6f6754fffffbL;

    /** The nanoapp binary which connects to a host-side test echo service */
    @Nullable private final NanoAppBinary mEchoClientNanoappBinary;

    /** The ID of the above nanoapp */
    private static final long ECHO_CLIENT_NANOAPP_ID = 0x476f6f6754000012L;

    /** A local hub endpoint currently registered with the service. */
    private HubEndpoint mRegisteredEndpoint = null;

    static class TestLifecycleCallback implements HubEndpointLifecycleCallback {
        TestLifecycleCallback() {
            this(/* acceptSession= */ false);
        }

        TestLifecycleCallback(boolean acceptSession) {
            mAcceptSession = acceptSession;
        }

        @Override
        public HubEndpointSessionResult onSessionOpenRequest(
                HubEndpointInfo requester, String serviceDescriptor) {
            Log.d(TAG, "onSessionOpenRequest");
            HubEndpointSessionResult result =
                    mAcceptSession
                            ? HubEndpointSessionResult.accept()
                            : HubEndpointSessionResult.reject("Unexpected request");
            mSessionRequestQueue.add(result);
            return result;
        }

        @Override
        public void onSessionOpened(HubEndpointSession session) {
            Log.d(TAG, "onSessionOpened: session=" + session);
            mSessionQueue.add(session);
        }

        @Override
        public void onSessionClosed(HubEndpointSession session, int reason) {
            Log.d(TAG, "onSessionClosed: session=" + session);
            mSessionCloseQueue.add(Pair.create(session, reason));
        }

        public HubEndpointSession waitForEndpointSession() throws InterruptedException {
            return mSessionQueue.poll(TIMEOUT_SESSION_OPEN_SECONDS, TimeUnit.SECONDS);
        }

        public HubEndpointSessionResult waitForOpenSessionRequest() throws InterruptedException {
            return mSessionRequestQueue.poll(TIMEOUT_SESSION_OPEN_SECONDS, TimeUnit.SECONDS);
        }

        public Pair<HubEndpointSession, Integer> waitForCloseSession() throws InterruptedException {
            return mSessionCloseQueue.poll(TIMEOUT_SESSION_OPEN_SECONDS, TimeUnit.SECONDS);
        }

        /** If true, accepts incoming sessions */
        private final boolean mAcceptSession;

        private final BlockingQueue<HubEndpointSession> mSessionQueue = new ArrayBlockingQueue<>(1);
        private final BlockingQueue<HubEndpointSessionResult> mSessionRequestQueue =
                new ArrayBlockingQueue<>(1);
        private final BlockingQueue<Pair<HubEndpointSession, Integer>> mSessionCloseQueue =
                new ArrayBlockingQueue<>(1);
    }

    static class TestMessageCallback implements HubEndpointMessageCallback {
        @Override
        public void onMessageReceived(HubEndpointSession session, HubMessage message) {
            Log.d(TAG, "onMessageReceived: session=" + session + ", message=" + message);
            mMessageQueue.add(message);
        }

        public HubMessage waitForMessage() throws InterruptedException {
            return mMessageQueue.poll(TIMEOUT_MESSAGE_SECONDS, TimeUnit.SECONDS);
        }

        private BlockingQueue<HubMessage> mMessageQueue = new ArrayBlockingQueue<>(1);
    }

    static class TestDiscoveryCallback implements HubEndpointDiscoveryCallback {
        @Override
        public void onEndpointsStarted(@NonNull List<HubDiscoveryInfo> discoveryInfoList) {
            Log.d(TAG, "onEndpointsStarted: discovery size=" + discoveryInfoList.size());
            mEndpointStartedQueue.add(discoveryInfoList);
        }

        @Override
        public void onEndpointsStopped(
                @NonNull List<HubDiscoveryInfo> discoveryInfoList, int reason) {
            Log.d(
                    TAG,
                    "onEndpointsStarted: discovery size="
                            + discoveryInfoList.size()
                            + ", reason="
                            + reason);
            mEndpointStoppedQueue.add(Pair.create(discoveryInfoList, reason));
        }

        public List<HubDiscoveryInfo> waitForStarted() throws InterruptedException {
            return mEndpointStartedQueue.poll(TIMEOUT_DISCOVERY_SECONDS, TimeUnit.SECONDS);
        }

        public Pair<List<HubDiscoveryInfo>, Integer> waitForStopped() throws InterruptedException {
            return mEndpointStoppedQueue.poll(TIMEOUT_DISCOVERY_SECONDS, TimeUnit.SECONDS);
        }

        private BlockingQueue<List<HubDiscoveryInfo>> mEndpointStartedQueue =
                new ArrayBlockingQueue<>(1);
        private BlockingQueue<Pair<List<HubDiscoveryInfo>, Integer>> mEndpointStoppedQueue =
                new ArrayBlockingQueue<>(1);
    }

    public ContextHubEchoEndpointExecutor(ContextHubManager manager) {
        this(
                manager,
                /* info= */ null,
                /* echoServiceNanoappBinary= */ null,
                /* echoClientNanoappBinary= */ null);
    }

    public ContextHubEchoEndpointExecutor(
            ContextHubManager manager,
            ContextHubInfo info,
            NanoAppBinary echoServiceNanoappBinary,
            NanoAppBinary echoClientNanoappBinary) {
        if (echoServiceNanoappBinary != null) {
            Assert.assertEquals(echoServiceNanoappBinary.getNanoAppId(), ECHO_SERVICE_NANOAPP_ID);
        }
        if (echoClientNanoappBinary != null) {
            Assert.assertEquals(echoServiceNanoappBinary.getNanoAppId(), ECHO_CLIENT_NANOAPP_ID);
        }
        mContextHubManager = manager;
        mContextHubInfo = info;
        mEchoServiceNanoappBinary = echoServiceNanoappBinary;
        mEchoClientNanoappBinary = echoClientNanoappBinary;
    }

    /** Deinitialization code that should be called in e.g. @After. */
    public void deinit() {
        if (mRegisteredEndpoint != null) {
            unregisterRegisteredEndpointNoThrow();
        }
        if (mContextHubInfo != null && mEchoServiceNanoappBinary != null) {
            List<NanoAppState> stateList =
                    ChreTestUtil.queryNanoAppsAssertSuccess(mContextHubManager, mContextHubInfo);
            for (NanoAppState state : stateList) {
                if (state.getNanoAppId() == ECHO_SERVICE_NANOAPP_ID) {
                    ChreTestUtil.unloadNanoAppAssertSuccess(
                            mContextHubManager, mContextHubInfo, state.getNanoAppId());
                }
            }
        }
    }

    /**
     * Checks to see if an echo service exists on the device, and validates the endpoint discovery
     * info contents.
     *
     * @return The list of hub discovery info which contains the echo service.
     */
    public List<HubDiscoveryInfo> getEchoServiceList() {
        List<HubDiscoveryInfo> infoList = new ArrayList<>();
        checkApiSupport(
                (manager) -> infoList.addAll(manager.findEndpoints(ECHO_SERVICE_DESCRIPTOR)));
        for (HubDiscoveryInfo info : infoList) {
            printHubDiscoveryInfo(info);
            HubEndpointInfo endpointInfo = info.getHubEndpointInfo();
            Assert.assertNotNull(endpointInfo);
            // The first valid endpoint info type is 1
            Assert.assertNotEquals(endpointInfo.getType(), 0);
            HubEndpointInfo.HubEndpointIdentifier identifier = endpointInfo.getIdentifier();
            Assert.assertNotNull(identifier);

            HubServiceInfo serviceInfo = info.getHubServiceInfo();
            Assert.assertNotNull(serviceInfo);
            Assert.assertEquals(ECHO_SERVICE_DESCRIPTOR, serviceInfo.getServiceDescriptor());

            List<HubDiscoveryInfo> identifierDiscoveryList = new ArrayList<>();
            checkApiSupport(
                    (manager) ->
                            identifierDiscoveryList.addAll(
                                    manager.findEndpoints(ECHO_SERVICE_DESCRIPTOR)));
            Assert.assertNotEquals(identifierDiscoveryList.size(), 0);
        }
        return infoList;
    }

    /** Validates that a local endpoint can be registered/unregistered. */
    public void testDefaultEndpointRegistration() throws Exception {
        mRegisteredEndpoint = registerDefaultEndpoint();
        unregisterRegisteredEndpoint();
    }

    /**
     * Creates a local endpoint and validates that a session can be opened with the echo service
     * endpoint.
     */
    public void testOpenEndpointSession() throws Exception {
        List<HubDiscoveryInfo> infoList = getEchoServiceList();
        for (HubDiscoveryInfo info : infoList) {
            HubEndpointInfo targetEndpointInfo = info.getHubEndpointInfo();
            Assert.assertNotNull(targetEndpointInfo);
            mRegisteredEndpoint = registerDefaultEndpoint();
            openSessionOrFailNoDescriptor(mRegisteredEndpoint, targetEndpointInfo);
            unregisterRegisteredEndpoint();
        }
    }

    /**
     * Creates a local endpoint and validates that a session can be opened with the echo service
     * endpoint, receives an onSessionOpened callback, and the session can be closed.
     */
    public void testOpenCloseEndpointSession() throws Exception {
        List<HubDiscoveryInfo> infoList = getEchoServiceList();
        for (HubDiscoveryInfo info : infoList) {
            HubEndpointInfo targetEndpointInfo = info.getHubEndpointInfo();

            TestLifecycleCallback callback = new TestLifecycleCallback();
            mRegisteredEndpoint = registerDefaultEndpoint(callback, null);
            openSessionOrFail(mRegisteredEndpoint, targetEndpointInfo);
            HubEndpointSession session = callback.waitForEndpointSession();
            Assert.assertEquals(session.getServiceDescriptor(), ECHO_SERVICE_DESCRIPTOR);
            Assert.assertNotNull(session);
            session.close();

            unregisterRegisteredEndpoint();
        }
    }

    public void testEndpointMessaging() throws Exception {
        doTestEndpointMessaging(/* executor= */ null);
    }

    public void testEndpointThreadedMessaging() throws Exception {
        ScheduledThreadPoolExecutor executor =
                new ScheduledThreadPoolExecutor(/* corePoolSize= */ 1);
        doTestEndpointMessaging(executor);
    }

    /**
     * Creates a local endpoint and validates that a session can be opened with the echo service
     * endpoint, receives an onSessionOpened callback, and confirms that a message can be echoed
     * through the service.
     *
     * @param executor An optional executor to invoke callbacks on.
     */
    private void doTestEndpointMessaging(@Nullable Executor executor) throws Exception {
        List<HubDiscoveryInfo> infoList = getEchoServiceList();
        for (HubDiscoveryInfo info : infoList) {
            HubEndpointInfo targetEndpointInfo = info.getHubEndpointInfo();

            TestLifecycleCallback callback = new TestLifecycleCallback();
            TestMessageCallback messageCallback = new TestMessageCallback();
            mRegisteredEndpoint =
                    (executor == null)
                            ? registerDefaultEndpoint(callback, messageCallback)
                            : registerDefaultEndpoint(callback, messageCallback, executor);
            openSessionOrFail(mRegisteredEndpoint, targetEndpointInfo);
            HubEndpointSession session = callback.waitForEndpointSession();
            Assert.assertNotNull(session);
            Assert.assertEquals(session.getServiceDescriptor(), ECHO_SERVICE_DESCRIPTOR);

            final int messageType = 1234;
            HubMessage message =
                    new HubMessage.Builder(
                            messageType,
                            new byte[] {1, 2, 3, 4, 5}).setResponseRequired(true).build();
            ContextHubTransaction<Void> txn = session.sendMessage(message);
            Assert.assertNotNull(txn);
            ContextHubTransaction.Response<Void> txnResponse =
                    txn.waitForResponse(TIMEOUT_MESSAGE_SECONDS, TimeUnit.SECONDS);
            Assert.assertNotNull(txnResponse);
            Assert.assertEquals(txnResponse.getResult(), ContextHubTransaction.RESULT_SUCCESS);
            HubMessage response = messageCallback.waitForMessage();
            Assert.assertNotNull(response);
            Assert.assertEquals(message.getMessageType(), response.getMessageType());
            Assert.assertTrue(
                    "Echo message unidentical. Expected: "
                            + Arrays.toString(message.getMessageBody())
                            + ", actual: "
                            + Arrays.toString(response.getMessageBody()),
                    Arrays.equals(message.getMessageBody(), response.getMessageBody()));
            session.close();

            unregisterRegisteredEndpoint();
        }
    }

    public void testEndpointDiscovery() throws Exception {
        doTestEndpointDiscovery(/* executor= */ null);
    }

    public void testThreadedEndpointDiscovery() throws Exception {
        ScheduledThreadPoolExecutor executor =
                new ScheduledThreadPoolExecutor(/* corePoolSize= */ 1);
        doTestEndpointDiscovery(executor);
    }

    /**
     * Registers an endpoint discovery callback for endpoints with the echo service descriptor.
     *
     * @param executor An optional executor to invoke callbacks on.
     */
    private void doTestEndpointDiscovery(@Nullable Executor executor) throws Exception {
        TestDiscoveryCallback callback = new TestDiscoveryCallback();
        if (executor != null) {
            checkApiSupport(
                    (manager) ->
                            manager.registerEndpointDiscoveryCallback(
                                    executor, callback, ECHO_SERVICE_DESCRIPTOR));
        } else {
            checkApiSupport(
                    (manager) ->
                            manager.registerEndpointDiscoveryCallback(
                                    callback, ECHO_SERVICE_DESCRIPTOR));
        }

        checkDynamicEndpointDiscovery(callback);
        checkApiSupport((manager) -> manager.unregisterEndpointDiscoveryCallback(callback));
    }

    public void testEndpointIdDiscovery() throws Exception {
        doTestEndpointIdDiscovery(/* executor= */ null);
    }

    public void testThreadedEndpointIdDiscovery() throws Exception {
        ScheduledThreadPoolExecutor executor =
                new ScheduledThreadPoolExecutor(/* corePoolSize= */ 1);
        doTestEndpointIdDiscovery(executor);
    }

    /**
     * Registers an endpoint discovery callback for endpoints with the echo message nanoapp ID.
     *
     * @param executor An optional executor to invoke callbacks on.
     */
    private void doTestEndpointIdDiscovery(@Nullable Executor executor) throws Exception {
        TestDiscoveryCallback callback = new TestDiscoveryCallback();
        if (executor != null) {
            checkApiSupport(
                    (manager) ->
                            manager.registerEndpointDiscoveryCallback(
                                    executor, callback, ECHO_SERVICE_NANOAPP_ID));
        } else {
            checkApiSupport(
                    (manager) ->
                            manager.registerEndpointDiscoveryCallback(
                                    callback, ECHO_SERVICE_NANOAPP_ID));
        }

        checkDynamicEndpointDiscovery(callback);
        checkApiSupport((manager) -> manager.unregisterEndpointDiscoveryCallback(callback));
    }

    /**
     * A test to see if a echo test service can be registered by the application.
     *
     * <p>For CHRE-capable devices, we will also confirm that a connection can be started from the
     * embedded client and echo works as intended. The echo client nanoapp is expected to open a
     * session with the host-side service when the nanoapp is loaded, and sends a message to echo
     * back to the nanoapp once the session is opened.
     */
    public void testApplicationEchoService() throws Exception {
        Collection<HubServiceInfo> serviceList = new ArrayList<>();
        HubServiceInfo.Builder builder =
                new HubServiceInfo.Builder(
                        ECHO_SERVICE_DESCRIPTOR,
                        HubServiceInfo.FORMAT_CUSTOM,
                        ECHO_SERVICE_MAJOR_VERSION,
                        ECHO_SERVICE_MINOR_VERSION);
        HubServiceInfo info = builder.build();
        Assert.assertNotNull(info);
        serviceList.add(info);

        TestLifecycleCallback callback = new TestLifecycleCallback(/* acceptSession= */ true);
        TestMessageCallback messageCallback = new TestMessageCallback();
        mRegisteredEndpoint =
                registerDefaultEndpoint(
                        callback, messageCallback, /* executor= */ null, serviceList);

        // TODO(b/385765805): Enable when ready
        boolean isDynamicLoadingSupported = false;
        if (isDynamicLoadingSupported
                && mContextHubInfo != null
                && mEchoClientNanoappBinary != null) {
            ChreTestUtil.loadNanoAppAssertSuccess(
                    mContextHubManager, mContextHubInfo, mEchoClientNanoappBinary);
            HubEndpointSessionResult result = callback.waitForOpenSessionRequest();
            Assert.assertNotNull(result);
            Assert.assertTrue(result.isAccepted());
            HubEndpointSession session = callback.waitForEndpointSession();
            Assert.assertNotNull(session);
            Log.d(TAG, "Session open: " + session);

            HubMessage message = messageCallback.waitForMessage();
            Assert.assertNotNull(message);
            HubMessage outMessage =
                    new HubMessage.Builder(message.getMessageType(), message.getMessageBody())
                            .setResponseRequired(true)
                            .build();
            ContextHubTransaction<Void> txn = session.sendMessage(outMessage);
            Assert.assertNotNull(txn);
            ContextHubTransaction.Response<Void> txnResponse =
                    txn.waitForResponse(TIMEOUT_MESSAGE_SECONDS, TimeUnit.SECONDS);
            Assert.assertNotNull(txnResponse);
            Assert.assertEquals(txnResponse.getResult(), ContextHubTransaction.RESULT_SUCCESS);

            ChreTestUtil.unloadNanoAppAssertSuccess(
                    mContextHubManager, mContextHubInfo, mEchoClientNanoappBinary.getNanoAppId());
            Pair<HubEndpointSession, Integer> closeResult = callback.waitForCloseSession();
            Assert.assertNotNull(closeResult);
            Assert.assertNotNull(closeResult.first);
            // TODO(b/385765805): Check for equality
            Log.d(TAG, "Session closed: " + closeResult.first);
            Assert.assertNotNull(closeResult.second);
            Assert.assertEquals(closeResult.second.intValue(), HubEndpoint.REASON_ENDPOINT_STOPPED);
        }

        unregisterRegisteredEndpoint();
    }

    private void printHubDiscoveryInfo(HubDiscoveryInfo info) {
        Log.d(TAG, "Found hub: ");
        Log.d(TAG, " - Endpoint info: " + info.getHubEndpointInfo());
        Log.d(TAG, " - Service info: " + info.getHubServiceInfo());
    }

    private HubEndpoint registerDefaultEndpoint() {
        return registerDefaultEndpoint(
                /* callback= */ null,
                /* messageCallback= */ null,
                /* executor= */ null,
                Collections.emptyList());
    }

    private HubEndpoint registerDefaultEndpoint(
            HubEndpointLifecycleCallback callback, HubEndpointMessageCallback messageCallback) {
        return registerDefaultEndpoint(
                callback, messageCallback, /* executor= */ null, Collections.emptyList());
    }

    private HubEndpoint registerDefaultEndpoint(
            HubEndpointLifecycleCallback callback,
            HubEndpointMessageCallback messageCallback,
            Executor executor) {
        return registerDefaultEndpoint(
                callback, messageCallback, executor, Collections.emptyList());
    }

    private HubEndpoint registerDefaultEndpoint(
            HubEndpointLifecycleCallback callback,
            HubEndpointMessageCallback messageCallback,
            Executor executor,
            Collection<HubServiceInfo> serviceList) {
        Assert.assertNotNull(serviceList);
        Context context = InstrumentationRegistry.getTargetContext();
        HubEndpoint.Builder builder = new HubEndpoint.Builder(context);
        builder.setTag(TAG);
        if (callback != null) {
            if (executor != null) {
                builder.setLifecycleCallback(executor, callback);
            } else {
                builder.setLifecycleCallback(callback);
            }
        }
        if (messageCallback != null) {
            if (executor != null) {
                builder.setMessageCallback(executor, messageCallback);
            } else {
                builder.setMessageCallback(messageCallback);
            }
        }
        builder.setServiceInfoCollection(serviceList);
        HubEndpoint endpoint = builder.build();
        Assert.assertNotNull(endpoint);
        Assert.assertEquals(endpoint.getTag(), TAG);
        Assert.assertEquals(endpoint.getLifecycleCallback(), callback);
        Assert.assertEquals(endpoint.getMessageCallback(), messageCallback);
        Assert.assertEquals(endpoint.getServiceInfoCollection().size(), serviceList.size());

        checkApiSupport((manager) -> manager.registerEndpoint(endpoint));
        return endpoint;
    }

    private void openSessionOrFail(HubEndpoint endpoint, HubEndpointInfo target) {
        checkApiSupport(
                (manager) -> manager.openSession(endpoint, target, ECHO_SERVICE_DESCRIPTOR));
    }

    /**
     * Same as openSessionOrFail but with no service descriptor.
     */
    private void openSessionOrFailNoDescriptor(HubEndpoint endpoint, HubEndpointInfo target) {
        checkApiSupport((manager) -> manager.openSession(endpoint, target));
    }

    private void unregisterRegisteredEndpointNoThrow() {
        try {
            unregisterRegisteredEndpoint();
        } catch (Exception e) {
            Log.e(TAG, "Exception when unregistering endpoint", e);
        }
    }

    private void unregisterRegisteredEndpoint() throws AssertionError {
        checkApiSupport((manager) -> manager.unregisterEndpoint(mRegisteredEndpoint));
        mRegisteredEndpoint = null;
    }

    private void checkApiSupport(Consumer<ContextHubManager> consumer) {
        try {
            consumer.accept(mContextHubManager);
        } catch (UnsupportedOperationException e) {
            // Forced assumption
            Assume.assumeTrue("Skipping endpoint test on unsupported device", false);
        }
    }

    private void checkDynamicEndpointDiscovery(TestDiscoveryCallback callback) throws Exception {
        // TODO(b/385765805): Enable when ready
        boolean isDynamicLoadingSupported = false;
        if (isDynamicLoadingSupported
                && mContextHubInfo != null
                && mEchoServiceNanoappBinary != null) {
            ChreTestUtil.loadNanoAppAssertSuccess(
                    mContextHubManager, mContextHubInfo, mEchoServiceNanoappBinary);
            List<HubDiscoveryInfo> discoveryList = callback.waitForStarted();
            Assert.assertNotNull(discoveryList);
            Assert.assertNotEquals(discoveryList.size(), 0);
            Assert.assertTrue(checkNanoappInDiscoveryList(discoveryList));

            ChreTestUtil.unloadNanoAppAssertSuccess(
                    mContextHubManager, mContextHubInfo, mEchoServiceNanoappBinary.getNanoAppId());
            Pair<List<HubDiscoveryInfo>, Integer> discoveryListAndReason =
                    callback.waitForStopped();
            Assert.assertNotNull(discoveryListAndReason);
            discoveryList = discoveryListAndReason.first;
            Assert.assertNotNull(discoveryList);
            Assert.assertNotEquals(discoveryList.size(), 0);
            Assert.assertTrue(checkNanoappInDiscoveryList(discoveryList));
            Integer reason = discoveryListAndReason.second;
            Assert.assertNotNull(reason);
            Assert.assertEquals(reason.intValue(), HubEndpoint.REASON_ENDPOINT_STOPPED);
        }
    }

    private boolean checkNanoappInDiscoveryList(List<HubDiscoveryInfo> discoveryList) {
        for (HubDiscoveryInfo info : discoveryList) {
            Assert.assertNotNull(info);
            HubEndpointInfo endpointInfo = info.getHubEndpointInfo();
            Assert.assertNotNull(endpointInfo);
            HubEndpointInfo.HubEndpointIdentifier id = endpointInfo.getIdentifier();
            Assert.assertNotNull(id);
            if (id.getEndpoint() == ECHO_SERVICE_NANOAPP_ID) {
                return true;
            }
        }
        return false;
    }
}
