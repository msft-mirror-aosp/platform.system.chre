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

#include "chpp/clients/loopback.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "chpp/app.h"
#include "chpp/clients.h"
#include "chpp/clients/discovery.h"
#include "chpp/log.h"
#include "chpp/memory.h"
#include "chpp/transport.h"

/************************************************
 *  Prototypes
 ***********************************************/

/************************************************
 *  Private Definitions
 ***********************************************/

/**
 * Structure to maintain state for the loopback client and its Request/Response
 * (RR) functionality.
 */
struct ChppLoopbackClientState {
  struct ChppEndpointState client;                  // CHPP client state
  struct ChppOutgoingRequestState runLoopbackTest;  // Loopback test state

  struct ChppLoopbackTestResult testResult;  // Last test result
  const uint8_t *loopbackData;               // Pointer to loopback data
};

// A loopback test buffer used for chppRunLoopbackTestAsync.
#define LOOPBACK_BUF_LEN 3
static const uint8_t gLoopbackBuf[LOOPBACK_BUF_LEN] = {1, 2, 3};

/************************************************
 *  Public Functions
 ***********************************************/

void chppLoopbackClientInit(struct ChppAppState *appState) {
  CHPP_LOGD("Loopback client init");
  CHPP_DEBUG_NOT_NULL(appState);
  if (appState->loopbackClientContext != NULL) {
    CHPP_LOGE("Loopback client already initialized");
    return;
  }

  appState->loopbackClientContext =
      chppMalloc(sizeof(struct ChppLoopbackClientState));
  CHPP_NOT_NULL(appState->loopbackClientContext);
  struct ChppLoopbackClientState *state = appState->loopbackClientContext;
  memset(state, 0, sizeof(struct ChppLoopbackClientState));

  state->client.appContext = appState;
  chppClientInit(&state->client, CHPP_HANDLE_LOOPBACK);
  state->testResult.error = CHPP_APP_ERROR_NONE;
  state->client.openState = CHPP_OPEN_STATE_OPENED;
}

void chppLoopbackClientDeinit(struct ChppAppState *appState) {
  CHPP_LOGD("Loopback client deinit");
  CHPP_NOT_NULL(appState);
  CHPP_NOT_NULL(appState->loopbackClientContext);

  chppClientDeinit(&appState->loopbackClientContext->client);
  CHPP_FREE_AND_NULLIFY(appState->loopbackClientContext);
}

bool chppDispatchLoopbackServiceResponse(struct ChppAppState *appState,
                                         const uint8_t *response, size_t len) {
  CHPP_LOGD("Loopback client dispatch service response");
  CHPP_ASSERT(len >= CHPP_LOOPBACK_HEADER_LEN);
  CHPP_NOT_NULL(response);
  CHPP_DEBUG_NOT_NULL(appState);
  struct ChppLoopbackClientState *state = appState->loopbackClientContext;
  CHPP_NOT_NULL(state);
  CHPP_NOT_NULL(state->loopbackData);

  CHPP_ASSERT(chppTimestampIncomingResponse(
      state->client.appContext, &state->runLoopbackTest,
      (const struct ChppAppHeader *)response));

  struct ChppLoopbackTestResult *result = &state->testResult;

  chppMutexLock(&state->client.syncResponse.mutex);
  result->error = CHPP_APP_ERROR_NONE;
  result->responseLen = len;
  result->firstError = len;
  result->byteErrors = 0;
  result->rttNs = state->runLoopbackTest.responseTimeNs -
                  state->runLoopbackTest.requestTimeNs;

  if (result->requestLen != result->responseLen) {
    result->error = CHPP_APP_ERROR_INVALID_LENGTH;
    result->firstError = MIN(result->requestLen, result->responseLen);
  }

  for (size_t loc = CHPP_LOOPBACK_HEADER_LEN;
       loc < MIN(result->requestLen, result->responseLen); loc++) {
    if (state->loopbackData[loc - CHPP_LOOPBACK_HEADER_LEN] != response[loc]) {
      result->error = CHPP_APP_ERROR_UNSPECIFIED;
      result->firstError =
          MIN(result->firstError, loc - CHPP_LOOPBACK_HEADER_LEN);
      result->byteErrors++;
    }
  }

  CHPP_LOGI("Loopback client RX err=0x%" PRIx16 " len=%" PRIuSIZE
            " req len=%" PRIuSIZE " first err=%" PRIuSIZE
            " total err=%" PRIuSIZE,
            result->error, result->responseLen, result->requestLen,
            result->firstError, result->byteErrors);

  // Notify waiting (synchronous) client
  state->client.syncResponse.ready = true;
  chppConditionVariableSignal(&state->client.syncResponse.condVar);
  chppMutexUnlock(&state->client.syncResponse.mutex);

  return true;
}

