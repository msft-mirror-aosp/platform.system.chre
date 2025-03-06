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

#ifndef CHPP_VENDOR_CLIENTS_H_
#define CHPP_VENDOR_CLIENTS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "chpp/app.h"
#include "chpp/macros.h"

#ifdef __cplusplus
extern "C" {
#endif

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
void chppRegisterVendorClients(struct ChppAppState *context);

/**
 * Deregisters vendor specific clients with the CHPP app layer.
 * These services are enabled by CHPP_CLIENT_ENABLED_VENDOR definition.
 * This function is automatically called by chppAppDeinit().
 *
 * @param context State of the app layer.
 */
void chppDeregisterVendorClients(struct ChppAppState *context);

#ifdef __cplusplus
}
#endif

#endif  // CHPP_VENDOR_CLIENTS_H_