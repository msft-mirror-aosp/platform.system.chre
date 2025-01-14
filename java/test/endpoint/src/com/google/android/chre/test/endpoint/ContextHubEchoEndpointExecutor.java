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
import android.hardware.location.ContextHubManager;
import android.hardware.location.ContextHubTransaction;
import android.util.Log;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.test.InstrumentationRegistry;

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

    private static final long ECHO_NANOAPP_ID = 0x476f6f6754fffffbL;

    private static final int TIMEOUT_MESSAGE_SECONDS = 5;

    private static final int TIMEOUT_SESSION_OPEN_SECONDS = 5;

    private final ContextHubManager mContextHubManager;

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
            Log.e(TAG, "onSessionOpenRequest");
            HubEndpointSessionResult result =
                    mAcceptSession
                            ? HubEndpointSessionResult.accept()
                            : HubEndpointSessionResult.reject("Unexpected request");
            // TODO(b/385765805): Change to assert once callback path is explicitly validated
            if (result.isAccepted() != mAcceptSession) {
                Log.w(
                        TAG,
                        "Unexpected session result status: expected "
                                + mAcceptSession
                                + " got "
                                + result.isAccepted()
                                + " reason="
                                + result.getReason());
            }
            return result;
        }

        @Override
        public void onSessionOpened(HubEndpointSession session) {
            Log.d(TAG, "onSessionOpened: session=" + session);
            mSessionQueue.add(session);
        }

        @Override
        public void onSessionClosed(HubEndpointSession session, int reason) {
            Log.e(TAG, "onSessionClosed: session=" + session);
        }

        public HubEndpointSession waitForEndpointSession() throws InterruptedException {
            return mSessionQueue.poll(TIMEOUT_SESSION_OPEN_SECONDS, TimeUnit.SECONDS);
        }

        /** If true, accepts incoming sessions */
        private final boolean mAcceptSession;

        private final BlockingQueue<HubEndpointSession> mSessionQueue = new ArrayBlockingQueue<>(1);
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
        }
    }

    public ContextHubEchoEndpointExecutor(ContextHubManager manager) {
        mContextHubManager = manager;
    }

    /** Deinitialization code that should be called in e.g. @After. */
    public void deinit() {
        if (mRegisteredEndpoint != null) {
            unregisterRegisteredEndpointNoThrow();
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
            txn.waitForResponse(TIMEOUT_MESSAGE_SECONDS, TimeUnit.SECONDS);
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

    public void testEndpointDiscovery() {
        doTestEndpointDiscovery(/* executor= */ null);
    }

    public void testThreadedEndpointDiscovery() {
        ScheduledThreadPoolExecutor executor =
                new ScheduledThreadPoolExecutor(/* corePoolSize= */ 1);
        doTestEndpointDiscovery(executor);
    }

    /**
     * Registers an endpoint discovery callback for endpoints with the echo service descriptor.
     *
     * @param executor An optional executor to invoke callbacks on.
     */
    private void doTestEndpointDiscovery(@Nullable Executor executor) {
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

        // TODO(b/385765805): Add CHRE/dynamic discovery

        checkApiSupport((manager) -> manager.unregisterEndpointDiscoveryCallback(callback));
    }

    public void testEndpointIdDiscovery() {
        doTestEndpointIdDiscovery(/* executor= */ null);
    }

    public void testThreadedEndpointIdDiscovery() {
        ScheduledThreadPoolExecutor executor =
                new ScheduledThreadPoolExecutor(/* corePoolSize= */ 1);
        doTestEndpointIdDiscovery(executor);
    }

    /**
     * Registers an endpoint discovery callback for endpoints with the echo message nanoapp ID.
     *
     * @param executor An optional executor to invoke callbacks on.
     */
    private void doTestEndpointIdDiscovery(@Nullable Executor executor) {
        TestDiscoveryCallback callback = new TestDiscoveryCallback();
        if (executor != null) {
            checkApiSupport(
                    (manager) ->
                            manager.registerEndpointDiscoveryCallback(
                                    executor, callback, ECHO_NANOAPP_ID));
        } else {
            checkApiSupport(
                    (manager) ->
                            manager.registerEndpointDiscoveryCallback(
                                    callback, ECHO_NANOAPP_ID));
        }

        // TODO(b/385765805): Add CHRE/dynamic discovery

        checkApiSupport((manager) -> manager.unregisterEndpointDiscoveryCallback(callback));
    }

    /**
     * A test to see if a echo test service can be registered by the application. For CHRE-capable
     * devices, we will also confirm that a connection can be started from the embedded client and
     * echo works as intended.
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
        mRegisteredEndpoint =
                registerDefaultEndpoint(
                        callback, /* messageCallback= */ null, /* executor= */ null, serviceList);

        // TODO(b/385765805): Add CHRE client and test echo

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
}
