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

#ifndef CHRE_PLATFORM_EVENT_LOOP_HOOKS_H_
#define CHRE_PLATFORM_EVENT_LOOP_HOOKS_H_

/**
 * @file
 * Include a platform-specific event-loop configuration header if it exists.
 * This header can optionally override the default macros defined below.
 *
 * TODO(b/380327627): Move the conentents of this file to chre/variant/config.h
 * once CHRE team migrates platforms to use the variant config pattern and
 * they address b/376532038.
 */
#ifdef CHRE_PLATFORM_EVENT_LOOP_CONFIG_HEADER
#include CHRE_PLATFORM_EVENT_LOOP_CONFIG_HEADER
#endif  // CHRE_PLATFORM_EVENT_LOOP_CONFIG_HEADER

/**
 * @param eventLoop Pointer to the current EventLoop instance that is used to
 *        deliver events to nanoapps.
 * @param eventType Event type identifier, which implies the type of eventData
 * @param eventData The data that failed to be posted
 * @param freeCallback Function to invoke to when the event has been processed
 *        by all recipients; this must be safe to call immediately, to handle
 *        the case where CHRE is shutting down
 * @param senderInstanceId The instance ID of the sender of this event
 * @param targetInstanceId targetInstanceId The instance ID of the destination
 *        of this event
 * @param targetGroupMask Mask used to limit the recipients that are
 *        registered to receive this event
 *
 * @since v1.11
 * @note FATAL_ERROR is called after this macro is executed
 */
#ifndef CHRE_HANDLE_FAILED_SYSTEM_EVENT_ENQUEUE
#define CHRE_HANDLE_FAILED_SYSTEM_EVENT_ENQUEUE(                     \
    eventLoop, eventType, eventData, freeCallback, senderInstanceId, \
    targetInstanceId, targetGroupMask)                               \
  do {                                                               \
  } while (0)
#endif  // !CHRE_HANDLE_FAILED_SYSTEM_EVENT_ENQUEUE

/**
 * @param eventLoop Pointer to the current EventLoop instance that is used to
 *        deliver events to nanoapps.
 * @param eventType Event type identifier, which implies the type of eventData
 * @param eventData The data that failed to be posted
 * @param callback Function to invoke from the context of the CHRE thread
 * @param extraData Additional arbitrary data to provide to the callback
 *
 * @since v1.11
 * @note FATAL_ERROR is called after this macro is executed
 */
#ifndef CHRE_HANDLE_EVENT_QUEUE_FULL_DURING_SYSTEM_POST
#define CHRE_HANDLE_EVENT_QUEUE_FULL_DURING_SYSTEM_POST(  \
    eventLoop, eventType, eventData, callback, extraData) \
  do {                                                    \
  } while (0)
#endif  // !CHRE_HANDLE_EVENT_QUEUE_FULL_DURING_SYSTEM_POST

/**
 * @param eventLoop Pointer to the current EventLoop instance that is used to
 *        deliver events to nanoapps.
 * @param eventType Event type identifier, which implies the type of eventData
 * @param eventData The data that failed to be posted
 * @param freeCallback Function to invoke to when the event has been processed
 *        by all recipients; this must be safe to call immediately, to handle
 *        the case where CHRE is shutting down
 * @param senderInstanceId The instance ID of the sender of this event
 * @param targetInstanceId targetInstanceId The instance ID of the destination
 *        of this event
 * @param targetGroupMask Mask used to limit the recipients that are
 *        registered to receive this event
 *
 * @since v1.11
 * @note Upon return, the freeCallaback will be invoked if not nullptr.
 */
#ifndef CHRE_HANDLE_LOW_PRIORITY_ENQUEUE_FAILURE
#define CHRE_HANDLE_LOW_PRIORITY_ENQUEUE_FAILURE(                    \
    eventLoop, eventType, eventData, freeCallback, senderInstanceId, \
    targetInstanceId, targetGroupMask)                               \
  do {                                                               \
  } while (0)
#endif  // !CHRE_HANDLE_LOW_PRIORITY_ENQUEUE_FAILURE

#endif  // CHRE_PLATFORM_EVENT_LOOP_HOOKS_H_
