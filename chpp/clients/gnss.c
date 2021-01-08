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

#include "chpp/clients/gnss.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "chpp/app.h"
#include "chpp/clients.h"
#include "chpp/clients/discovery.h"
#include "chpp/common/gnss.h"
#include "chpp/common/gnss_types.h"
#include "chpp/common/standard_uuids.h"
#include "chpp/log.h"
#include "chpp/macros.h"
#include "chpp/memory.h"
#include "chre/pal/gnss.h"
#include "chre_api/chre/gnss.h"

#ifndef CHPP_GNSS_DISCOVERY_TIMEOUT_MS
#define CHPP_GNSS_DISCOVERY_TIMEOUT_MS CHPP_DISCOVERY_DEFAULT_TIMEOUT_MS
#endif

/************************************************
 *  Prototypes
 ***********************************************/

static enum ChppAppErrorCode chppDispatchGnssResponse(void *clientContext,
                                                      uint8_t *buf, size_t len);
static enum ChppAppErrorCode chppDispatchGnssNotification(void *clientContext,
                                                          uint8_t *buf,
                                                          size_t len);
static bool chppGnssClientInit(void *clientContext, uint8_t handle,
                               struct ChppVersion serviceVersion);
static void chppGnssClientDeinit(void *clientContext);

/************************************************
 *  Private Definitions
 ***********************************************/

/**
 * Configuration parameters for this client
 */
static const struct ChppClient kGnssClientConfig = {
    .descriptor.uuid = CHPP_UUID_GNSS_STANDARD,

    // Version
    .descriptor.version.major = 1,
    .descriptor.version.minor = 0,
    .descriptor.version.patch = 0,

    // Notifies client if CHPP is reset
    .resetNotifierFunctionPtr = NULL,

    // Service response dispatch function pointer
    .responseDispatchFunctionPtr = &chppDispatchGnssResponse,

    // Service notification dispatch function pointer
    .notificationDispatchFunctionPtr = &chppDispatchGnssNotification,

    // Service response dispatch function pointer
    .initFunctionPtr = &chppGnssClientInit,

    // Service notification dispatch function pointer
    .deinitFunctionPtr = &chppGnssClientDeinit,

    // Min length is the entire header
    .minLength = sizeof(struct ChppAppHeader),
};

/**
 * Structure to maintain state for the GNSS client and its Request/Response
 * (RR) functionality.
 */
struct ChppGnssClientState {
  struct ChppClientState client;     // GNSS client state
  const struct chrePalGnssApi *api;  // GNSS PAL API

  struct ChppRequestResponseState open;             // Service init state
  struct ChppRequestResponseState close;            // Service deinit state
  struct ChppRequestResponseState getCapabilities;  // Get Capabilities state
  struct ChppRequestResponseState
      controlLocationSession;  // Control Location Session state
  struct ChppRequestResponseState
      controlMeasurementSession;  // Control Measurement Session state
  struct ChppRequestResponseState
      passiveLocationListener;  // PassiveLocationListener state

  uint32_t capabilities;  // Cached GetCapabilities result

  bool opened;  // GNSS has been successfully opened
};

// Note: This global definition of gGnssClientContext supports only one
// instance of the CHPP GNSS client at a time.
struct ChppGnssClientState gGnssClientContext;
static const struct chrePalSystemApi *gSystemApi;
static const struct chrePalGnssCallbacks *gCallbacks;

/************************************************
 *  Prototypes
 ***********************************************/

static bool chppGnssClientOpen(const struct chrePalSystemApi *systemApi,
                               const struct chrePalGnssCallbacks *callbacks);
static void chppGnssClientClose(void);
static uint32_t chppGnssClientGetCapabilities(void);
static bool chppGnssClientControlLocationSession(bool enable,
                                                 uint32_t minIntervalMs,
                                                 uint32_t minTimeToNextFixMs);
static void chppGnssClientReleaseLocationEvent(
    struct chreGnssLocationEvent *event);
static bool chppGnssClientControlMeasurementSession(bool enable,
                                                    uint32_t minIntervalMs);
static void chppGnssClientReleaseMeasurementDataEvent(
    struct chreGnssDataEvent *event);
static bool chppGnssClientConfigurePassiveLocationListener(bool enable);

static void chppGnssOpenResult(struct ChppGnssClientState *clientContext,
                               uint8_t *buf, size_t len);
static void chppGnssCloseResult(struct ChppGnssClientState *clientContext,
                                uint8_t *buf, size_t len);
