/*
 * Copyright (C) 2017 The Android Open Source Project
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

#ifndef CHRE_CORE_HOST_COMMS_MANAGER_H_
#define CHRE_CORE_HOST_COMMS_MANAGER_H_

#include <cstddef>
#include <cstdint>

#include "chre/core/nanoapp.h"
#include "chre/core/timer_pool.h"
#include "chre/platform/atomic.h"
#include "chre/platform/host_link.h"
#include "chre/util/buffer.h"
#include "chre/util/duplicate_message_detector.h"
#include "chre/util/non_copyable.h"
#include "chre/util/system/synchronized_memory_pool.h"
#include "chre/util/system/transaction_manager.h"
#include "chre/util/time.h"
#include "chre_api/chre/event.h"

namespace chre {

//! Only valid for messages from host to CHRE - indicates that the sender of the
//! message is not specified.
constexpr uint16_t kHostEndpointUnspecified = CHRE_HOST_ENDPOINT_UNSPECIFIED;

//! Only valid for messages from CHRE to host - delivers the message to all
//! registered clients of the Context Hub HAL, which is the default behavior.
constexpr uint16_t kHostEndpointBroadcast = CHRE_HOST_ENDPOINT_BROADCAST;

/**
 * Data associated with a message either to or from the host.
 */
struct HostMessage : public NonCopyable {
  // This union must be first, as this structure is aliased with
  // chreMessageFromHostData
  union {
    // Fields use when the message was received from the host
    struct chreMessageFromHostData fromHostData;

    // Fields used when the messsage is directed to the host
    struct {
      //! Application-specific message ID
      uint32_t messageType;

      //! List of Android permissions declared by the nanoapp. This must be a
      //! superset of messagePermissions.
      uint32_t appPermissions;

      //! List of Android permissions that cover the contents of the message.
      //! These permissions are used to record and attribute access to
      //! permissions-controlled resources.
      //! Note that these permissions must always be a subset of uint32_t
      //! permissions. Otherwise, the message will be dropped.
      uint32_t messagePermissions;

      //! Message free callback supplied by the nanoapp. Must only be invoked
      //! from the EventLoop where the nanoapp runs.
      chreMessageFreeFunction *nanoappFreeFunction;

      //! Identifier for the host-side entity that should receive this message.
      uint16_t hostEndpoint;

      //! true if this message resulted in the host transitioning from suspend
      //! to awake.
      bool wokeHost;
    } toHostData;
  };

  //! Distinguishes whether this is a message from the host or to the host,
  //! which dictates whether fromHostData or toHostData are used.
  bool fromHost;

  //! Whether the message is reliable.
  //! Reliable messages are acknowledge by sending with a status containing
  //! the transaction ID.
  bool isReliable;

  //! Used to report reliable message status back to the sender.
  uint32_t messageSequenceNumber;

  //! Opaque nanoapp-supplied cookie associated with reliable messages.
  const void *cookie;

  //! Source/destination nanoapp ID.
  uint64_t appId;

  //! Application-defined message data.
  Buffer<uint8_t> message;
};

typedef HostMessage MessageFromHost;
typedef HostMessage MessageToHost;

/**
 * Common code for managing bi-directional communications between the host and
 * nanoapps. Inherits from the platform-specific HostLink class to accomplish
 * this, and also to provide an access point (lookup via the EventLoopManager
 * Singleton) to the platform-specific HostLinkBase functionality for use by
 * platform-specific code.
 */
class HostCommsManager : public HostLink, private TransactionManagerCallback {
 public:
  HostCommsManager();

  /**
   * Completes a reliable message transaction.
   *
   * The callback registered when starting the transaction is called with the
   * errorCode.
   *
   * @param transactionId ID of the transaction to complete.
   * @param errorCode Error code to pass to the callback.
   * @return Whether the transaction was completed successfully.
   */
  bool completeTransaction(uint32_t transactionId, uint8_t errorCode);

  /**
   * Flush (or purge) any messages sent by the given app ID that are currently
   * pending delivery to the host. At the point that this function is called, it
   * is guaranteed that no new messages will be generated from this nanoapp.
   *
   * This function also flushes any outstanding reliable message transactions,
   * by ensuring at least one attempt to send to the host is made, and not
   * providing a message delivery status event to the nanoapp.
   *
   * This function must impose strict ordering constraints, such that after it
   * returns, it is guaranteed that HostCommsManager::onMessageToHostComplete
   * will not be invoked for the app with the given ID.
   */
  void flushNanoappMessages(Nanoapp &nanoapp);

  /**
   * Invoked by the HostLink platform layer when it is done with a message to
   * the host: either it successfully sent it, or encountered an error.
   *
   * This function is thread-safe.
   *
   * @param message A message pointer previously given to HostLink::sendMessage
   */
  void onMessageToHostComplete(const MessageToHost *msgToHost);

  /*
   * Resets mIsNanoappBlamedForWakeup to false so that
   * nanoapp->blameHostWakeup() can be called again on next wakeup for one of
   * the nanoapps.
   */
  void resetBlameForNanoappHostWakeup();

