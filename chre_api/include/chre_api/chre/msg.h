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

// IWYU pragma: private, include "chre_api/chre.h"
// IWYU pragma: friend chre/.*\.h

#ifndef _CHRE_MSG_H_
#define _CHRE_MSG_H_

/**
 * @file
 * Context Hub Runtime Environment API dealing with generic endpoint messages.
 *
 * These APIs are used to send and receive messages to and from generic
 * endpoints. This API is a single interface to nanoapps to communicate with
 * other nanoapps (endpoints of CHRE), other embedded endpoints, or host
 * endpoints.
 *
 * Nanoapps should use these APIs to communicate with other nanoapps, local
 * endpoints, or host endpoints instead of chreSendEvent() or any other
 * messaging APIs if they do not need to support Android versions prior to
 * Android 16 nor CHRE APIs older than v1.11.
 *
 * This API uses sessions to organize groups of messages between endpoints.
 * Sessions are created when an endpoint is connected to another endpoint and
 * represent an active connection between the endpoints. Messages are sent
 * between endpoints using the session ID. A session will be automatically
 * closed if an error occurs or an endpoint disconnects.
 *
 * The general order of API usage is:
 *
 * 1. Use one of the query APIs to find an endpoint to communicate with. The
 *    nanoapp may also know the message hub ID and endpoint ID of the endpoint
 *    it wants to communicate with.
 * 2. chreMsgSessionOpenAsync() - to open a session with an endpoint.
 * 3. chreMsgSend() - to send a message to an endpoint.
 * 4. chreMsgSessionCloseAsync() - to close a session with an endpoint.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <chre/common.h>
#include <chre/event.h>
#include <chre/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The type of endpoint.
 * Backing type: uint32_t.
 */
enum chreMsgEndpointType {
  CHRE_MSG_ENDPOINT_TYPE_INVALID = 0,
  CHRE_MSG_ENDPOINT_TYPE_HOST_FRAMEWORK = 1,
  CHRE_MSG_ENDPOINT_TYPE_HOST_APP = 2,
  CHRE_MSG_ENDPOINT_TYPE_HOST_NATIVE = 3,
  CHRE_MSG_ENDPOINT_TYPE_NANOAPP = 4,
  CHRE_MSG_ENDPOINT_TYPE_GENERIC = 5,
};

/**
 * The service RPC format.
 * Backing type: uint32_t.
 */
enum chreMsgEndpointServiceFormat {
  CHRE_MSG_ENDPOINT_SERVICE_FORMAT_INVALID = 0,
  CHRE_MSG_ENDPOINT_SERVICE_FORMAT_CUSTOM = 1,
  CHRE_MSG_ENDPOINT_SERVICE_FORMAT_AIDL = 2,
  CHRE_MSG_ENDPOINT_SERVICE_FORMAT_PW_RPC_PROTOBUF = 3,
};

/**
 * The reason for a session closure event or an endpoint notification
 * event.
 * Backing type: uint8_t.
 */
enum chreMsgEndpointReason {
  CHRE_MSG_ENDPOINT_REASON_UNSPECIFIED = 0,
  CHRE_MSG_ENDPOINT_REASON_OUT_OF_MEMORY = 1,
  CHRE_MSG_ENDPOINT_REASON_TIMEOUT = 2,
  CHRE_MSG_ENDPOINT_REASON_OPEN_ENDPOINT_SESSION_REQUEST_REJECTED = 3,
  CHRE_MSG_ENDPOINT_REASON_CLOSE_ENDPOINT_SESSION_REQUESTED = 4,
  CHRE_MSG_ENDPOINT_REASON_ENDPOINT_INVALID = 5,
  CHRE_MSG_ENDPOINT_REASON_ENDPOINT_GONE = 6,
  CHRE_MSG_ENDPOINT_REASON_ENDPOINT_CRASHED = 7,
  CHRE_MSG_ENDPOINT_REASON_HUB_RESET = 8,
  CHRE_MSG_ENDPOINT_REASON_PERMISSION_DENIED = 9,
};