static void chppGnssGetCapabilitiesResult(
    struct ChppGnssClientState *clientContext, uint8_t *buf, size_t len);
static void chppGnssControlLocationSessionResult(
    struct ChppGnssClientState *clientContext, uint8_t *buf, size_t len);
static void chppGnssControlMeasurementSessionResult(
    struct ChppGnssClientState *clientContext, uint8_t *buf, size_t len);

static void chppGnssStateResyncNotification(
    struct ChppGnssClientState *clientContext, uint8_t *buf, size_t len);
static void chppGnssLocationResultNotification(
    struct ChppGnssClientState *clientContext, uint8_t *buf, size_t len);
static void chppGnssMeasurementResultNotification(
    struct ChppGnssClientState *clientContext, uint8_t *buf, size_t len);

/************************************************
 *  Private Functions
 ***********************************************/

/**
 * Dispatches a service response from the transport layer that is determined to
 * be for the GNSS client.
 *
 * This function is called from the app layer using its function pointer given
 * during client registration.
 *
 * @param clientContext Maintains status for each client instance.
 * @param buf Input data. Cannot be null.
 * @param len Length of input data in bytes.
 *
 * @return Indicates the result of this function call.
 */
static enum ChppAppErrorCode chppDispatchGnssResponse(void *clientContext,
                                                      uint8_t *buf,
                                                      size_t len) {
  struct ChppAppHeader *rxHeader = (struct ChppAppHeader *)buf;
  struct ChppGnssClientState *gnssClientContext =
      (struct ChppGnssClientState *)clientContext;
  enum ChppAppErrorCode error = CHPP_APP_ERROR_NONE;

  switch (rxHeader->command) {
    case CHPP_GNSS_OPEN: {
      chppClientTimestampResponse(&gnssClientContext->open, rxHeader);
      chppGnssOpenResult(gnssClientContext, buf, len);
      break;
    }

    case CHPP_GNSS_CLOSE: {
      chppClientTimestampResponse(&gnssClientContext->close, rxHeader);
      chppGnssCloseResult(gnssClientContext, buf, len);
      break;
    }

    case CHPP_GNSS_GET_CAPABILITIES: {
      chppClientTimestampResponse(&gnssClientContext->getCapabilities,
                                  rxHeader);
      chppGnssGetCapabilitiesResult(gnssClientContext, buf, len);
      break;
    }

    case CHPP_GNSS_CONTROL_LOCATION_SESSION: {
      chppClientTimestampResponse(&gnssClientContext->controlLocationSession,
                                  rxHeader);
      chppGnssControlLocationSessionResult(gnssClientContext, buf, len);
      break;
    }

    case CHPP_GNSS_CONTROL_MEASUREMENT_SESSION: {
      chppClientTimestampResponse(&gnssClientContext->controlMeasurementSession,
                                  rxHeader);
      chppGnssControlMeasurementSessionResult(gnssClientContext, buf, len);
      break;
    }

    default: {
      error = CHPP_APP_ERROR_INVALID_COMMAND;
      break;
    }
  }

  return error;
}

/**
 * Dispatches a service notification from the transport layer that is determined
 * to be for the GNSS client.
 *
 * This function is called from the app layer using its function pointer given
 * during client registration.
 *
 * @param clientContext Maintains status for each client instance.
 * @param buf Input data. Cannot be null.
 * @param len Length of input data in bytes.
 *
 * @return Indicates the result of this function call.
 */
static enum ChppAppErrorCode chppDispatchGnssNotification(void *clientContext,
                                                          uint8_t *buf,
                                                          size_t len) {
  struct ChppAppHeader *rxHeader = (struct ChppAppHeader *)buf;
  struct ChppGnssClientState *gnssClientContext =
      (struct ChppGnssClientState *)clientContext;
  enum ChppAppErrorCode error = CHPP_APP_ERROR_NONE;

  switch (rxHeader->command) {
    case CHPP_GNSS_REQUEST_STATE_RESYNC_NOTIFICATION: {
      chppGnssStateResyncNotification(gnssClientContext, buf, len);
      break;
    }

    case CHPP_GNSS_LOCATION_RESULT_NOTIFICATION: {
      chppGnssLocationResultNotification(gnssClientContext, buf, len);
      break;
    }

    case CHPP_GNSS_MEASUREMENT_RESULT_NOTIFICATION: {
      chppGnssMeasurementResultNotification(gnssClientContext, buf, len);
      break;
    }

    default: {
      error = CHPP_APP_ERROR_INVALID_COMMAND;
      break;
    }
  }

  return error;
}

