/*
 * Copyright (C) 2016 The Android Open Source Project
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

#ifndef CHRE_PAL_GNSS_H
#define CHRE_PAL_GNSS_H

/**
 * @file
 * Defines the interface between the common CHRE core system and the
 * platform-specific GNSS module. This API is largely asynchronous - any
 * implementation must be able to handle multiple outstanding requests to
 * asynchronous APIs such as controlLocationSession() under reasonable resource
 * constraints. Requests to the same API and their associated responses must be
 * handled strictly in-order. Refer to {@link #chreAsyncResult} for more
 * information.
 */

#include "chre_api/chre/common.h"
#include "chre_api/chre/gnss.h"
#include "chre/pal/version.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initial version of the CHRE GNSS PAL, tied to CHRE API v1.1.
 */
#define CHRE_PAL_GNSS_API_V1_0  CHRE_PAL_CREATE_API_VERSION(1, 0)

/**
 * The version of the CHRE GNSS PAL defined in this header file.
 */
#define CHRE_PAL_GNSS_API_CURRENT_VERSION  CHRE_PAL_GNSS_API_V1_0

struct chrePalGnssCallbacks {
    /**
     * This function can be used by the PAL module to request that the core CHRE
     * system re-send requests for any active sessions. For example, if the GNSS
     * subsystem has recovered from a system crash, this function can be used to
     * silently (from the client's perspective) restore open sessions.
     */
    void (*requestStateResync)(void);

    /**
     * Callback invoked to inform the CHRE of the result of changes to the
     * location session status requested via controlLocationSession in struct
     * chrePalGnssApi.
     *
     * Unsolicited calls to this function must not be made. In other words,
     * this callback should only be invoked as the direct result of an earlier
     * call to controlLocationSession. If the location session is terminated on
     * the remote end, then requestStateResync() should be used if it is due to
     * a recoverable condition, otherwise the PAL should leave the location
     * session in the "enabled" state even though it does not expect to deliver
     * new location events.
     *
     * @param enabled true if the location session is currently active, false
     *        otherwise
     * @param errorCode An error code from enum chreError
     *
     * @see #controlLocationSession
     * @see #chreError
     */
    void (*locationStatusChangeCallback)(bool enabled, uint8_t errorCode);

    /**
     * Callback used to pass GNSS location fixes to the core CHRE system, which
     * distributes it to clients (nanoapps). These events are only delivered
     * while a location session is active, i.e. locationStatusChangeCallback was
     * previously invoked with enabled=true.
     *
     * This function call passes ownership of the event memory to the core CHRE
     * system, i.e. the PAL module must not modify the referenced data until the
     * associated API function is called to release the memory.
     *
     * @param event Event data to distribute to clients. The GNSS module
     *        must ensure that this memory remains accessible until it is passed
     *        to the releaseLocationEvent function in struct chrePalGnssApi.
     */
    void (*locationEventCallback)(struct chreGnssLocationEvent *event);

    /**
     * Callback invoked to inform the CHRE of the result of changes to the raw
     * GNSS measurement session status requested via controlMeasurementSession
     * in struct chrePalGnssApi.
     *
     * Unsolicited calls to this function must not be made. See
     * locationStatusChangeCallback() for more information.
     *
     * @param enabled true if the measurement session is currently active, false
     *        otherwise
     * @param errorCode An error code from enum chreError
     */
    void (*measurementStatusChangeCallback)(bool enabled, uint8_t errorCode);

    /**
     * Callback used to pass raw GNSS measurement data from the GNSS module to
     * the core CHRE system, which distributes it to clients (nanoapps).
     *
     * This function call passes ownership of the event memory to the core CHRE
     * system, i.e. the PAL module must not modify the referenced data until the
     * associated API function is called to release the memory.
     *
     * @param event Event data to distribute to clients. The GNSS module
     *        must ensure that this memory remains accessible until it is passed
     *        to the releaseMeasurementDataEvent() function in struct
     *        chrePalGnssApi.
     */
    void (*measurementEventCallback)(struct chreGnssDataEvent *event);
};