/**
 * The message hub ID reserved for the Android framework (Context Hub Service)
 */
#define CHRE_MSG_HUB_ID_ANDROID UINT64_C(0x416E64726F696400)

#define CHRE_MSG_HUB_ID_INVALID UINT64_C(0)
#define CHRE_MSG_HUB_ID_RESERVED UINT64_C(-1)
#define CHRE_MSG_ENDPOINT_ID_INVALID UINT64_C(0)
#define CHRE_MSG_ENDPOINT_ID_RESERVED UINT64_C(-1)
#define CHRE_MSG_SESSION_ID_INVALID UINT16_MAX

/**
 * Wildcard hub ID for use with chreMsgConfigureEndpointReadyEvents() and
 * chreMsgConfigureServiceReadyEvents()
 */
#define CHRE_MSG_HUB_ID_ANY CHRE_MSG_HUB_ID_INVALID

/**
 * The maximum length of an endpoint's name.
 */
#define CHRE_MSG_MAX_NAME_LEN (51)

/**
 * The maximum length of a service descriptor.
 */
#define CHRE_MSG_MAX_SERVICE_DESCRIPTOR_LEN (128)

/**
 * @see chreMsgPublishServices
 */
#define CHRE_MSG_MINIMUM_SERVICE_LIMIT UINT8_C(4)

/**
 * Produce an event ID in the block of IDs reserved for session-based messaging.
 *
 * Valid input range is [0, 15]. Do not add new events with ID > 15
 * (see chre/event.h)
 *
 * @param offset Index into MSG event ID block; valid range is [0, 15].
 *
 * @defgroup CHRE_MSG_EVENT_ID
 * @{
 */
#define CHRE_MSG_EVENT_ID(offset) (CHRE_EVENT_MSG_FIRST_EVENT + (offset))

/**
 * nanoappHandleEvent argument: struct chreMsgMessageFromEndpointData
 *
 * The format of the 'message' part of this structure is left undefined,
 * and it's up to the nanoapp and endpoint to have an established protocol
 * beforehand.
 *
 * On receiving the first message from an endpoint, the nanoapp can assume
 * a session with the sessionId has been created and can be used to send
 * messages to the endpoint. The nanoapp will receive a
 * CHRE_EVENT_MSG_SESSION_CLOSED event when the session is closed.
 *
 * @since v1.11
 */
#define CHRE_EVENT_MSG_FROM_ENDPOINT CHRE_MSG_EVENT_ID(0)

/**
 * nanoappHandleEvent argument: struct chreMsgSessionInfo
 *
 * Indicates that a session with an endpoint has been opened.
 *
 * @since v1.11
 */
#define CHRE_EVENT_MSG_SESSION_OPENED CHRE_MSG_EVENT_ID(1)

/**
 * nanoappHandleEvent argument: struct chreMsgSessionInfo
 *
 * Indicates that a session with an endpoint has been closed.
 *
 * @since v1.11
 */
#define CHRE_EVENT_MSG_SESSION_CLOSED CHRE_MSG_EVENT_ID(2)

/**
 * nanoappHandleEvent argument: struct chreMsgEndpointReadyEvent
 *
 * Notifications event regarding a generic endpoint.
 *
 * @see chreConfigureEndpointNotifications
 * @since v1.11
 */
#define CHRE_EVENT_MSG_ENDPOINT_READY CHRE_MSG_EVENT_ID(3)

/**
 * nanoappHandleEvent argument: struct chreMsgServiceReadyEvent
 *
 * Notifications event regarding a generic endpoint with a service.
 *
 * @see chreConfigureEndpointServiceNotifications
 * @since v1.11
 */
#define CHRE_EVENT_MSG_SERVICE_READY CHRE_MSG_EVENT_ID(4)

// NOTE: Do not add new events with ID > 15
/** @} */

/**
 * Provides metadata for an endpoint.
 */