/**
 * Initializes the client and provides its handle number and the version of the
 * matched service when/if it the client is matched with a service during
 * discovery.
 *
 * @param clientContext Maintains status for each client instance.
 * @param handle Handle number for this client.
 * @param serviceVersion Version of the matched service.
 *
 * @return True if client is compatible and successfully initialized.
 */
static bool chppGnssClientInit(void *clientContext, uint8_t handle,
                               struct ChppVersion serviceVersion) {
  UNUSED_VAR(serviceVersion);

  struct ChppGnssClientState *gnssClientContext =
      (struct ChppGnssClientState *)clientContext;
  chppClientInit(&gnssClientContext->client, handle);

  return true;
}

/**
 * Deinitializes the client.
 *
 * @param clientContext Maintains status for each client instance.
 */
static void chppGnssClientDeinit(void *clientContext) {
  struct ChppGnssClientState *gnssClientContext =
      (struct ChppGnssClientState *)clientContext;
  chppClientDeinit(&gnssClientContext->client);
}

/**
 * Handles the service response for the open client request.
 *
 * This function is called from chppDispatchGnssResponse().
 *
 * @param clientContext Maintains status for each client instance.
 * @param buf Input data. Cannot be null.
 * @param len Length of input data in bytes.
 */
static void chppGnssOpenResult(struct ChppGnssClientState *clientContext,
                               uint8_t *buf, size_t len) {
  UNUSED_VAR(len);

  struct ChppAppHeader *rxHeader = (struct ChppAppHeader *)buf;
  clientContext->opened = (rxHeader->error == CHPP_APP_ERROR_NONE);
  if (!clientContext->opened) {
    CHPP_LOGE("GNSS open failed at service");
  } else {
    CHPP_LOGI("GNSS open succeeded at service");
  }
}

/**
 * Handles the service response for the close client request.
 *
 * This function is called from chppDispatchGnssResponse().
 *
 * @param clientContext Maintains status for each client instance.
 * @param buf Input data. Cannot be null.
 * @param len Length of input data in bytes.
 */
static void chppGnssCloseResult(struct ChppGnssClientState *clientContext,
                                uint8_t *buf, size_t len) {
  // TODO
  UNUSED_VAR(clientContext);
  UNUSED_VAR(buf);
  UNUSED_VAR(len);
}

/**
 * Handles the service response for the get capabilities client request.
 *
 * This function is called from chppDispatchGnssResponse().
 *
 * @param clientContext Maintains status for each client instance.
 * @param buf Input data. Cannot be null.
 * @param len Length of input data in bytes.
 */
static void chppGnssGetCapabilitiesResult(
    struct ChppGnssClientState *clientContext, uint8_t *buf, size_t len) {
  if (len < sizeof(struct ChppGnssGetCapabilitiesResponse)) {
    struct ChppAppHeader *rxHeader = (struct ChppAppHeader *)buf;
    CHPP_LOGE("GNSS GetCapabilities request failed at service. error=%" PRIu8,
              rxHeader->error);

  } else {
    struct ChppGnssGetCapabilitiesParameters *result =
        &((struct ChppGnssGetCapabilitiesResponse *)buf)->params;

    CHPP_LOGI("chppGnssGetCapabilitiesResult received capabilities=0x%" PRIx32,
              result->capabilities);

    clientContext->capabilities = result->capabilities;
  }
}

/**
 * Handles the service response for the Control Location Session client request.
 *
 * This function is called from chppDispatchGnssResponse().
 *
 * @param clientContext Maintains status for each client instance.
 * @param buf Input data. Cannot be null.
 * @param len Length of input data in bytes.
 */
static void chppGnssControlLocationSessionResult(
    struct ChppGnssClientState *clientContext, uint8_t *buf, size_t len) {
  UNUSED_VAR(clientContext);

  if (len < sizeof(struct ChppGnssControlLocationSessionResponse)) {
    struct ChppAppHeader *rxHeader = (struct ChppAppHeader *)buf;
    CHPP_LOGE(
        "GNSS ControlLocationSession request failed at service. error=%" PRIu8,
        rxHeader->error);

  } else {
    struct ChppGnssControlLocationSessionResponse *result =
        (struct ChppGnssControlLocationSessionResponse *)buf;

    CHPP_LOGI(
        "chppGnssControlLocationSessionResult received enable=%s, "
        "errorCode=%" PRIu8,
        result->enabled ? "true" : "false", result->errorCode);

    gCallbacks->locationStatusChangeCallback(result->enabled,
                                             result->errorCode);
  }
}