struct chrePalGnssApi {
    /**
     * Version of the module providing this API. This value should be
     * constructed from CHRE_PAL_CREATE_MODULE_VERSION using the supported
     * API version constant (CHRE_PAL_GNSS_API_*) and the module-specific patch
     * version.
     */
    uint32_t moduleVersion;

    /**
     * Initializes the GNSS module. Initialization must complete synchronously.
     *
     * @param callbacks Structure containing entry points to the core CHRE
     *        system. The module does not need to deep-copy this structure; its
     *        memory remains accessible at least until after close() is called.
     *
     * @return true if initialization was successful, false otherwise
     */
    bool (*open)(const struct chrePalGnssCallbacks *callbacks);

    /**
     * Performs clean shutdown of the GNSS module, usually done in preparation
     * for stopping the CHRE. The GNSS module must end any active sessions,
     * ensure that it will not invoke any callbacks past this point, and
     * complete any relevant teardown activities before returning from this
     * function.
     */
    void (*close)(void);

    /**
     * Retrieves information about the features supported by this module. The
     * value returned from this function must not change for the duration of
     * execution.
     *
     * @return See chreGnssGetCapabilities()
     *
     * @see chreGnssGetCapabilities()
     */
    uint32_t (*getCapabilities)(void);

    /**
     * Start/stop/modify the GNSS location session used for clients of the CHRE
     * API.
     *
     * @param enable true to start/modify the session, false to stop the
     *        session. If false, other parameters are ignored.
     * @param minIntervalMs See chreGnssLocationSessionStartAsync()
     * @param minTimeToNextFixMs See chreGnssLocationSessionStartAsync()
     *
     * @return true if the request was accepted for further processing, in which
     *         case its result will be indicated via a call to the location
     *         session status change callback
     *
     * @see chreGnssLocationSessionStartAsync()
     * @see chreGnssLocationSessionStopAsync()
     */
    bool (*controlLocationSession)(
        bool enable, uint32_t minIntervalMs, uint32_t minTimeToNextFixMs);

    /**
     * Invoked when the core CHRE system no longer needs a location event
     * structure that was provided to it via locationEventCallback(). The GNSS
     * module may use this to free associated memory, etc.
     */
    void (*releaseLocationEvent)(struct chreGnssLocationEvent *event);

    /**
     * Start/stop/modify the raw GNSS measurement session used for clients of
     * the CHRE API.
     *
     * @param enable true to start/modify the session, false to stop the
     *        session. If false, other parameters are ignored.
     * @param minIntervalMs See chreGnssMeasurementSessionStartAsync()
     *
     * @return true if the request was accepted for further processing, in which
     *         case its result will be indicated via a call to the measurement
     *         session status change callback.
     *
     * @see chreGnssMeasurementSessionStartAsync()
     * @see chreGnssMeasurementSessionStopAsync()
     */
    bool (*controlMeasurementSession)(
        bool enable, uint32_t minIntervalMs);

    /**
     * Invoked when the core CHRE system no longer needs a raw measurement event
     * structure that was provided to it via measurementEventCallback(). The
     * GNSS module may use this to free associated memory, etc.
     *
     * @param event Event data to release
     */
    void (*releaseMeasurementDataEvent)(struct chreGnssDataEvent *event);
};

/**
 * Retrieve a handle for the CHRE GNSS PAL.
 *
 * @param requestedApiVersion The implementation of this function must return a
 *        pointer to a structure with the same major version as requested.
 *
 * @return Pointer to API handle, or NULL if a compatible API version is not
 *         supported by the module, or the API as a whole is not implemented. If
 *         non-NULL, the returned API handle must be valid as long as this
 *         module is loaded.
 */
const struct chrePalGnssApi *chrePalGnssGetApi(uint32_t requestedApiVersion);

#ifdef __cplusplus
}
#endif

#endif  // CHRE_PAL_GNSS_H
