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

package com.google.android.utils.chre;

import com.google.android.chre.utils.pigweed.ChreRpcClient;
import com.google.protobuf.MessageLite;

import java.util.concurrent.TimeUnit;

import dev.chre.rpc.proto.ChreApiTest;
import dev.pigweed.pw_rpc.Call.UnaryFuture;
import dev.pigweed.pw_rpc.MethodClient;
import dev.pigweed.pw_rpc.Service;
import dev.pigweed.pw_rpc.UnaryResult;

/**
 * A set of helper functions for tests that use the CHRE API Test nanoapp.
 */
public class ChreApiTestUtil {
    /**
     * The default timeout for an RPC call in seconds.
     */
    public static final int RPC_TIMEOUT_IN_SECONDS = 2;

    /**
     * Gets the RPC service for the CHRE API Test nanoapp.
     */
    public static Service getChreApiService() {
        Service chreApiService = new Service("chre.rpc.ChreApiTestService",
                Service.unaryMethod("ChreBleGetCapabilities",
                        ChreApiTest.Void.class,
                        ChreApiTest.Capabilities.class),
                Service.unaryMethod("ChreBleGetFilterCapabilities",
                        ChreApiTest.Void.class,
                        ChreApiTest.Capabilities.class),
                Service.unaryMethod("ChreSensorFindDefault",
                        ChreApiTest.ChreSensorFindDefaultInput.class,
                        ChreApiTest.ChreSensorFindDefaultOutput.class),
                Service.unaryMethod("ChreGetSensorInfo",
                        ChreApiTest.ChreHandleInput.class,
                        ChreApiTest.ChreGetSensorInfoOutput.class),
                Service.unaryMethod("ChreGetSensorSamplingStatus",
                        ChreApiTest.ChreHandleInput.class,
                        ChreApiTest.ChreGetSensorSamplingStatusOutput.class),
                Service.unaryMethod("ChreSensorConfigureModeOnly",
                        ChreApiTest.ChreSensorConfigureModeOnlyInput.class,
                        ChreApiTest.Status.class),
                Service.unaryMethod("ChreAudioGetSource",
                        ChreApiTest.ChreHandleInput.class,
                        ChreApiTest.ChreAudioGetSourceOutput.class));
        return chreApiService;
    }

    /**
     * Calls an RPC method with RPC_TIMEOUT_IN_SECONDS seconds of timeout.
     *
     * @param <RequestType>   the type of the request (proto generated type).
     * @param <ResponseType>  the type of the response (proto generated type).
     * @param rpcClient       the RPC client.
     * @param method          the fully-qualified method name.
     * @param request         the request object.
     *
     * @return                the proto response.
     */
    public static <RequestType extends MessageLite, ResponseType extends MessageLite> ResponseType
            callUnaryRpcMethodSync(ChreRpcClient rpcClient, String method, RequestType request)
            throws Exception {
        MethodClient methodClient = rpcClient.getMethodClient(method);
        UnaryFuture<ResponseType> responseFuture = methodClient.invokeUnaryFuture(request);
        UnaryResult<ResponseType> responseResult = responseFuture.get(RPC_TIMEOUT_IN_SECONDS,
                TimeUnit.SECONDS);
        return responseResult.response();
    }

    /**
     * Calls an RPC method with RPC_TIMEOUT_IN_SECONDS seconds of timeout with a void request.
     *
     * @param <ResponseType>  the type of the response (proto generated type).
     * @param rpcClient       the RPC client.
     * @param method          the fully-qualified method name.
     *
     * @return                the proto response.
     */
    public static <ResponseType extends MessageLite> ResponseType
            callUnaryRpcMethodSync(ChreRpcClient rpcClient, String method)
            throws Exception {
        ChreApiTest.Void request = ChreApiTest.Void.newBuilder().build();
        return callUnaryRpcMethodSync(rpcClient, method, request);
    }
}