/**
 * Handles the service response for the Control Measurement Session client
 * request.
 *
 * This function is called from chppDispatchGnssResponse().
 *
 * @param clientContext Maintains status for each client instance.
 * @param buf Input data. Cannot be null.
 * @param len Length of input data in bytes.
 */
static void chppGnssControlMeasurementSessionResult(
    struct ChppGnssClientState *clientContext, uint8_t *buf, size_t len) {
  UNUSED_VAR(clientContext);

  if (len < sizeof(struct ChppGnssControlMeasurementSessionResponse)) {
    struct ChppAppHeader *rxHeader = (struct ChppAppHeader *)buf;
    CHPP_LOGE(
        "GNSS ControlMeasurementSession request failed at service. "
        "error=%" PRIu8,
        rxHeader->error);

  } else {
    struct ChppGnssControlMeasurementSessionResponse *result =
        (struct ChppGnssControlMeasurementSessionResponse *)buf;

    CHPP_LOGI(
        "chppGnssControlMeasurementSessionResult received enable=%s, "
        "errorCode=%" PRIu8,
        result->enabled ? "true" : "false", result->errorCode);

    gCallbacks->measurementStatusChangeCallback(result->enabled,
                                                result->errorCode);
  }
}

/**
 * Handles the State Resync service notification.
 *
 * This function is called from chppDispatchGnssNotification().
 *
 * @param clientContext Maintains status for each client instance.
 * @param buf Input data. Cannot be null.
 * @param len Length of input data in bytes.
 */
static void chppGnssStateResyncNotification(
    struct ChppGnssClientState *clientContext, uint8_t *buf, size_t len) {
  UNUSED_VAR(clientContext);
  UNUSED_VAR(buf);
  UNUSED_VAR(len);
  // TODO: Possibly needs more housekeeping
  gCallbacks->requestStateResync();
}

/**
 * Handles the Location Result service notification.
 *
 * This function is called from chppDispatchGnssNotification().
 *
 * @param clientContext Maintains status for each client instance.
 * @param buf Input data. Cannot be null.
 * @param len Length of input data in bytes.
 */
static void chppGnssLocationResultNotification(
    struct ChppGnssClientState *clientContext, uint8_t *buf, size_t len) {
  UNUSED_VAR(clientContext);
  CHPP_LOGI("chppGnssLocationResultNotification received data len=%" PRIuSIZE,
            len);

  buf += sizeof(struct ChppAppHeader);
  len -= sizeof(struct ChppAppHeader);

  struct chreGnssLocationEvent *chre =
      chppGnssLocationEventToChre((struct ChppGnssLocationEvent *)buf, len);

  if (chre == NULL) {
    CHPP_LOGE(
        "chppGnssLocationResultNotification CHPP -> CHRE conversion failed. "
        "Input len=%" PRIuSIZE,
        len);
  } else {
    gCallbacks->locationEventCallback(chre);
  }
}

/**
 * Handles the Measurement Result service notification.
 *
 * This function is called from chppDispatchGnssNotification().
 *
 * @param clientContext Maintains status for each client instance.
 * @param buf Input data. Cannot be null.
 * @param len Length of input data in bytes.
 */
static void chppGnssMeasurementResultNotification(
    struct ChppGnssClientState *clientContext, uint8_t *buf, size_t len) {
  UNUSED_VAR(clientContext);
  CHPP_LOGI(
      "chppGnssMeasurementResultNotification received data len=%" PRIuSIZE,
      len);

  buf += sizeof(struct ChppAppHeader);
  len -= sizeof(struct ChppAppHeader);

  struct chreGnssDataEvent *chre =
      chppGnssDataEventToChre((struct ChppGnssDataEvent *)buf, len);

  if (chre == NULL) {
    CHPP_LOGE(
        "chppGnssMeasurementResultNotification CHPP -> CHRE conversion failed. "
        "Input len=%" PRIuSIZE,
        len);
  } else {
    gCallbacks->measurementEventCallback(chre);
  }
}

/**
 * Initializes the GNSS client upon an open request from CHRE and responds
 * with the result.
 *
 * @param systemApi CHRE system function pointers.
 * @param callbacks CHRE entry points.
 *
 * @return True if successful. False otherwise.
 */