  /**
   * Formulates a MessageToHost using the supplied message contents and
   * passes it to HostLink for transmission to the host.
   *
   * @param nanoapp The sender of this message.
   * @param messageData Pointer to message payload. Can be null if
   * messageSize is 0. This buffer must remain valid until freeCallback is
   * invoked.
   * @param messageSize Size of the message to send, in bytes
   * @param messageType Application-defined identifier for the message
   * @param hostEndpoint Identifier for the entity on the host that should
   *        receive this message
   * @param messagePermissions List of Android permissions that cover the
   *        contents of the message. These permissions are used to record
   * and attribute access to permissions-controlled resources.
   * @param freeCallback Optional callback to invoke when the messageData is
   * no longer needed. The provided callback is only invoked when the return
   *         value is CHRE_ERROR_NONE.
   * @param isReliable Whether the message is reliable. The receiver
   *        acknowledges the delivery of a reliable message by sending a
   * status back to the sender.
   * @param cookie The cookie to use when reporting the async status to the
   *        nanoapp via the CHRE_EVENT_RELIABLE_MSG_ASYNC_STATUS event. Only
   *        used when isReliable is true.
   * @return true if the message was accepted into the outbound message
   * queue. If this function returns false, it does *not* invoke
   * freeCallback. If it returns true, freeCallback will be invoked (if
   * non-null) on either success or failure.
   *
   * @see chreSendMessageToHost
   */
  bool sendMessageToHostFromNanoapp(Nanoapp *nanoapp, void *messageData,
                                    size_t messageSize, uint32_t messageType,
                                    uint16_t hostEndpoint,
                                    uint32_t messagePermissions,
                                    chreMessageFreeFunction *freeCallback,
                                    bool isReliable, const void *cookie);

  /**
   * Makes a copy of the supplied message data and posts it to the queue for
   * later delivery to the addressed nanoapp.
   *
   * This function is safe to call from any thread.
   *
   * @param appId Identifier for the destination nanoapp
   * @param messageType Application-defined message identifier
   * @param hostEndpoint Identifier for the entity on the host that sent this
   *        message
   * @param messageData Buffer containing application-specific message data; can
   *        be null if messageSize is 0
   * @param messageSize Size of messageData, in bytes
   * @param isReliable Whether the message is reliable
   * @param messageSequenceNumber The message sequence number for reliable
   * messages
   */
  void sendMessageToNanoappFromHost(uint64_t appId, uint32_t messageType,
                                    uint16_t hostEndpoint,
                                    const void *messageData, size_t messageSize,
                                    bool isReliable,
                                    uint32_t messageSequenceNumber);

 private:
  //! How many times we'll try sending a reliable message before giving up.
  static constexpr uint16_t kReliableMessageMaxAttempts = 4;

  //! How long we'll wait after sending a reliable message which doesn't receive
  //! an ACK before trying again.
  static constexpr Milliseconds kReliableMessageRetryWaitTime =
      Milliseconds(250);

  //! How long we'll wait before timing out a reliable message.
  static constexpr Nanoseconds kReliableMessageTimeout =
      kReliableMessageRetryWaitTime * kReliableMessageMaxAttempts;

  //! How long we'll wait before removing a duplicate message record from the
  //! duplicate message detector.
  static constexpr Nanoseconds kReliableMessageDuplicateDetectorTimeout =
      kReliableMessageTimeout * 3;

  //! The maximum number of messages we can have outstanding at any given time.
  static constexpr size_t kMaxOutstandingMessages = 32;

  //! Ensures that we do not blame more than once per host wakeup. This is
  //! checked before calling host blame to make sure it is set once. The power
  //! control managers then reset back to false on host suspend.
  AtomicBool mIsNanoappBlamedForWakeup{false};

  //! Memory pool used to allocate message metadata (but not the contents of the
  //! messages themselves). Must be synchronized as the same HostCommsManager
  //! handles communications for all EventLoops, and also to support freeing
  //! messages directly in onMessageToHostComplete.
  SynchronizedMemoryPool<HostMessage, kMaxOutstandingMessages> mMessagePool;

#ifdef CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED
  //! The duplicate message detector for reliable messages.
  DuplicateMessageDetector mDuplicateMessageDetector;

  //! The transaction manager for reliable messages.
  TransactionManager<kMaxOutstandingMessages, TimerPool> mTransactionManager;
#endif  // CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED

  /**
   * Allocates and populates the event structure used to notify a nanoapp of an
   * incoming message from the host.
   *
   * Used to implement sendMessageToNanoappFromHost() - see that
   * function for parameter documentation.
   *
   * All parameters must be sanitized before invoking this function.
   *
   * @see sendMessageToNanoappFromHost
   */
  MessageFromHost *craftNanoappMessageFromHost(
      uint64_t appId, uint16_t hostEndpoint, uint32_t messageType,
      const void *messageData, uint32_t messageSize, bool isReliable,
      uint32_t messageSequenceNumber);

