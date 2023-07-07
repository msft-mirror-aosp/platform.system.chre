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

#ifndef _CHRE_NANOAPP_H_
#define _CHRE_NANOAPP_H_

/**
 * @file
 * Methods in the Context Hub Runtime Environment which must be implemented
 * by the nanoapp.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Method invoked by the CHRE when loading the nanoapp.
 *
 * Every CHRE method is legal to call from this method.
 *
 * @return  'true' if the nanoapp successfully started.  'false' if the nanoapp
 *     failed to properly initialize itself (for example, could not obtain
 *     sufficient memory from the heap).  If this method returns 'false', the
 *     nanoapp will be unloaded by the CHRE (and nanoappEnd will
 *     _not_ be invoked in that case).
 * @see nanoappEnd
 */
bool nanoappStart(void);

/**
 * Method invoked by the CHRE when there is an event for this nanoapp.
 *
 * Every CHRE method is legal to call from this method.
 *
 * @param senderInstanceId  The Instance ID for the source of this event.
 *     Note that this may be CHRE_INSTANCE_ID, indicating that the event
 *     was generated by the CHRE.
 * @param eventType  The event type.  This might be one of the CHRE_EVENT_*
 *     types defined in this API.  But it might also be a user-defined event.
 * @param eventData  The associated data, if any, for this specific type of
 *     event.  From the nanoapp's perspective, this eventData's lifetime ends
 *     when this method returns, and thus any data the nanoapp wishes to
 *     retain must be copied.  Note that interpretation of event data is
 *     given by the event type, and for some events may not be a valid
 *     pointer.  See documentation of the specific CHRE_EVENT_* types for how to
 *     interpret this data for those.  Note that for user events, you will
 *     need to establish what this data means.
 */
void nanoappHandleEvent(uint32_t senderInstanceId, uint16_t eventType,
                        const void *eventData);

/**
 * Method invoked by the CHRE when unloading the nanoapp.
 *
 * It is not valid to attempt to send events or messages, or to invoke functions
 * which will generate events to this app, within the nanoapp implementation of
 * this function.  That means it is illegal for the nanoapp invoke any of the
 * following:
 *
 * - chreSendEvent()
 * - chreSendMessageToHost()
 * - chreSensorConfigure()
 * - chreSensorConfigureModeOnly()
 * - chreTimerSet()
 * - etc.
 *
 * @see nanoappStart
 */
void nanoappEnd(void);


#ifdef __cplusplus
}
#endif

#endif  /* _CHRE_NANOAPP_H_ */
