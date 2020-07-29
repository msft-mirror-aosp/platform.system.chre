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

#include "chpp/services/wwan.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "chpp/common/standard_uuids.h"
#include "chpp/common/wwan.h"
#include "chpp/macros.h"
#include "chpp/memory.h"
#include "chpp/platform/log.h"
#include "chpp/services.h"
#include "chpp/services/wwan_types.h"
#include "chre/pal/wwan.h"

/************************************************
 *  Prototypes
 ***********************************************/

static enum ChppAppErrorCode chppDispatchWwanRequest(void *serviceContext,
                                                     uint8_t *buf, size_t len);

/************************************************
 *  Private Definitions
 ***********************************************/

/**
 * Configuration parameters for this service
 */
static const struct ChppService kWwanServiceConfig = {
    .descriptor.uuid = CHPP_UUID_WWAN_STANDARD,

    // Human-readable name
    .descriptor.name = "WWAN",

    // Version
    .descriptor.version.major = 1,
    .descriptor.version.minor = 0,
    .descriptor.version.patch = 0,

    // Client request dispatch function pointer
    .requestDispatchFunctionPtr = &chppDispatchWwanRequest,

    // Client notification dispatch function pointer
    .notificationDispatchFunctionPtr = NULL,  // Not supported

    // Min length is the entire header
    .minLength = sizeof(struct ChppAppHeader),
};

/**
 * Structure to maintain state for the WWAN service and its Request/Response
 * (RR) functionality.
 */
struct ChppWwanServiceState {
  struct ChppServiceState service;   // WWAN service state
  const struct chrePalWwanApi *api;  // WWAN PAL API

  struct ChppRequestResponseState open;              // Service init state
  struct ChppRequestResponseState close;             // Service deinit state
  struct ChppRequestResponseState getCapabilities;   // Get Capabilities state
  struct ChppRequestResponseState getCellInfoAsync;  // Get CellInfo Async state
};

// Note: This global definition of gWwanServiceContext supports only one
// instance of the CHPP WWAN service at a time. This limitation is primarily due
// to the PAL API.
// It would be possible to generate different API and callback pointers to
// support multiple instances of the service or modify the PAL API to pass a
// void* for context, but this is not necessary in the current version of CHPP.
// In such case, wwanServiceContext would be allocated dynamically as part of
// chppRegisterWwanService(), e.g.
//   struct ChppWwanServiceState *wwanServiceContext = chppMalloc(...);
// instead of globally here.
struct ChppWwanServiceState gWwanServiceContext;

/************************************************
 *  Prototypes
 ***********************************************/

static enum ChppAppErrorCode chppWwanServiceOpen(
    struct ChppWwanServiceState *wwanServiceContext,
    struct ChppAppHeader *requestHeader);
static enum ChppAppErrorCode chppWwanServiceClose(
    struct ChppWwanServiceState *wwanServiceContext,
    struct ChppAppHeader *requestHeader);
static enum ChppAppErrorCode chppWwanServiceGetCapabilities(
    struct ChppWwanServiceState *wwanServiceContext,
    struct ChppAppHeader *requestHeader);
static enum ChppAppErrorCode chppWwanServiceGetCellInfoAsync(
    struct ChppWwanServiceState *wwanServiceContext,
    struct ChppAppHeader *requestHeader);

static void chppWwanServiceCellInfoResultCallback(
    struct chreWwanCellInfoResult *result);

/************************************************
 *  Private Functions
 ***********************************************/

/**
 * Dispatches a client request from the transport layer that is determined to be
 * for the WWAN service. If the result of the dispatch is an error, this
 * function responds to the client with the same error.
 *
 * This function is called from the app layer using its function pointer given
 * during service registration.
 *
 * @param serviceContext Maintains status for each service instance.
 * @param buf Input data. Cannot be null.
 * @param len Length of input data in bytes.
 *
 * @return Indicates the result of this function call.
 */