struct chreMsgEndpointInfo {
  /**
   * The message hub ID and endpoint ID of the endpoint.
   */
  uint64_t hubId;
  uint64_t endpointId;

  /**
   * The type of the endpoint. One of chreMsgEndpointType enum values.
   */
  uint32_t type;

  /**
   * The version of the endpoint.
   */
  uint32_t version;

  /**
   * The required permissions of the endpoint, a bitmask of
   * CHRE_MESSAGE_PERMISSION_* values.
   */
  uint32_t requiredPermissions;

  /**
   * The name of the endpoint, an ASCII null-terminated string. This name is
   * specified by the endpoint when it is registered by its message hub.
   */
  char name[CHRE_MSG_MAX_NAME_LEN];
};

/**
 * Provides metadata for an endpoint service.
 */
struct chreMsgServiceInfo {
  /**
   * The major version of the service.
   */
  uint32_t majorVersion;

  /**
   * The minor version of the service.
   */
  uint32_t minorVersion;

  /**
   * The descriptor of the service, and ASCII null-terminated string. This must
   * be valid for the lifetime of the nanoapp.
   */
  const char *serviceDescriptor;

  /**
   * The format of the service. One of chreMsgEndpointServiceFormat enum values.
   */
  uint32_t serviceFormat;
};

/**
 * Data provided with CHRE_EVENT_MSG_SESSION_OPENED,
 * CHRE_EVENT_MSG_SESSION_CLOSED or chreGetSessionInfo().
 */
struct chreMsgSessionInfo {
  /**
   * The message hub ID and endpoint ID of the other party in the session.
   */
  uint64_t hubId;
  uint64_t endpointId;

  /**
   * The descriptor of the service, and ASCII null-terminated string. This
   * will be an empty string if the session was not opened with a service.
   */
  char serviceDescriptor[CHRE_MSG_MAX_SERVICE_DESCRIPTOR_LEN];

  /**
   * The ID of the session.
   */
  uint16_t sessionId;

  /**
   * The reason for the event. Used for sessions closure. For all other uses,
   * this value will be CHRE_MSG_ENDPOINT_REASON_UNSPECIFIED. One of
   * chreMsgEndpointReason enum values.
   */
  uint8_t reason;
};

/**
 * Data provided with CHRE_EVENT_MSG_FROM_ENDPOINT.
 */
struct chreMsgMessageFromEndpointData {
  /**
   * Message type supplied by the endpoint.
   */
  uint32_t messageType;

  /**
   * Message permissions supplied by the endpoint. The format is specified by
   * the CHRE_MESSAGE_PERMISSION_* values if the endpoint is a nanoapp, else
   * it is specified by the endpoint. These permissions are enforced by CHRE.
   * A nanoapp without the required permissions will not receive the message.
   */
  uint32_t messagePermissions;

  /**
   * The message from the endpoint.
   *
   * These contents are of a format that the endpoint and nanoapp must have
   * established beforehand.
   *
   * This data is 'messageSize' bytes in length.  Note that if 'messageSize'
   * is 0, this might contain NULL.
   */
  const void *message;

  /**
   * The size, in bytes of the following 'message'.
   *
   * This can be 0.
   */
  size_t messageSize;

  /**
   * The session ID of the message. A session is the active connection between
   * two endpoints. The receiving nanoapp or endpoint initiated the session
   * before sending this message. If the nanoapp has not yet received a
   * message with this session ID, it can assume the session was created by
   * the nanoapp or other endpoint. The nanoapp may send messages to the other
   * endpoint with this session ID.
   */
  uint16_t sessionId;
};

/**
 * Data provided in CHRE_EVENT_MSG_ENDPOINT_READY.
 */
struct chreMsgEndpointReadyEvent {
  /**
   * The message hub ID and endpoint ID of the endpoint.
   */
  uint64_t hubId;
  uint64_t endpointId;
};

/**
 * Data provided in CHRE_EVENT_MSG_SERVICE_READY.
 */
