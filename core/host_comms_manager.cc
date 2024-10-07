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

#include "chre/core/host_comms_manager.h"

#include <cinttypes>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "chre/core/event_loop_common.h"
#include "chre/core/event_loop_manager.h"
#include "chre/platform/assert.h"
#include "chre/platform/context.h"
#include "chre/platform/host_link.h"
#include "chre/platform/log.h"
#include "chre/target_platform/log.h"
#include "chre/util/duplicate_message_detector.h"
#include "chre/util/macros.h"
#include "chre/util/nested_data_ptr.h"
#include "chre/util/optional.h"
#include "chre_api/chre.h"

namespace chre {

namespace {

/**
 * Checks if the message can be send from the nanoapp to the host.
 *
 * @see sendMessageToHostFromNanoapp for a description of the parameters.
 *
 * @return Whether the message can be send to the host.
 */
bool shouldAcceptMessageToHostFromNanoapp(Nanoapp *nanoapp, void *messageData,
                                          size_t messageSize,
                                          uint16_t hostEndpoint,
                                          uint32_t messagePermissions,
                                          bool isReliable) {
  bool success = false;
  if (messageSize > 0 && messageData == nullptr) {
    LOGW("Rejecting malformed message (null data but non-zero size)");
  } else if (messageSize > chreGetMessageToHostMaxSize()) {
    LOGW("Rejecting message of size %zu bytes (max %" PRIu32 ")", messageSize,
         chreGetMessageToHostMaxSize());
  } else if (hostEndpoint == kHostEndpointUnspecified) {
    LOGW("Rejecting message to invalid host endpoint");
  } else if (isReliable && hostEndpoint == kHostEndpointBroadcast) {
    LOGW("Rejecting reliable message to broadcast endpoint");
  } else if (!BITMASK_HAS_VALUE(nanoapp->getAppPermissions(),
                                messagePermissions)) {
    LOGE("Message perms %" PRIx32 " not subset of napp perms %" PRIx32,
         messagePermissions, nanoapp->getAppPermissions());
  } else {
    success = true;
  }

  return success;
}

}  // namespace

HostCommsManager::HostCommsManager()
#ifdef CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED
    : mDuplicateMessageDetector(kReliableMessageDuplicateDetectorTimeout),
      mTransactionManager(
          *this,
          EventLoopManagerSingleton::get()->getEventLoop().getTimerPool(),
          kReliableMessageRetryWaitTime, kReliableMessageMaxAttempts)
#endif  // CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED
{
}

// TODO(b/346345637): rename this to align it with the message delivery status
// terminology used elsewhere, and make it return void
bool HostCommsManager::completeTransaction(
    [[maybe_unused]] uint32_t transactionId,
    [[maybe_unused]] uint8_t errorCode) {
#ifdef CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED
  auto callback = [](uint16_t /*type*/, void *data, void *extraData) {
    uint32_t txnId = NestedDataPtr<uint32_t>(data);
    uint8_t err = NestedDataPtr<uint8_t>(extraData);
    EventLoopManagerSingleton::get()
        ->getHostCommsManager()
        .handleMessageDeliveryStatusSync(txnId, err);
  };
  EventLoopManagerSingleton::get()->deferCallback(
      SystemCallbackType::ReliableMessageEvent,
      NestedDataPtr<uint32_t>(transactionId), callback,
      NestedDataPtr<uint8_t>(errorCode));
  return true;
#else
  return false;
#endif  // CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED
}

void HostCommsManager::removeAllTransactionsFromNanoapp(
    [[maybe_unused]] const Nanoapp &nanoapp) {
#ifdef CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED
  struct FindContext {
    decltype(mTransactionManager) &transactionManager;
    const Nanoapp &nanoapp;
  };

  // Cancel any pending outbound reliable messages. We leverage find() here as
  // a forEach() method by always returning false.
  auto transactionRemover = [](HostMessage *msg, void *data) {
    FindContext *ctx = static_cast<FindContext *>(data);

    if (msg->isReliable && !msg->fromHost &&
        msg->appId == ctx->nanoapp.getAppId() &&
        !ctx->transactionManager.remove(msg->messageSequenceNumber)) {
      LOGE("Couldn't find transaction %" PRIu32 " at flush",
           msg->messageSequenceNumber);
    }

    return false;
  };

  FindContext context{mTransactionManager, nanoapp};
  mMessagePool.find(transactionRemover, &context);
#endif  // CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED
}

void HostCommsManager::freeAllReliableMessagesFromNanoapp(
    [[maybe_unused]] Nanoapp &nanoapp) {
#ifdef CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED
  auto reliableMessageFromNanoappMatcher = [](HostMessage *msg, void *data) {
    auto *napp = static_cast<const Nanoapp *>(data);
    return (msg->isReliable && !msg->fromHost &&
            msg->appId == napp->getAppId());
  };
  MessageToHost *message;
  while ((message = mMessagePool.find(reliableMessageFromNanoappMatcher,
                                      &nanoapp)) != nullptr) {
    // We don't post message delivery status to the nanoapp, since it's being
    // unloaded and we don't actually know the final message delivery status –
    // simply free the memory
    onMessageToHostCompleteInternal(message);
  }
#endif  // CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED
}

void HostCommsManager::flushNanoappMessages(Nanoapp &nanoapp) {
  // First we remove all of the outgoing reliable message transactions from the
  // transaction manager, which triggers sending any pending reliable messages
  removeAllTransactionsFromNanoapp(nanoapp);

  // This ensures that HostLink does not reference message memory (owned the
  // nanoapp) anymore, i.e. onMessageToHostComplete() is called, which lets us
  // free memory for any pending reliable messages
  HostLink::flushMessagesSentByNanoapp(nanoapp.getAppId());
  freeAllReliableMessagesFromNanoapp(nanoapp);
}

// TODO(b/346345637): rename this to better reflect its true meaning, which is
// that HostLink doesn't reference the memory anymore
void HostCommsManager::onMessageToHostComplete(const MessageToHost *message) {
  // We do not call onMessageToHostCompleteInternal for reliable messages
  // until the completion callback is called.
  if (message != nullptr && !message->isReliable) {
    onMessageToHostCompleteInternal(message);
  }
}

void HostCommsManager::resetBlameForNanoappHostWakeup() {
  mIsNanoappBlamedForWakeup = false;
}

bool HostCommsManager::sendMessageToHostFromNanoapp(
    Nanoapp *nanoapp, void *messageData, size_t messageSize,
    uint32_t messageType, uint16_t hostEndpoint, uint32_t messagePermissions,
    chreMessageFreeFunction *freeCallback, bool isReliable,
    const void *cookie) {
  if (!shouldAcceptMessageToHostFromNanoapp(nanoapp, messageData, messageSize,
                                            hostEndpoint, messagePermissions,
                                            isReliable)) {
    return false;
  }

  MessageToHost *msgToHost = mMessagePool.allocate();
  if (msgToHost == nullptr) {
    LOG_OOM();
    return false;
  }

  msgToHost->appId = nanoapp->getAppId();
  msgToHost->message.wrap(static_cast<uint8_t *>(messageData), messageSize);
  msgToHost->toHostData.hostEndpoint = hostEndpoint;
  msgToHost->toHostData.messageType = messageType;
  msgToHost->toHostData.messagePermissions = messagePermissions;
  msgToHost->toHostData.appPermissions = nanoapp->getAppPermissions();
  msgToHost->toHostData.nanoappFreeFunction = freeCallback;
  msgToHost->isReliable = isReliable;
  msgToHost->cookie = cookie;
  msgToHost->fromHost = false;

  bool success = false;
  if (isReliable) {
#ifdef CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED
    success = mTransactionManager.add(nanoapp->getInstanceId(),
                                      &msgToHost->messageSequenceNumber);
#endif  // CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED
  } else {
    success = doSendMessageToHostFromNanoapp(nanoapp, msgToHost);
  }

  if (!success) {
    mMessagePool.deallocate(msgToHost);
  }
  return success;
}

void HostCommsManager::sendMessageToNanoappFromHost(
    uint64_t appId, uint32_t messageType, uint16_t hostEndpoint,
    const void *messageData, size_t messageSize, bool isReliable,
    uint32_t messageSequenceNumber) {
  std::pair<chreError, MessageFromHost *> output =
      validateAndCraftMessageFromHostToNanoapp(
          appId, messageType, hostEndpoint, messageData, messageSize,
          isReliable, messageSequenceNumber);
  chreError error = output.first;
  MessageFromHost *craftedMessage = output.second;

  if (error == CHRE_ERROR_NONE) {
    auto callback = [](uint16_t /*type*/, void *data, void* /* extraData */) {
      MessageFromHost *craftedMessage = static_cast<MessageFromHost *>(data);
      EventLoopManagerSingleton::get()
          ->getHostCommsManager()
          .deliverNanoappMessageFromHost(craftedMessage);
    };

    if (!EventLoopManagerSingleton::get()->deferCallback(
            SystemCallbackType::DeferredMessageToNanoappFromHost,
            craftedMessage, callback)) {
      LOGE("Failed to defer callback to send message to nanoapp from host");
      error = CHRE_ERROR_BUSY;
    }
  }

  if (error != CHRE_ERROR_NONE) {
#ifdef CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED
    if (isReliable) {
      sendMessageDeliveryStatus(messageSequenceNumber, error);
    }
#endif  // CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED

    if (craftedMessage != nullptr) {
      mMessagePool.deallocate(craftedMessage);
    }
  }
}

MessageFromHost *HostCommsManager::craftNanoappMessageFromHost(
    uint64_t appId, uint16_t hostEndpoint, uint32_t messageType,
    const void *messageData, uint32_t messageSize, bool isReliable,
    uint32_t messageSequenceNumber) {
  MessageFromHost *msgFromHost = mMessagePool.allocate();
  if (msgFromHost == nullptr) {
    LOG_OOM();
  } else if (!msgFromHost->message.copy_array(
                 static_cast<const uint8_t *>(messageData), messageSize)) {
    LOGE("Couldn't allocate %" PRIu32
         " bytes for message data from host (endpoint 0x%" PRIx16
         " type %" PRIu32 ")",
         messageSize, hostEndpoint, messageType);
    mMessagePool.deallocate(msgFromHost);
    msgFromHost = nullptr;
  } else {
    msgFromHost->appId = appId;
    msgFromHost->fromHostData.messageType = messageType;
    msgFromHost->fromHostData.messageSize = messageSize;
    msgFromHost->fromHostData.message = msgFromHost->message.data();
    msgFromHost->fromHostData.hostEndpoint = hostEndpoint;
    msgFromHost->isReliable = isReliable;
    msgFromHost->messageSequenceNumber = messageSequenceNumber;
    msgFromHost->fromHost = true;
  }

  return msgFromHost;
}

/**
 * Checks if the message can be send to the nanoapp from the host. Crafts
 * the message to the nanoapp.
 *
 * @see sendMessageToNanoappFromHost for a description of the parameters.
 *
 * @return the error code and the crafted message
 */
std::pair<chreError, MessageFromHost *>
HostCommsManager::validateAndCraftMessageFromHostToNanoapp(
    uint64_t appId, uint32_t messageType, uint16_t hostEndpoint,
    const void *messageData, size_t messageSize, bool isReliable,
    uint32_t messageSequenceNumber) {
  chreError error = CHRE_ERROR_NONE;
  MessageFromHost *craftedMessage = nullptr;

  if (hostEndpoint == kHostEndpointBroadcast) {
    LOGE("Received invalid message from host from broadcast endpoint");
    error = CHRE_ERROR_INVALID_ARGUMENT;
  } else if (messageSize > UINT32_MAX) {
    // The current CHRE API uses uint32_t to represent the message size in
    // struct chreMessageFromHostData. We don't expect to ever need to exceed
    // this, but the check ensures we're on the up and up.
    LOGE("Rejecting message of size %zu (too big)", messageSize);
    error = CHRE_ERROR_INVALID_ARGUMENT;
  } else {
    craftedMessage = craftNanoappMessageFromHost(
        appId, hostEndpoint, messageType, messageData,
        static_cast<uint32_t>(messageSize), isReliable,
        messageSequenceNumber);
    if (craftedMessage == nullptr) {
      LOGE("Out of memory - rejecting message to app ID 0x%016" PRIx64
            "(size %zu)",
            appId, messageSize);
      error = CHRE_ERROR_NO_MEMORY;
    }
  }
  return std::make_pair(error, craftedMessage);
}

void HostCommsManager::deliverNanoappMessageFromHost(
    MessageFromHost *craftedMessage) {
  CHRE_ASSERT_LOG(craftedMessage != nullptr,
                  "Cannot deliver NULL pointer nanoapp message from host");

  Optional<chreError> error;
  uint16_t targetInstanceId;

  bool foundNanoapp = EventLoopManagerSingleton::get()
                          ->getEventLoop()
                          .findNanoappInstanceIdByAppId(craftedMessage->appId,
                                                        &targetInstanceId);
  bool shouldDeliverMessage = !craftedMessage->isReliable ||
                                shouldSendReliableMessageToNanoapp(
                                    craftedMessage->messageSequenceNumber,
                                    craftedMessage->fromHostData.hostEndpoint);
  if (!foundNanoapp) {
    error = CHRE_ERROR_DESTINATION_NOT_FOUND;
  } else if (shouldDeliverMessage) {
    EventLoopManagerSingleton::get()->getEventLoop().distributeEventSync(
        CHRE_EVENT_MESSAGE_FROM_HOST, &craftedMessage->fromHostData,
        targetInstanceId);
    error = CHRE_ERROR_NONE;
  }

  if (craftedMessage->isReliable && error.has_value()) {
    handleDuplicateAndSendMessageDeliveryStatus(
        craftedMessage->messageSequenceNumber,
        craftedMessage->fromHostData.hostEndpoint, error.value());
  }
  mMessagePool.deallocate(craftedMessage);

#ifdef CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED
  mDuplicateMessageDetector.removeOldEntries();
#endif  // CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED
}

bool HostCommsManager::doSendMessageToHostFromNanoapp(
    Nanoapp *nanoapp, MessageToHost *msgToHost) {
  bool hostWasAwake = EventLoopManagerSingleton::get()
                          ->getEventLoop()
                          .getPowerControlManager()
                          .hostIsAwake();
  bool wokeHost = !hostWasAwake && !mIsNanoappBlamedForWakeup;
  msgToHost->toHostData.wokeHost = wokeHost;

  if (!HostLink::sendMessage(msgToHost)) {
    return false;
  }

  if (wokeHost) {
    EventLoopManagerSingleton::get()
        ->getEventLoop()
        .handleNanoappWakeupBuckets();
    mIsNanoappBlamedForWakeup = true;
    nanoapp->blameHostWakeup();
  }
  nanoapp->blameHostMessageSent();
  return true;
}

MessageToHost *HostCommsManager::findMessageToHostBySeq(
    uint32_t messageSequenceNumber) {
  return mMessagePool.find(
      [](HostMessage *inputMessage, void *data) {
        NestedDataPtr<uint32_t> targetMessageSequenceNumber(data);
        return inputMessage->isReliable && !inputMessage->fromHost &&
               inputMessage->messageSequenceNumber ==
                   targetMessageSequenceNumber;
      },
      NestedDataPtr<uint32_t>(messageSequenceNumber));
}

void HostCommsManager::freeMessageToHost(MessageToHost *msgToHost) {
  if (msgToHost->toHostData.nanoappFreeFunction != nullptr) {
    EventLoopManagerSingleton::get()->getEventLoop().invokeMessageFreeFunction(
        msgToHost->appId, msgToHost->toHostData.nanoappFreeFunction,
        msgToHost->message.data(), msgToHost->message.size());
  }
#ifdef CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED
  if (msgToHost->isReliable) {
    mTransactionManager.remove(msgToHost->messageSequenceNumber);
  }
#endif  // CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED
  mMessagePool.deallocate(msgToHost);
}

void HostCommsManager::onTransactionAttempt(uint32_t messageSequenceNumber,
                                            uint16_t nanoappInstanceId) {
  MessageToHost *message = findMessageToHostBySeq(messageSequenceNumber);
  Nanoapp *nanoapp =
      EventLoopManagerSingleton::get()->getEventLoop().findNanoappByInstanceId(
          nanoappInstanceId);
  if (message == nullptr || nanoapp == nullptr) {
    LOGE("Attempted to send reliable message %" PRIu32 " from nanoapp %" PRIu16
         " but couldn't find:%s%s",
         messageSequenceNumber, nanoappInstanceId,
         (message == nullptr) ? " msg" : "",
         (nanoapp == nullptr) ? " napp" : "");
  } else {
    bool success = doSendMessageToHostFromNanoapp(nanoapp, message);
    LOGD("Attempted to send reliable message %" PRIu32 " from nanoapp %" PRIu16
         " with success: %s",
         messageSequenceNumber, nanoappInstanceId, success ? "true" : "false");
  }
}

void HostCommsManager::onTransactionFailure(uint32_t messageSequenceNumber,
                                            uint16_t nanoappInstanceId) {
  LOGE("Reliable message %" PRIu32 " from nanoapp %" PRIu16 " timed out",
       messageSequenceNumber, nanoappInstanceId);
  handleMessageDeliveryStatusSync(messageSequenceNumber, CHRE_ERROR_TIMEOUT);
}

void HostCommsManager::handleDuplicateAndSendMessageDeliveryStatus(
    [[maybe_unused]] uint32_t messageSequenceNumber,
    [[maybe_unused]] uint16_t hostEndpoint,
    [[maybe_unused]] chreError error) {
#ifdef CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED
  bool success = mDuplicateMessageDetector.findAndSetError(
      messageSequenceNumber, hostEndpoint, error);
  if (!success) {
    LOGW("Failed to set error for message with message sequence number: %"
          PRIu32 " and host endpoint: 0x%" PRIx16,
          messageSequenceNumber,
          hostEndpoint);
  }
  sendMessageDeliveryStatus(messageSequenceNumber, error);
#endif  // CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED
}

void HostCommsManager::handleMessageDeliveryStatusSync(
    uint32_t messageSequenceNumber, uint8_t errorCode) {
  EventLoop &eventLoop = EventLoopManagerSingleton::get()->getEventLoop();
  uint16_t nanoappInstanceId;
  MessageToHost *message = findMessageToHostBySeq(messageSequenceNumber);
  if (message == nullptr) {
    LOGW("Got message delivery status for unexpected seq %" PRIu32,
         messageSequenceNumber);
  } else if (!eventLoop.findNanoappInstanceIdByAppId(message->appId,
                                                     &nanoappInstanceId)) {
    // Expected if we unloaded the nanoapp while a message was in flight
    LOGW("Got message delivery status seq %" PRIu32
         " but couldn't find nanoapp 0x%" PRIx64,
         messageSequenceNumber, message->appId);
  } else {
    chreAsyncResult asyncResult = {};
    asyncResult.success = errorCode == CHRE_ERROR_NONE;
    asyncResult.errorCode = errorCode;
    asyncResult.cookie = message->cookie;

    onMessageToHostCompleteInternal(message);
    eventLoop.distributeEventSync(CHRE_EVENT_RELIABLE_MSG_ASYNC_RESULT,
                                  &asyncResult, nanoappInstanceId);
  }
}

void HostCommsManager::onMessageToHostCompleteInternal(
    const MessageToHost *message) {
  // Removing const on message since we own the memory and will deallocate it;
  // the caller (HostLink) only gets a const pointer
  auto *msgToHost = const_cast<MessageToHost *>(message);

  // TODO(b/346345637): add an assertion that HostLink does not own the memory,
  // which is technically possible if a reliable message timed out before it
  // was released

  // If there's no free callback, we can free the message right away as the
  // message pool is thread-safe; otherwise, we need to do it from within the
  // EventLoop context.
  if (msgToHost->toHostData.nanoappFreeFunction == nullptr) {
    mMessagePool.deallocate(msgToHost);
  } else if (inEventLoopThread()) {
    // If we're already within the event loop context, it is safe to call the
    // free callback synchronously.
    freeMessageToHost(msgToHost);
  } else {
    auto freeMsgCallback = [](uint16_t /*type*/, void *data,
                              void * /*extraData*/) {
      EventLoopManagerSingleton::get()->getHostCommsManager().freeMessageToHost(
          static_cast<MessageToHost *>(data));
    };

    if (!EventLoopManagerSingleton::get()->deferCallback(
            SystemCallbackType::MessageToHostComplete, msgToHost,
            freeMsgCallback)) {
      freeMessageToHost(static_cast<MessageToHost *>(msgToHost));
    }
  }
}

bool HostCommsManager::shouldSendReliableMessageToNanoapp(
    [[maybe_unused]] uint32_t messageSequenceNumber,
    [[maybe_unused]] uint16_t hostEndpoint) {
#ifdef CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED
  bool isDuplicate;
  Optional<chreError> pastError = mDuplicateMessageDetector.findOrAdd(
      messageSequenceNumber, hostEndpoint, &isDuplicate);

  if (isDuplicate) {
    bool isTransientFailure =
        pastError.has_value() && (pastError.value() == CHRE_ERROR_BUSY ||
                                  pastError.value() == CHRE_ERROR_TRANSIENT);
    LOGW("Duplicate message with message sequence number: %" PRIu32
         " and host endpoint: 0x%" PRIx16 " was detected. %s",
         messageSequenceNumber, hostEndpoint,
         isTransientFailure ? "Retrying." : "Not sending message to nanoapp.");
    if (!isTransientFailure) {
      if (pastError.has_value()) {
        sendMessageDeliveryStatus(messageSequenceNumber, pastError.value());
      }
      return false;
    }
  }
#endif  // CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED

  return true;
}

}  // namespace chre