  /**
   * Checks if the message could be sent to the nanoapp from the host. Crafts
   * the message to the nanoapp.
   *
   * @see sendMessageToNanoappFromHost for a description of the parameters.
   *
   * @return the error code and the crafted message. The message is dynamically
   *         allocated and must be freed by the caller.
   */
  std::pair<chreError, MessageFromHost *>
  validateAndCraftMessageFromHostToNanoapp(uint64_t appId, uint32_t messageType,
                                           uint16_t hostEndpoint,
                                           const void *messageData,
                                           size_t messageSize, bool isReliable,
                                           uint32_t messageSequenceNumber);

  /**
   * Posts a crafted event, craftedMessage, to a nanoapp for processing, and
   * deallocates it afterwards.
   *
   * Used to implement sendMessageToNanoappFromHost(). It allocates and
   * populates the event using craftNanoappMessageFromHost().
   *
   * @param craftedMessage Message from host to be delivered to the
   * destination nanoapp
   */
  void deliverNanoappMessageFromHost(MessageFromHost *craftedMessage);

  /**
   * Sends a message to the host from a nanoapp. This method also
   * appropriately blames the nanoapp for sending a message or
   * waking up the host. This function assumes both parameters
   * are non-nullptr.
   *
   * @param nanoapp The sender of this message.
   * @param msgToHost The message to send.
   *
   * @return Whether the message was successfully sent.
   */
  bool doSendMessageToHostFromNanoapp(Nanoapp *nanoapp,
                                      MessageToHost *msgToHost);

  /**
   * Find the message to the host associated with the message sequence number,
   * if it exists. Returns nullptr otherwise.
   *
   * @param messageSequenceNumber The message sequence number.
   * @return The message or nullptr if not found.
   */
  MessageToHost *findMessageToHostBySeq(uint32_t messageSequenceNumber);

  /**
   * Releases memory associated with a message to the host, including invoking
   * the Nanoapp's free callback (if given). Must be called from within the
   * context of the EventLoop that contains the sending Nanoapp.
   *
   * @param msgToHost The message to free
   */
  void freeMessageToHost(MessageToHost *msgToHost);

  /**
   * Callback used to send a reliable message.
   * @see TransactionManagerCallback
   */
  void onTransactionAttempt(uint32_t messageSequenceNumber,
                            uint16_t nanoappInstanceId) final;

  /**
   * Callback invoked when a transaction has timed out after the maximum
   * number of retries.
   * @see TransactionManagerCallback
   */
  void onTransactionFailure(uint32_t messageSequenceNumber,
                            uint16_t nanoappInstanceId) final;

  /**
   * Handles a duplicate message from the host by setting the error in the
   * duplicate message detector and sends a message delivery status to the
   * nanoapp.
   *
   * @param messageSequenceNumber The message sequence number.
   * @param hostEndpoint The host endpoint.
   * @param error The error from sending the message to the nanoapp.
   */
  void handleDuplicateAndSendMessageDeliveryStatus(
      uint32_t messageSequenceNumber, uint16_t hostEndpoint, chreError error);

  /**
   * Called when a reliable message transaction status is reported by the
   * host.
   *
   * The status is delivered to the nanoapp that sent the message by posting a
   * CHRE_EVENT_RELIABLE_MSG_ASYNC_STATUS event.
   *
   * @param data The message transaction data.
   * @param errorCode The errorCode reported by the host from enum chreError.
   * @return Whether the event was posted successfully.
   *
   */
  void handleMessageDeliveryStatusSync(uint32_t messageSequenceNumber,
                                       uint8_t errorCode);

  /**
   * Invoked by onMessageToHostComplete for a non-reliable message
   * or the TransactionManager for a reliable message when either
   * are done with a message to the host.
   *
   * This function is thread-safe.
   *
   * @param message A message pointer previously given to
   * HostLink::sendMessage
   */
  void onMessageToHostCompleteInternal(const MessageToHost *msgToHost);

  /**
   * Calls TransactionManager::remove for all pending reliable messages sent
   * by this nanoapp, normally used as part of nanoapp unload flow.
   */
  void removeAllTransactionsFromNanoapp(const Nanoapp &nanoapp);

  /**
   * Releases memory for all pending reliable messages sent by this nanoapp.
   * The data must have already been flushed through HostLink, and the
   * transactions must have already been cleaned up.
   */
  void freeAllReliableMessagesFromNanoapp(Nanoapp &nanoapp);

  /**
   * Returns whether to send the reliable message to the nanoapp. This
   * function returns true, indicating to the caller to send the message, when
   * the message is not a duplicate or when the duplicate message was sent
   * previously with a transient error. When this function returns false, the
   * error is sent to the host using sendMessageDeliveryStatus.
   *
   * @param messageSequenceNumber The message sequence number.
   * @param hostEndpoint The host endpoint.
   * @return Whether to send the message to the nanoapp.
   */
  bool shouldSendReliableMessageToNanoapp(uint32_t messageSequenceNumber,
                                          uint16_t hostEndpoint);
};

}  // namespace chre

#endif  // CHRE_CORE_HOST_COMMS_MANAGER_H_
