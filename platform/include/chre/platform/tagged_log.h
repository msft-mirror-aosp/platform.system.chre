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

#ifndef CHRE_PLATFORM_TAGGED_LOG_H_
#define CHRE_PLATFORM_TAGGED_LOG_H_

/**
 * @file
 * Includes the appropriate platform-specific header file that supplies logging
 * capabilities. The platform header file must supply these symbols, either as
 * macros or free functions:
 *
 *   TLOGE(format, ...)
 *   TLOGW(format, ...)
 *   TLOGI(format, ...)
 *   TLOGD(format, ...)
 *
 * The platform header is recommend to also supply TLOGV for verbose logs,
 * however it is not required.
 *
 * Where "format" is a printf-style format string, and E, W, I, D correspond to
 * the log levels Error, Warning, Informational, and Debug, respectively.
 */

#include "chre/target_platform/tagged_log.h"
#include "chre/util/log_common.h"

#ifndef TLOGE
#error "TLOGE must be defined by chre/target_platform/tagged_log.h"
#endif  // TLOGE

#ifndef TLOGW
#error "TLOGW must be defined by chre/target_platform/tagged_log.h"
#endif  // TLOGW

#ifndef TLOGI
#error "TLOGI must be defined by chre/target_platform/tagged_log.h"
#endif  // TLOGI

#ifndef TLOGD
#error "TLOGD must be defined by chre/target_platform/tagged_log.h"
#endif  // TLOGD

#ifndef TLOGV
// Map TLOGV to TLOGD if the platform doesn't supply it - in that case TLOGV won't
// be distinguished at runtime from TLOGD, but we'll still retain the ability to
// compile out TLOGV based on CHRE_MINIMUM_LOG_LEVEL
#define TLOGV TLOGD
#endif

/*
 * Supply a stub implementation of the LOGx macros when the build is
 * configured with a minimum logging level that is above the requested level.
 */

#ifndef CHRE_MINIMUM_LOG_LEVEL
#error "CHRE_MINIMUM_LOG_LEVEL must be defined"
#endif  // CHRE_MINIMUM_LOG_LEVEL

#if CHRE_MINIMUM_LOG_LEVEL < CHRE_LOG_LEVEL_ERROR
#undef TLOGE
#define TLOGE(format, ...) CHRE_LOG_NULL(format, ##__VA_ARGS__)
#endif

#if CHRE_MINIMUM_LOG_LEVEL < CHRE_LOG_LEVEL_WARN
#undef TLOGW
#define TLOGW(format, ...) CHRE_LOG_NULL(format, ##__VA_ARGS__)
#endif

#if CHRE_MINIMUM_LOG_LEVEL < CHRE_LOG_LEVEL_INFO
#undef TLOGI
#define TLOGI(format, ...) CHRE_LOG_NULL(format, ##__VA_ARGS__)
#endif

#if CHRE_MINIMUM_LOG_LEVEL < CHRE_LOG_LEVEL_DEBUG
#undef TLOGD
#define TLOGD(format, ...) CHRE_LOG_NULL(format, ##__VA_ARGS__)
#endif

#if CHRE_MINIMUM_LOG_LEVEL < CHRE_LOG_LEVEL_VERBOSE
#undef TLOGV
#define TLOGV(format, ...) CHRE_LOG_NULL(format, ##__VA_ARGS__)
#endif

#endif  // CHRE_PLATFORM_TAGGED_LOG_H_