static bool chppGnssClientOpen(const struct chrePalSystemApi *systemApi,
                               const struct chrePalGnssCallbacks *callbacks) {
  CHPP_DEBUG_ASSERT(systemApi != NULL);
  CHPP_DEBUG_ASSERT(callbacks != NULL);

  gGnssClientContext.opened = false;
  gSystemApi = systemApi;
  gCallbacks = callbacks;

  // Local
  gGnssClientContext.capabilities = CHRE_GNSS_CAPABILITIES_NONE;

  // Wait for discovery to complete for "open" call to succeed
  if (!chppWaitForDiscoveryComplete(gGnssClientContext.client.appContext,
                                    CHPP_GNSS_DISCOVERY_TIMEOUT_MS)) {
    CHPP_LOGE("Timed out waiting to discover CHPP GNSS service");
  } else {
    // Remote
    struct ChppAppHeader *request = chppAllocClientRequestCommand(
        &gGnssClientContext.client, CHPP_GNSS_OPEN);

    if (request == NULL) {
      CHPP_LOG_OOM();
    } else {
      chppSendTimestampedRequestAndWait(&gGnssClientContext.client,
                                        &gGnssClientContext.open, request,
                                        sizeof(*request));
      // gGnssClientContext.opened is now set
    }
  }

  return gGnssClientContext.opened;
}

/**
 * Deinitializes the GNSS client.
 */
static void chppGnssClientClose(void) {
  // Remote
  struct ChppAppHeader *request = chppAllocClientRequestCommand(
      &gGnssClientContext.client, CHPP_GNSS_CLOSE);

  if (request == NULL) {
    CHPP_LOG_OOM();
  } else if (chppSendTimestampedRequestAndWait(&gGnssClientContext.client,
                                               &gGnssClientContext.close,
                                               request, sizeof(*request))) {
    gGnssClientContext.opened = false;
    gGnssClientContext.capabilities = CHRE_GNSS_CAPABILITIES_NONE;
  }
}

/**
 * Retrieves a set of flags indicating the GNSS features supported by the
 * current implementation.
 *
 * @return Capabilities flags.
 */
static uint32_t chppGnssClientGetCapabilities(void) {
#ifdef CHPP_GNSS_DEFAULT_CAPABILITIES
  uint32_t capabilities = CHPP_GNSS_DEFAULT_CAPABILITIES;
#else
  uint32_t capabilities = CHRE_GNSS_CAPABILITIES_NONE;
#endif

  if (gGnssClientContext.capabilities != CHRE_GNSS_CAPABILITIES_NONE) {
    // Result already cached
    capabilities = gGnssClientContext.capabilities;

  } else {
    struct ChppAppHeader *request = chppAllocClientRequestCommand(
        &gGnssClientContext.client, CHPP_GNSS_GET_CAPABILITIES);

    if (request == NULL) {
      CHPP_LOG_OOM();
    } else {
      if (chppSendTimestampedRequestAndWait(&gGnssClientContext.client,
                                            &gGnssClientContext.getCapabilities,
                                            request, sizeof(*request))) {
        // Success. gGnssClientContext.capabilities is now populated
        capabilities = gGnssClientContext.capabilities;
      }
    }
  }

  return capabilities;
}

/**
 * Start/stop/modify the GNSS location session used for clients of the CHRE
 * API.
 *
 * @param enable true to start/modify the session, false to stop the
 *        session. If false, other parameters are ignored.
 * @param minIntervalMs See chreGnssLocationSessionStartAsync()
 * @param minTimeToNextFixMs See chreGnssLocationSessionStartAsync()
 *
 * @return True indicates the request was sent off to the service.
 */

static bool chppGnssClientControlLocationSession(bool enable,
                                                 uint32_t minIntervalMs,
                                                 uint32_t minTimeToNextFixMs) {
  bool result = false;

  struct ChppGnssControlLocationSessionRequest *request =
      chppAllocClientRequestFixed(&gGnssClientContext.client,
                                  struct ChppGnssControlLocationSessionRequest);

  if (request == NULL) {
    CHPP_LOG_OOM();
  } else {
    request->header.command = CHPP_GNSS_CONTROL_LOCATION_SESSION;
    request->params.enable = enable;
    request->params.minIntervalMs = minIntervalMs;
    request->params.minTimeToNextFixMs = minTimeToNextFixMs;

    result = chppSendTimestampedRequestOrFail(
        &gGnssClientContext.client, &gGnssClientContext.controlLocationSession,
        request, sizeof(*request));
  }

  return result;
}

/**
 * Releases the memory held for the location event callback.
 *
 * @param event Location event to be released.
 */
static void chppGnssClientReleaseLocationEvent(
    struct chreGnssLocationEvent *event) {
  CHPP_FREE_AND_NULLIFY(event);
}