struct chreMsgServiceReadyEvent {
  /**
   * The message hub ID and endpoint ID of the endpoint.
   */
  uint64_t hubId;
  uint64_t endpointId;

  /**
   * The descriptor of the service, and ASCII null-terminated string. This
   * will be an empty string if the session was not opened with a service.
   */
  char serviceDescriptor[CHRE_MSG_MAX_SERVICE_DESCRIPTOR_LEN];
};

/**
 * Retrieves metadata for a given endpoint.
 *
 * If the given message hub ID and endpoint ID are not associated with a valid
 * endpoint, this method will return false and info will not be populated.
 *
 * @param hubId The message hub ID of the endpoint for which to get info.
 * @param endpointId The endpoint ID of the endpoint for which to get info.
 * @param info The non-null pointer to where the metadata will be stored.
 *
 * @return true if info has been successfully populated.
 *
 * @since v1.11
 */
bool chreMsgGetEndpointInfo(uint64_t hubId, uint64_t endpointId,
                            struct chreMsgEndpointInfo *info);

/**
 * Configures whether this nanoapp will receive updates regarding an
 * endpoint that is connected with a message hub and a specific service.
 * The hubId can be CHRE_MSG_HUB_ID_ANY to configure notifications
 * for all endpoints that are connected with any message hub. The endpoint ID
 * can be CHRE_MSG_ENDPOINT_ID_INVALID to configure notifications for all
 * endpoints that match the given hub.
 *
 * If this API succeeds, the nanoapp will receive endpoint
 * notifications, via the CHRE_EVENT_MSG_ENDPOINT_READY event with an
 * eventData of type chreMsgEndpointReadyEvent.
 *
 * If one or more endpoints matching the filter are already ready when this
 * function is called, CHRE_EVENT_MSG_ENDPOINT_READY will be immediately
 * posted to this nanoapp.
 *
 * @param hubId The message hub ID of the endpoint for which to configure
 * notifications for all endpoints that are connected with any message hub.
 * @param endpointId The endpoint ID of the endpoint for which to configure
 * notifications.
 * @param enable true to enable notifications.
 *
 * @return true on success
 *
 * @since v1.11
 */
bool chreMsgConfigureEndpointReadyEvents(uint64_t hubId, uint64_t endpointId,
                                         bool enable);

/**
 * Configures whether this nanoapp will receive updates regarding all
 * endpoints that are connected with the message hub that provide the specified
 * service.
 *
 * If this API succeeds, the nanoapp will receive endpoint
 * notifications, via the CHRE_EVENT_MSG_SERVICE_READY event
 * with an eventData of type chreMsgServiceReadyEvent.
 *
 * If one or more endpoints matching the filter are already ready when this
 * function is called, CHRE_EVENT_MSG_SERVICE_READY will be
 * immediately posted to this nanoapp.
 *
 * @param hubId The message hub ID of the endpoint for which to configure
 * notifications for all endpoints that are connected with any message hub.
 * @param serviceDescriptor The descriptor of the service associated with the
 * endpoint for which to configure notifications, a null-terminated ASCII
 * string. If not NULL, the underlying memory must outlive the notifications
 * configuration. If NULL, this will return false.
 * @param enable true to enable notifications.
 *
 * @return true on success
 *
 * @see chreMsgConfigureEndpointReadyEvents
 *
 * @since v1.11
 */
bool chreMsgConfigureServiceReadyEvents(uint64_t hubId,
                                        const char *serviceDescriptor,
                                        bool enable);

/**
 * Retrieves metadata for a given session ID. Sessions represent an active
 * connection between a nanoapp and an endpoint. The nanoapp will use this
 * session ID to send messages to the endpoint.
 *
 * If the given session ID is not associated with a valid session or if the
 * caller nanoapp is not a participant in the session, this method
 * will return false and info will not be populated.
 *
 * @param sessionId The session ID of the session for which to get info.
 * @param info The non-null pointer to where the metadata will be stored.
 *
 * @return true if info has been successfully populated.
 *
 * @since v1.11
 */
