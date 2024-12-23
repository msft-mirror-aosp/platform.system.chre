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

import androidx.annotation.Nullable;
import androidx.test.InstrumentationRegistry;

import org.junit.Assert;

import java.util.Arrays;
import java.util.List;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.Executor;
import java.util.concurrent.ScheduledThreadPoolExecutor;
import java.util.concurrent.TimeUnit;

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

    private static final int TIMEOUT_MESSAGE_SECONDS = 5;

    private static final int TIMEOUT_SESSION_OPEN_SECONDS = 5;

    private final ContextHubManager mContextHubManager;

    /** A local hub endpoint currently registered with the service. */
    private HubEndpoint mRegisteredEndpoint = null;

    static class TestLifecycleCallback implements HubEndpointLifecycleCallback {
        @Override
        public HubEndpointSessionResult onSessionOpenRequest(
                HubEndpointInfo requester, String serviceDescriptor) {
            Log.e(TAG, "onSessionOpenRequest");
            // We don't expect any open requests from the remote endpoint.
            return HubEndpointSessionResult.reject("Unexpected request");
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

        private BlockingQueue<HubEndpointSession> mSessionQueue = new ArrayBlockingQueue<>(1);
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

    public ContextHubEchoEndpointExecutor(ContextHubManager manager) {
        mContextHubManager = manager;
    }

    /** Deinitialization code that should be called in e.g. @After. */
    public void deinit() {
        if (mRegisteredEndpoint != null) {
            unregisterEndpointNoThrow(mRegisteredEndpoint);
        }
    }

    /**
     * Checks to see if an echo service exists on the device, and validates the endpoint discovery
     * info contents.
     *
     * @return The list of hub discovery info which contains the echo service.
     */
    public List<HubDiscoveryInfo> getEchoServiceList() {
        List<HubDiscoveryInfo> infoList = mContextHubManager.findEndpoints(ECHO_SERVICE_DESCRIPTOR);
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

            List<HubDiscoveryInfo> identifierDiscoveryList =
                    mContextHubManager.findEndpoints(identifier.getEndpoint());
            Assert.assertNotEquals(identifierDiscoveryList.size(), 0);
        }
        return infoList;
    }

    /** Validates that a local endpoint can be registered/unregistered. */
    public void testDefaultEndpointRegistration() throws Exception {
        mRegisteredEndpoint = registerDefaultEndpoint();
        unregisterEndpoint(mRegisteredEndpoint);
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
            unregisterEndpoint(mRegisteredEndpoint);
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

            unregisterEndpoint(mRegisteredEndpoint);
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

            unregisterEndpoint(mRegisteredEndpoint);
        }
    }

    private void printHubDiscoveryInfo(HubDiscoveryInfo info) {
        Log.d(TAG, "Found hub: ");
        Log.d(TAG, " - Endpoint info: " + info.getHubEndpointInfo());
        Log.d(TAG, " - Service info: " + info.getHubServiceInfo());
    }

    private HubEndpoint registerDefaultEndpoint() {
        return registerDefaultEndpoint(
                /* callback= */ null, /* messageCallback= */ null, /* executor= */ null);
    }

    private HubEndpoint registerDefaultEndpoint(
            HubEndpointLifecycleCallback callback, HubEndpointMessageCallback messageCallback) {
        return registerDefaultEndpoint(callback, messageCallback, /* executor= */ null);
    }

    private HubEndpoint registerDefaultEndpoint(
            HubEndpointLifecycleCallback callback,
            HubEndpointMessageCallback messageCallback,
            Executor executor) {
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
        HubEndpoint endpoint = builder.build();
        Assert.assertNotNull(endpoint);
        Assert.assertEquals(endpoint.getTag(), TAG);
        Assert.assertEquals(endpoint.getLifecycleCallback(), callback);
        Assert.assertEquals(endpoint.getMessageCallback(), messageCallback);
        Assert.assertEquals(endpoint.getServiceInfoCollection().size(), 0);

        try {
            mContextHubManager.registerEndpoint(endpoint);
            Log.i(TAG, "Successfully registered endpoint");
        } catch (Exception e) {
            Log.e(TAG, "Exception when registering endpoint", e);
            Assert.fail("Failed to register endpoint");
        }
        return endpoint;
    }

    private void openSessionOrFail(HubEndpoint endpoint, HubEndpointInfo target) {
        try {
            mContextHubManager.openSession(endpoint, target, ECHO_SERVICE_DESCRIPTOR);
        } catch (Exception e) {
            Assert.fail("Failed to open session: " + e);
        }
    }

    /**
     * Same as openSessionOrFail but with no service descriptor.
     */
    private void openSessionOrFailNoDescriptor(HubEndpoint endpoint, HubEndpointInfo target) {
        try {
            mContextHubManager.openSession(endpoint, target);
        } catch (Exception e) {
            Assert.fail("Failed to open session: " + e);
        }
    }

    private void unregisterEndpointNoThrow(HubEndpoint endpoint) {
        try {
            unregisterEndpoint(mRegisteredEndpoint);
        } catch (AssertionError e) {
            Log.e(TAG, "Exception when unregistering endpoint", e);
        }
    }

    private void unregisterEndpoint(HubEndpoint endpoint) throws AssertionError {
        try {
            mContextHubManager.unregisterEndpoint(mRegisteredEndpoint);
            Log.i(TAG, "Successfully unregistered endpoint");
        } catch (Exception e) {
            Assert.fail("Failed to unregister endpoint");
        }
    }
}
