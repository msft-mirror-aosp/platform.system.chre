/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "fake_link_client.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "chpp/app.h"
#include "chpp/clients.h"
#include "chpp/condition_variable.h"
#include "chpp/log.h"
#include "chpp/macros.h"
#include "chpp/mutex.h"
#ifdef CHPP_CLIENT_ENABLED_VENDOR
#include "chpp/platform/vendor_clients.h"
#endif

/************************************************
 *  Prototypes
 ***********************************************/

static bool chppTestClientInit(void *clientContext, uint8_t handle,
                               struct ChppVersion serviceVersion);
static void chppTestClientDeinit(void *clientContext);
static void chppTestClientProcessTimeout(void *clientContext);

/************************************************
 *  Private Definitions
 ***********************************************/

#define CHPP_TESTCLIENT_REQUEST_MAX 0

/**
 * Structure to maintain state for the Context client and its Request/Response
 * (RR) functionality.
 */
struct ChppTestClientState {
  struct ChppEndpointState client;  // CHPP client state
  struct ChppOutgoingRequestState outReqStates[CHPP_TESTCLIENT_REQUEST_MAX + 1];
  bool timeoutPending;
};

// Note: This global definition of gTestClientContext supports only one
// instance of the CHPP Context client at a time.
struct ChppTestClientState gTestClientContext;
struct ChppConditionVariable gTestClientTimeoutCondition;
struct ChppMutex gTestClientTimeoutMutex;

/**
 * Test Client UUID
 */
#define CHPP_UUID_CLIENT_TEST                      \
  {0x3d, 0x29, 0x78, 0x28, 0x79, 0xf0, 0x4a, 0xad, \
   0x8f, 0x72, 0x22, 0x15, 0x2f, 0x7d, 0xcc, 0x04}

/**
 * Configuration parameters for this client
 */
static const struct ChppClient kTestClientConfig = {
    .descriptor.uuid = CHPP_UUID_CLIENT_TEST,

    // Version
    .descriptor.version.major = 1,
    .descriptor.version.minor = 0,
    .descriptor.version.patch = 0,

    // Notifies client if CHPP is reset
    .resetNotifierFunctionPtr = NULL,

    // Notifies client if they are matched to a service
    .matchNotifierFunctionPtr = NULL,

    // Service response dispatch function pointer
    .responseDispatchFunctionPtr = NULL,

    // Service notification dispatch function pointer
    .notificationDispatchFunctionPtr = NULL,

    // Service response dispatch function pointer
    .initFunctionPtr = &chppTestClientInit,

    // Service notification dispatch function pointer
    .deinitFunctionPtr = &chppTestClientDeinit,

    // Client timeout function pointer
    .timeoutFunctionPtr = &chppTestClientProcessTimeout,

    // Number of request-response states in the outReqStates array.
    .outReqCount = ARRAY_SIZE(gTestClientContext.outReqStates),

    // Min length is the entire header
    .minLength = sizeof(struct ChppAppHeader),
};

/************************************************
 *  Private Functions
 ***********************************************/

static bool chppTestClientInit(void *clientContext, uint8_t handle,
                               struct ChppVersion serviceVersion) {
  CHPP_LOGI("%s", __func__);
  UNUSED_VAR(serviceVersion);

  struct ChppTestClientState *TestClientContext =
      (struct ChppTestClientState *)clientContext;
  chppClientInit(&TestClientContext->client, handle);

  return true;
}

/**
 * Deinitializes the client.
 *
 * @param clientContext Maintains status for each client instance.
 */
static void chppTestClientDeinit(void *clientContext) {
  struct ChppTestClientState *TestClientContext =
      (struct ChppTestClientState *)clientContext;
  chppClientDeinit(&TestClientContext->client);
}

/************************************************
 *  Public Functions
 ***********************************************/

void chppRegisterVendorClients(struct ChppAppState *context) {
  CHPP_DEBUG_NOT_NULL(context);

  if (context->clientServiceSet.vendorClients) {
    chppRegisterTestClient(context);
  }
}

void chppDeregisterVendorClients(struct ChppAppState *context) {
  CHPP_DEBUG_NOT_NULL(context);

  if (context->clientServiceSet.vendorClients) {
    chppDeregisterTestClient(context);
  }
}

void chppRegisterTestClient(struct ChppAppState *appContext) {
  CHPP_LOGI("%s", __func__);
  memset(&gTestClientContext, 0, sizeof(gTestClientContext));
  chppRegisterClient(appContext, (void *)&gTestClientContext,
                     &gTestClientContext.client,
                     gTestClientContext.outReqStates, &kTestClientConfig);

  // Trigger a timeout to test the timeout mechanism
  chppMutexLock(&gTestClientTimeoutMutex);
  gTestClientContext.timeoutPending = true;
  chppAppRequestTimerTimeout(&gTestClientContext.client,
                             CHPP_TEST_CLIENT_TIMEOUT_MS * CHPP_NSEC_PER_MSEC);
  chppMutexUnlock(&gTestClientTimeoutMutex);
}

void chppDeregisterTestClient(struct ChppAppState *appContext) {
  CHPP_LOGI("%s", __func__);
  UNUSED_VAR(appContext);
}

void chppTestClientProcessTimeout(void *clientContext) {
  CHPP_LOGI("%s", __func__);
  UNUSED_VAR(clientContext);
  chppMutexLock(&gTestClientTimeoutMutex);
  gTestClientContext.timeoutPending = false;
  chppConditionVariableSignal(&gTestClientTimeoutCondition);
  chppMutexUnlock(&gTestClientTimeoutMutex);
}

bool chppTestClientWaitForTimeout(uint64_t timeoutMs) {
  bool timeoutTriggered = false;
  chppMutexLock(&gTestClientTimeoutMutex);
  if (gTestClientContext.timeoutPending) {
    chppConditionVariableTimedWait(&gTestClientTimeoutCondition,
                                   &gTestClientTimeoutMutex,
                                   timeoutMs * CHPP_NSEC_PER_MSEC);
  }
  timeoutTriggered = !gTestClientContext.timeoutPending;
  chppMutexUnlock(&gTestClientTimeoutMutex);
  return timeoutTriggered;
}

struct ChppEndpointState *getChppTestClientState(void) {
  return &gTestClientContext.client;
}