static enum ChppAppErrorCode chppLoopbackCheckPreconditions(
    struct ChppAppState *appState) {
  if (appState == NULL) {
    CHPP_LOGE("Cannot run loopback test with null app");
    return CHPP_APP_ERROR_UNSUPPORTED;
  }

  if (!chppWaitForDiscoveryComplete(appState, 0 /* timeoutMs */)) {
    return CHPP_APP_ERROR_NOT_READY;
  }

  return CHPP_APP_ERROR_NONE;
}

/**
 * Internal method for running the loopback test (sync or async).
 *
 * Note that the input buf must be valid for duration of the loopback test, so
 * it is recommended to use a read-only constant.
 *
 * @param appState Application layer state.
 * @param buf Input data. Cannot be null.
 * @param len Length of input data in bytes.
 * @param sync If true, runs the loopback test in synchronous mode.
 * @param result A non-null pointer to store the detailed result of the test.
 *
 * @return true if the test was successfully started
 */
static bool chppRunLoopbackTestInternal(struct ChppAppState *appState,
                                        const uint8_t *buf, size_t len,
                                        bool sync,
                                        struct ChppLoopbackTestResult *out) {
  CHPP_NOT_NULL(out);
  bool success = false;
  CHPP_LOGD("Loopback client TX len=%" PRIuSIZE,
            len + CHPP_LOOPBACK_HEADER_LEN);

  enum ChppAppErrorCode error = chppLoopbackCheckPreconditions(appState);
  if (error != CHPP_APP_ERROR_NONE) {
    out->error = error;
  } else if (len == 0) {
    CHPP_LOGE("Loopback payload=0!");
    out->error = CHPP_APP_ERROR_INVALID_LENGTH;
  } else {
    struct ChppLoopbackClientState *state = appState->loopbackClientContext;
    CHPP_NOT_NULL(state);
    chppMutexLock(&state->client.syncResponse.mutex);
    struct ChppLoopbackTestResult *result = &state->testResult;

    if (result->error == CHPP_APP_ERROR_BLOCKED) {
      CHPP_DEBUG_ASSERT_LOG(false, "Another loopback in progress");
      out->error = CHPP_APP_ERROR_BLOCKED;
    } else {
      memset(result, 0, sizeof(struct ChppLoopbackTestResult));
      result->error = CHPP_APP_ERROR_BLOCKED;
      result->requestLen = len + CHPP_LOOPBACK_HEADER_LEN;

      uint8_t *loopbackRequest =
          (uint8_t *)chppAllocClientRequest(&state->client, result->requestLen);

      if (loopbackRequest == NULL) {
        CHPP_LOG_OOM();
        result->error = CHPP_APP_ERROR_OOM;
      } else {
        state->loopbackData = buf;
        memcpy(&loopbackRequest[CHPP_LOOPBACK_HEADER_LEN], buf, len);

        chppMutexUnlock(&state->client.syncResponse.mutex);
        if (sync) {
          if (!chppClientSendTimestampedRequestAndWaitTimeout(
                  &state->client, &state->runLoopbackTest, loopbackRequest,
                  result->requestLen, 5 * CHPP_NSEC_PER_SEC)) {
            result->error = CHPP_APP_ERROR_UNSPECIFIED;
          } else {
            success = true;
          }
        } else {
          if (!chppClientSendTimestampedRequestOrFail(
                  &state->client, &state->runLoopbackTest, loopbackRequest,
                  result->requestLen, 5 * CHPP_NSEC_PER_SEC)) {
            result->error = CHPP_APP_ERROR_UNSPECIFIED;
          } else {
            success = true;
          }
        }
        chppMutexLock(&state->client.syncResponse.mutex);

        *out = state->testResult;
      }
    }

    chppMutexUnlock(&state->client.syncResponse.mutex);
  }

  return success;
}

struct ChppLoopbackTestResult chppRunLoopbackTest(struct ChppAppState *appState,
                                                  const uint8_t *buf,
                                                  size_t len) {
  struct ChppLoopbackTestResult result;
  chppRunLoopbackTestInternal(appState, buf, len, /* sync= */ true, &result);
  return result;
}

enum ChppAppErrorCode chppRunLoopbackTestAsync(struct ChppAppState *appState) {
  struct ChppLoopbackTestResult result;
  bool success = chppRunLoopbackTestInternal(
      appState, gLoopbackBuf, LOOPBACK_BUF_LEN, /* sync= */ false, &result);
  // Override the result for the success case because for async, the stored
  // error code would be CHPP_APP_ERROR_BLOCKED until the response arrives.
  return success ? CHPP_APP_ERROR_NONE : result.error;
}