static enum ChppAppErrorCode chppDispatchWwanRequest(void *serviceContext,
                                                     uint8_t *buf, size_t len) {
  struct ChppAppHeader *rxHeader = (struct ChppAppHeader *)buf;
  struct ChppWwanServiceState *wwanServiceContext =
      (struct ChppWwanServiceState *)serviceContext;
  struct ChppRequestResponseState *rRState;
  enum ChppAppErrorCode error = CHPP_APP_ERROR_NONE;
  bool dispatched = true;

  UNUSED_VAR(len);

  switch (rxHeader->command) {
    case CHPP_WWAN_OPEN: {
      rRState = &wwanServiceContext->open;
      chppServiceTimestampRequest(rRState, rxHeader);
      error = chppWwanServiceOpen(wwanServiceContext, rxHeader);
      break;
    }

    case CHPP_WWAN_CLOSE: {
      rRState = &wwanServiceContext->close;
      chppServiceTimestampRequest(rRState, rxHeader);
      error = chppWwanServiceClose(wwanServiceContext, rxHeader);
      break;
    }

    case CHPP_WWAN_GET_CAPABILITIES: {
      rRState = &wwanServiceContext->getCapabilities;
      chppServiceTimestampRequest(rRState, rxHeader);
      error = chppWwanServiceGetCapabilities(wwanServiceContext, rxHeader);
      break;
    }

    case CHPP_WWAN_GET_CELLINFO_ASYNC: {
      rRState = &wwanServiceContext->getCellInfoAsync;
      chppServiceTimestampRequest(rRState, rxHeader);
      error = chppWwanServiceGetCellInfoAsync(wwanServiceContext, rxHeader);
      break;
    }

    default: {
      dispatched = false;
      error = CHPP_APP_ERROR_INVALID_COMMAND;
      break;
    }
  }

  if (dispatched == true && error != CHPP_APP_ERROR_NONE) {
    // Request was dispatched but an error was returned. Close out
    // chppServiceTimestampRequest()
    chppServiceTimestampResponse(rRState);
  }

  return error;
}

/**
 * Initializes the WWAN service upon an open request from the client and
 * responds to the client with the result.
 *
 * @param serviceContext Maintains status for each service instance.
 * @param requestHeader App layer header of the request.
 *
 * @return Indicates the result of this function call.
 */
static enum ChppAppErrorCode chppWwanServiceOpen(
    struct ChppWwanServiceState *wwanServiceContext,
    struct ChppAppHeader *requestHeader) {
  static const struct chrePalWwanCallbacks palCallbacks = {
      .cellInfoResultCallback = chppWwanServiceCellInfoResultCallback,
  };

  enum ChppAppErrorCode error = CHPP_APP_ERROR_NONE;

  if (!wwanServiceContext->api->open(
          wwanServiceContext->service.appContext->systemApi, &palCallbacks)) {
    CHPP_LOGE("CHPP WWAN PAL API initialization failed");
    CHPP_DEBUG_ASSERT(false);
    error = CHPP_APP_ERROR_UNSPECIFIED;

  } else {
    CHPP_LOGI("CHPP WWAN service initialized");
    struct ChppAppHeader *response =
        chppAllocServiceResponseFixed(requestHeader, struct ChppAppHeader);

    if (response == NULL) {
      CHPP_LOG_OOM();
      error = CHPP_APP_ERROR_OOM;
    } else {
      chppSendTimestampedResponseOrFail(&wwanServiceContext->service,
                                        &wwanServiceContext->open, response,
                                        sizeof(*response));
    }
  }

  return error;
}

/**
 * Deinitializes the WWAN service.
 *
 * @param serviceContext Maintains status for each service instance.
 * @param requestHeader App layer header of the request.
 *
 * @return Indicates the result of this function call.
 */
static enum ChppAppErrorCode chppWwanServiceClose(
    struct ChppWwanServiceState *wwanServiceContext,
    struct ChppAppHeader *requestHeader) {
  enum ChppAppErrorCode error = CHPP_APP_ERROR_NONE;

  wwanServiceContext->api->close();
  CHPP_LOGI("CHPP WWAN service deinitialized");

  struct ChppAppHeader *response =
      chppAllocServiceResponseFixed(requestHeader, struct ChppAppHeader);

  if (response == NULL) {
    CHPP_LOG_OOM();
    error = CHPP_APP_ERROR_OOM;
  } else {
    chppSendTimestampedResponseOrFail(&wwanServiceContext->service,
                                      &wwanServiceContext->close, response,
                                      sizeof(*response));
  }

  return error;
}

/**
 * Retrieves a set of flags indicating the WWAN features supported by the
 * current implementation.
 *
 * @param serviceContext Maintains status for each service instance.
 * @param requestHeader App layer header of the request.
 *
 * @return Indicates the result of this function call.
 */
