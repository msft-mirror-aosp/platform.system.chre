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

#ifndef CHPP_FAKE_LINK_CLIENTS_H_
#define CHPP_FAKE_LINK_CLIENTS_H_

#include <stdbool.h>
#include <stddef.h>

#include "chpp/app.h"

#ifdef __cplusplus
extern "C" {
#endif

// The timeout at which the test client will trigger a timeout during init.
#define CHPP_TEST_CLIENT_TIMEOUT_MS 500

/************************************************
 *  Public functions
 ***********************************************/

/**
 * Registers vendor specific services with the CHPP app layer.
 * These services are enabled by CHPP_CLIENT_ENABLED_VENDOR definition.
 * This function is automatically called by chppAppInit().
 *
 * @param context State of the app layer.
 */
void chppRegisterTestClient(struct ChppAppState *context);

/**
 * Deregisters vendor specific clients with the CHPP app layer.
 * These services are enabled by CHPP_CLIENT_ENABLED_VENDOR definition.
 * This function is automatically called by chppAppDeinit().
 *
 * @param context State of the app layer.
 */
void chppDeregisterTestClient(struct ChppAppState *context);

/**
 * Waits for a timeout timer to trigger.
 *
 * @param timeoutMs The timeout to wait for the trigger, in milliseconds.
 * @return true if the timeout timer triggered, false otherwise.
 */
bool chppTestClientWaitForTimeout(uint64_t timeoutMs);

#ifdef __cplusplus
}
#endif

#endif  // CHPP_FAKE_LINK_CLIENTS_H_