/**
 * Start/stop/modify the raw GNSS measurement session used for clients of the
 * CHRE API.
 *
 * @param enable true to start/modify the session, false to stop the
 *        session. If false, other parameters are ignored.
 * @param minIntervalMs See chreGnssMeasurementSessionStartAsync()
 *
 * @return True indicates the request was sent off to the service.
 */

static bool chppGnssClientControlMeasurementSession(bool enable,
                                                    uint32_t minIntervalMs) {
  bool result = false;

  struct ChppGnssControlMeasurementSessionRequest *request =
      chppAllocClientRequestFixed(
          &gGnssClientContext.client,
          struct ChppGnssControlMeasurementSessionRequest);

  if (request == NULL) {
    CHPP_LOG_OOM();
  } else {
    request->header.command = CHPP_GNSS_CONTROL_MEASUREMENT_SESSION;
    request->params.enable = enable;
    request->params.minIntervalMs = minIntervalMs;

    result = chppSendTimestampedRequestOrFail(
        &gGnssClientContext.client,
        &gGnssClientContext.controlMeasurementSession, request,
        sizeof(*request));
  }

  return result;
}

/**
 * Releases the memory held for the measurement event callback.
 *
 * @param event Measurement event to be released.
 */
static void chppGnssClientReleaseMeasurementDataEvent(
    struct chreGnssDataEvent *event) {
  void *measurements = CHPP_CONST_CAST_POINTER(event->measurements);
  CHPP_FREE_AND_NULLIFY(measurements);
  CHPP_FREE_AND_NULLIFY(event);
}

/**
 * Starts/stops opportunistic delivery of location fixes.
 *
 * @param enable true to turn the passive location listener on, false to
 *        turn it off.
 *
 * @return True indicates the request was sent off to the service.
 */
static bool chppGnssClientConfigurePassiveLocationListener(bool enable) {
  bool result = false;

  struct ChppGnssConfigurePassiveLocationListenerRequest *request =
      chppAllocClientRequestFixed(
          &gGnssClientContext.client,
          struct ChppGnssConfigurePassiveLocationListenerRequest);

  if (request == NULL) {
    CHPP_LOG_OOM();
  } else {
    request->header.command = CHPP_GNSS_CONFIGURE_PASSIVE_LOCATION_LISTENER;
    request->params.enable = enable;

    result = chppSendTimestampedRequestOrFail(
        &gGnssClientContext.client, &gGnssClientContext.passiveLocationListener,
        request, sizeof(*request));
  }

  return result;
}

/************************************************
 *  Public Functions
 ***********************************************/

void chppRegisterGnssClient(struct ChppAppState *appContext) {
  gGnssClientContext.client.appContext = appContext;
  chppRegisterClient(appContext, (void *)&gGnssClientContext,
                     &kGnssClientConfig);
}

void chppDeregisterGnssClient(struct ChppAppState *appContext) {
  // TODO

  UNUSED_VAR(appContext);
}

#ifdef CHPP_CLIENT_ENABLED_GNSS

#ifdef CHPP_CLIENT_ENABLED_CHRE_GNSS
const struct chrePalGnssApi *chrePalGnssGetApi(uint32_t requestedApiVersion) {
#else
const struct chrePalGnssApi *chppPalGnssGetApi(uint32_t requestedApiVersion) {
#endif

  static const struct chrePalGnssApi api = {
      .moduleVersion = CHPP_PAL_GNSS_API_VERSION,
      .open = chppGnssClientOpen,
      .close = chppGnssClientClose,
      .getCapabilities = chppGnssClientGetCapabilities,
      .controlLocationSession = chppGnssClientControlLocationSession,
      .releaseLocationEvent = chppGnssClientReleaseLocationEvent,
      .controlMeasurementSession = chppGnssClientControlMeasurementSession,
      .releaseMeasurementDataEvent = chppGnssClientReleaseMeasurementDataEvent,
      .configurePassiveLocationListener =
          chppGnssClientConfigurePassiveLocationListener,
  };

  CHPP_STATIC_ASSERT(
      CHRE_PAL_GNSS_API_CURRENT_VERSION == CHPP_PAL_GNSS_API_VERSION,
      "A newer CHRE PAL API version is available. Please update.");

  if (!CHRE_PAL_VERSIONS_ARE_COMPATIBLE(api.moduleVersion,
                                        requestedApiVersion)) {
    return NULL;
  } else {
    return &api;
  }
}

#endif