static enum ChppAppErrorCode chppWwanServiceGetCapabilities(
    struct ChppWwanServiceState *wwanServiceContext,
    struct ChppAppHeader *requestHeader) {
  enum ChppAppErrorCode error = CHPP_APP_ERROR_NONE;

  struct ChppWwanGetCapabilitiesResponse *response =
      chppAllocServiceResponseFixed(requestHeader,
                                    struct ChppWwanGetCapabilitiesResponse);

  if (response == NULL) {
    CHPP_LOG_OOM();
    error = CHPP_APP_ERROR_OOM;
  } else {
    response->capabilities = wwanServiceContext->api->getCapabilities();

    CHPP_LOGD("chppWwanServiceGetCapabilities returning %" PRIx32 ", %zu bytes",
              response->capabilities, sizeof(*response));
    chppSendTimestampedResponseOrFail(&wwanServiceContext->service,
                                      &wwanServiceContext->getCapabilities,
                                      response, sizeof(*response));
  }

  return error;
}

/**
 * Query information about the current serving cell and its neighbors in
 * response to a client request. This does not perform a network scan, but
 * should return state from the current network registration data stored in the
 * cellular modem.
 *
 * This function returns an error code synchronously. The requested cellular
 * information shall be returned asynchronously to the client via the
 * chppPlatformWwanCellInfoResultEvent() service response.
 *
 * @param serviceContext Maintains status for each service instance.
 * @param requestHeader App layer header of the request.
 *
 * @return Indicates the result of this function call.
 */
static enum ChppAppErrorCode chppWwanServiceGetCellInfoAsync(
    struct ChppWwanServiceState *wwanServiceContext,
    struct ChppAppHeader *requestHeader) {
  UNUSED_VAR(requestHeader);

  enum ChppAppErrorCode error = CHPP_APP_ERROR_NONE;

  if (!wwanServiceContext->api->requestCellInfo()) {
    CHPP_LOGE(
        "WWAN requestCellInfo PAL API failed. Unable to register for callback");
    error = CHPP_APP_ERROR_UNSPECIFIED;
  }

  return error;
}

/**
 * PAL callback to provide the result of a prior Request Cell Info
 * (cellInfoResultCallback).
 *
 * @param result Scan results.
 */
static void chppWwanServiceCellInfoResultCallback(
    struct chreWwanCellInfoResult *result) {
  // Recover state
  struct ChppRequestResponseState *rRState =
      (struct ChppRequestResponseState *)result->cookie;
  struct ChppWwanServiceState *wwanServiceContext =
      container_of(rRState, struct ChppWwanServiceState, getCellInfoAsync);

  // Craft response per parser script
  struct ChppWwanCellInfoResultWithHeader *response;
  size_t responseLen;
  if (!chppWwanCellInfoResultFromChre(result, &response, &responseLen)) {
    CHPP_LOGE(
        "chppWwanCellInfoResultFromChre failed (OOM?). Transaction ID = "
        "%" PRIu8,
        rRState->transaction);
    // TODO: consider sending an error response if this fails

  } else {
    response->header.handle = wwanServiceContext->service.handle;
    response->header.type = CHPP_MESSAGE_TYPE_SERVICE_RESPONSE;
    response->header.transaction = rRState->transaction;
    response->header.error = CHPP_APP_ERROR_NONE;
    response->header.command = CHPP_WWAN_GET_CELLINFO_ASYNC;

    chppSendTimestampedResponseOrFail(&wwanServiceContext->service, rRState,
                                      response, responseLen);
  }

  gWwanServiceContext.api->releaseCellInfoResult(result);
}

/************************************************
 *  Public Functions
 ***********************************************/

void chppRegisterWwanService(struct ChppAppState *appContext) {
  gWwanServiceContext.api = chrePalWwanGetApi(CHPP_PAL_WWAN_API_VERSION);

  if (gWwanServiceContext.api == NULL) {
    CHPP_LOGE(
        "WWAN PAL API version not compatible with CHPP. Cannot register WWAN "
        "service");
    CHPP_DEBUG_ASSERT(false);

  } else {
    gWwanServiceContext.service.appContext = appContext;
    gWwanServiceContext.service.handle = chppRegisterService(
        appContext, (void *)&gWwanServiceContext, &kWwanServiceConfig);
    CHPP_DEBUG_ASSERT(gWwanServiceContext.service.handle);
  }
}

void chppDeregisterWwanService(struct ChppAppState *appContext) {
  // TODO

  UNUSED_VAR(appContext);
}