bool chreMsgSessionGetInfo(uint16_t sessionId, struct chreMsgSessionInfo *info);

/**
 * Publishes services from this nanoapp.
 *
 * When this API is invoked, the list of services will be provided to
 * host applications and endpoints interacting with the nanoapp.
 *
 * This function must be invoked from nanoappStart(), to guarantee stable output
 * of the list of services supported by the nanoapp.
 *
 * Although nanoapps are recommended to only call this API once with all
 * services it intends to publish, if it is called multiple times, each
 * call will append to the list of published services.
 *
 * The implementation must allow for a nanoapp to publish at least
 * CHRE_MSG_MINIMUM_SERVICE_LIMIT services and at most UINT8_MAX services. If
 * calling this function would result in exceeding the limit, the services must
 * not be published and it must return false.
 *
 * @param services A non-null pointer to the list of services to publish.
 * @param numServices The number of services to publish, i.e. the length of the
 * services array.
 *
 * @return true if the publishing is successful.
 *
 * @since v1.11
 */
bool chreMsgPublishServices(const struct chreMsgServiceInfo *services,
                            size_t numServices);

/**
 * Opens a session with an endpoint.
 *
 * The nanoapp will receive a CHRE_EVENT_MSG_SESSION_OPENED event or a
 * CHRE_EVENT_MSG_SESSION_CLOSED event upon the response from the other
 * hub and endpoint. The event may have a session ID of UINT16_MAX if the
 * session could not be opened before sending any request to the other hub and
 * endpoint, i.e. if the given message hub ID and endpoint ID are not associated
 * with a valid endpoint.
 *
 * @param hubId The message hub ID of the endpoint. Can be
 * CHRE_MSG_HUB_ID_ANY to open a session with the default endpoint.
 * @param endpointId The endpoint ID of the endpoint. Can be
 * CHRE_MSG_ENDPOINT_ID_INVALID to open a session with a specified service. The
 * service cannoe be NULL in this case.
 * @param serviceDescriptor The descriptor of the service associated with the
 * endpoint with which to open the session, a null-terminated ASCII
 * string. Can be NULL. The underlying memory must outlive the session.
 *
 * @return whether the request was successfully processed.
 *
 * @since v1.11
 */
bool chreMsgSessionOpenAsync(uint64_t hubId, uint64_t endpointId,
                             const char *serviceDescriptor);

/**
 * Closes a session with an endpoint.
 *
 * If the given session ID is not associated with a valid session or if the
 * caller nanoapp is not a participant in the session, this method
 * will return false.
 *
 * The nanoapp will receive a CHRE_EVENT_MSG_SESSION_CLOSED event upon
 * successful closure of the session.
 *
 * @param sessionId The session ID of the session to close.
 *
 * @return true if the session was successfully closed.
 *
 * @since v1.11
 */
bool chreMsgSessionCloseAsync(uint16_t sessionId);

/**
 * Send a reliable message to an endpoint.
 *
 * This function is similar to sending a reliable message using
 * chreSendReliableMessageAsync() with the difference that the message can be
 * sent to any registered endpoint and the nanoapp will not receive the
 * CHRE_EVENT_RELIABLE_MSG_ASYNC_RESULT event. Instead, when the message is
 * successfully processed by the receiving message hub and endpoint, the
 * freeCallback will be invoked. If the receiving message hub sends the message
 * further, i.e. in the case of the host message hub, if the message will be
 * sent reliably. A failure in reliable message delivery will be indicated
 * by the corresponding session closing.
 *
 * @see chreSendReliableMessageAsync
 *
 * @since v1.11
 */
bool chreMsgSend(void *message, size_t messageSize, uint32_t messageType,
                 uint16_t sessionId, uint32_t messagePermissions,
                 chreMessageFreeFunction *freeCallback);

#ifdef __cplusplus
}
#endif

#endif /* _CHRE_MSG_H_ */
