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

#include "chre/core/event_loop_manager.h"
#include "chre/platform/assert.h"
#include "chre/platform/context.h"
#include "chre/platform/host_link.h"
#include "chre/util/macros.h"
#include "chre/util/nested_data_ptr.h"
#include "chre/util/system/event_callbacks.h"
#include "chre_api/chre.h"

namespace chre {

namespace {

#ifdef CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED
//! @see TransactionManager::DeferCancelCallback
bool deferCancelCallback(uint32_t timerHandle) {
  return EventLoopManagerSingleton::get()->cancelDelayedCallback(timerHandle);
}
#endif  // CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED

/**
 * Checks if the message can be send from the nanoapp to the host.
 *
 * @see sendMessageToHostFromNanoapp for a description of the parameter.
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
    : mTransactionManager(sendMessageWithTransactionData,
                          onMessageDeliveryStatus, deferCallback,
                          deferCancelCallback, kReliableMessageRetryWaitTime,
                          kReliableMessageNumRetries)
#endif  // CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED
    {}

bool HostCommsManager::completeTransaction(uint32_t transactionId,
                                           uint8_t errorCode) {
#ifdef CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED
  return mTransactionManager.completeTransaction(transactionId, errorCode);
#else
  UNUSED_VAR(transactionId);
  UNUSED_VAR(errorCode);
  return false;
#endif  // CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED
}

void HostCommsManager::flushNanoappMessagesAndTransactions(uint64_t appId) {
  uint16_t nanoappInstanceId;
  bool nanoappFound =
      EventLoopManagerSingleton::get()
          ->getEventLoop()
          .findNanoappInstanceIdByAppId(appId, &nanoappInstanceId);
  if (nanoappFound) {
    flushNanoappTransactions(nanoappInstanceId);
  } else {
    LOGE("Could not find nanoapp 0x%016" PRIx64 " to flush transactions",
         appId);
  }

  HostLink::flushMessagesSentByNanoapp(appId);
}

void HostCommsManager::onMessageToHostComplete(const MessageToHost *message) {
  // We do not call onMessageToHostCompleteInternal for reliable messages
  // until the completion callback is called.
  if (message == nullptr || message->isReliable) {
    return;
  }

  onMessageToHostCompleteInternal(message);
}

void HostCommsManager::resetBlameForNanoappHostWakeup() {
  mIsNanoappBlamedForWakeup = false;
}

void HostCommsManager::sendDeferredMessageToNanoappFromHost(
    MessageFromHost *craftedMessage) {
  CHRE_ASSERT_LOG(craftedMessage != nullptr,
                  "Deferred message from host is a NULL pointer");

  if (!deliverNanoappMessageFromHost(craftedMessage)) {
    LOGE("Dropping deferred message; destination app ID 0x%016" PRIx64
         " still not found",
         craftedMessage->appId);
    if (craftedMessage->isReliable) {
      sendMessageDeliveryStatus(craftedMessage->messageSequenceNumber,
                                CHRE_ERROR_DESTINATION_NOT_FOUND);
    }
    mMessagePool.deallocate(craftedMessage);
  } else {
    LOGD("Deferred message to app ID 0x%016" PRIx64 " delivered",
         craftedMessage->appId);
  }
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

  bool success;
  if (isReliable) {
#ifdef CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED
    MessageTransactionData data = {
        .messageSequenceNumberPtr = &msgToHost->messageSequenceNumber,
        .messageSequenceNumber = static_cast<uint32_t>(-1),
        .nanoappInstanceId = nanoapp->getInstanceId(),
        .cookie = cookie,
    };
    success = mTransactionManager.startTransaction(
        data, kReliableMessageTimeout, &msgToHost->messageSequenceNumber);
#else
    UNUSED_VAR(cookie);
    success = false;
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
  if (hostEndpoint == kHostEndpointBroadcast) {
    LOGE("Received invalid message from host from broadcast endpoint");
  } else if (messageSize > ((UINT32_MAX))) {
    // The current CHRE API uses uint32_t to represent the message size in
    // struct chreMessageFromHostData. We don't expect to ever need to exceed
    // this, but the check ensures we're on the up and up.
    LOGE("Rejecting message of size %zu (too big)", messageSize);
  } else {
    MessageFromHost *craftedMessage = craftNanoappMessageFromHost(
        appId, hostEndpoint, messageType, messageData,
        static_cast<uint32_t>(messageSize), isReliable, messageSequenceNumber);
    if (craftedMessage == nullptr) {
      LOGE("Out of memory - rejecting message to app ID 0x%016" PRIx64
           "(size %zu)",
           appId, messageSize);
      if (isReliable) {
        sendMessageDeliveryStatus(messageSequenceNumber, CHRE_ERROR_NO_MEMORY);
      }
    } else if (!deliverNanoappMessageFromHost(craftedMessage)) {
      LOGV("Deferring message; destination app ID 0x%016" PRIx64
           " not found at this time",
           appId);

      auto callback = [](uint16_t /*type*/, void *data, void * /*extraData*/) {
        EventLoopManagerSingleton::get()
            ->getHostCommsManager()
            .sendDeferredMessageToNanoappFromHost(
                static_cast<MessageFromHost *>(data));
      };
      if (!EventLoopManagerSingleton::get()->deferCallback(
              SystemCallbackType::DeferredMessageToNanoappFromHost,
              craftedMessage, callback)) {
        mMessagePool.deallocate(craftedMessage);
      }
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
         " bytes for message data from host "
         "(endpoint 0x%" PRIx16 " type %" PRIu32 ")",
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
  }

  return msgFromHost;
}

bool HostCommsManager::deferCallback(
    TransactionManager<MessageTransactionData,
                       kMaxOutstandingMessages>::DeferCallbackFunction func,
    void *data, void *extraData, Nanoseconds delay, uint32_t *outTimerHandle) {
  if (delay.toRawNanoseconds() == 0) {
    CHRE_ASSERT(outTimerHandle == nullptr);
    return EventLoopManagerSingleton::get()->deferCallback(
        SystemCallbackType::ReliableMessageEvent, data, func, extraData);
  }

  CHRE_ASSERT(outTimerHandle != nullptr);
  *outTimerHandle = EventLoopManagerSingleton::get()->setDelayedCallback(
      SystemCallbackType::ReliableMessageEvent, data, func, delay);
  return true;
}

bool HostCommsManager::deliverNanoappMessageFromHost(
    MessageFromHost *craftedMessage) {
  const EventLoop &eventLoop = EventLoopManagerSingleton::get()->getEventLoop();
  uint16_t targetInstanceId;
  bool nanoappFound = false;

  CHRE_ASSERT_LOG(craftedMessage != nullptr,
                  "Cannot deliver NULL pointer nanoapp message from host");

  if (eventLoop.findNanoappInstanceIdByAppId(craftedMessage->appId,
                                             &targetInstanceId)) {
    nanoappFound = true;
    EventLoopManagerSingleton::get()->getEventLoop().postEventOrDie(
        CHRE_EVENT_MESSAGE_FROM_HOST, &craftedMessage->fromHostData,
        freeMessageFromHostCallback, targetInstanceId);
    if (craftedMessage->isReliable) {
      sendMessageDeliveryStatus(craftedMessage->messageSequenceNumber,
                                CHRE_ERROR_NONE);
    }
  }

  return nanoappFound;
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

HostMessage *HostCommsManager::findMessageByMessageSequenceNumber(
    uint32_t messageSequenceNumber) {
  return mMessagePool.find(
      [](HostMessage *inputMessage, void *data) {
        NestedDataPtr<uint32_t> targetMessageSequenceNumber(data);
        return inputMessage->isReliable &&
               inputMessage->messageSequenceNumber ==
                   targetMessageSequenceNumber;
      },
      NestedDataPtr<uint32_t>(messageSequenceNumber));
}

size_t HostCommsManager::flushNanoappTransactions(uint16_t nanoappInstanceId) {
#ifdef CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED
  return mTransactionManager.flushTransactions(
      [](const MessageTransactionData &data, void *callbackData) {
        NestedDataPtr<uint16_t> innerNanoappInstanceId(callbackData);
        if (innerNanoappInstanceId == data.nanoappInstanceId) {
          HostMessage *message = EventLoopManagerSingleton::get()
                                     ->getHostCommsManager()
                                     .findMessageByMessageSequenceNumber(
                                         data.messageSequenceNumber);
          if (message != nullptr) {
            EventLoopManagerSingleton::get()
                ->getHostCommsManager()
                .onMessageToHostCompleteInternal(message);
          }
          return true;
        }
        return false;
      },
      NestedDataPtr<uint16_t>(nanoappInstanceId));
#else
  UNUSED_VAR(nanoappInstanceId);
  return 0;
#endif  // CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED
}

void HostCommsManager::freeMessageToHost(MessageToHost *msgToHost) {
  if (msgToHost->toHostData.nanoappFreeFunction != nullptr) {
    EventLoopManagerSingleton::get()->getEventLoop().invokeMessageFreeFunction(
        msgToHost->appId, msgToHost->toHostData.nanoappFreeFunction,
        msgToHost->message.data(), msgToHost->message.size());
  }
  mMessagePool.deallocate(msgToHost);
}

void HostCommsManager::freeMessageFromHostCallback(uint16_t /*type*/,
                                                   void *data) {
  // We pass the chreMessageFromHostData structure to the nanoapp as the event's
  // data pointer, but we need to return to the enclosing HostMessage pointer.
  // As long as HostMessage is standard-layout, and fromHostData is the first
  // field, we can convert between these two pointers via reinterpret_cast.
  // These static assertions ensure this assumption is held.
  static_assert(std::is_standard_layout<HostMessage>::value,
                "HostMessage* is derived from HostMessage::fromHostData*, "
                "therefore it must be standard layout");
  static_assert(offsetof(MessageFromHost, fromHostData) == 0,
                "fromHostData must be the first field in HostMessage");

  auto *eventData = static_cast<chreMessageFromHostData *>(data);
  auto *msgFromHost = reinterpret_cast<MessageFromHost *>(eventData);
  auto &hostCommsMgr = EventLoopManagerSingleton::get()->getHostCommsManager();
  hostCommsMgr.mMessagePool.deallocate(msgFromHost);
}

bool HostCommsManager::sendMessageWithTransactionData(
    MessageTransactionData &data) {
  // Set the message sequence number now that TransactionManager has set it.
  // The message should still be available right now, but might not be later,
  // so the pointer could be invalid at a later time.
  data.messageSequenceNumber = *data.messageSequenceNumberPtr;

  HostMessage *message =
      EventLoopManagerSingleton::get()
          ->getHostCommsManager()
          .findMessageByMessageSequenceNumber(data.messageSequenceNumber);
  Nanoapp *nanoapp =
      EventLoopManagerSingleton::get()->getEventLoop().findNanoappByInstanceId(
          data.nanoappInstanceId);
  return nanoapp != nullptr && message != nullptr &&
         EventLoopManagerSingleton::get()
             ->getHostCommsManager()
             .doSendMessageToHostFromNanoapp(nanoapp, message);
}

bool HostCommsManager::onMessageDeliveryStatus(
    const MessageTransactionData &data, uint8_t errorCode) {
  chreAsyncResult *asyncResult = memoryAlloc<chreAsyncResult>();
  if (asyncResult == nullptr) {
    LOG_OOM();
    return false;
  }

  asyncResult->requestType = 0;
  asyncResult->cookie = data.cookie;
  asyncResult->errorCode = errorCode;
  asyncResult->reserved = 0;
  asyncResult->success = errorCode == CHRE_ERROR_NONE;

  EventLoopManagerSingleton::get()->getEventLoop().postEventOrDie(
      CHRE_EVENT_RELIABLE_MSG_ASYNC_RESULT, asyncResult, freeEventDataCallback,
      data.nanoappInstanceId);

  HostMessage *message =
      EventLoopManagerSingleton::get()
          ->getHostCommsManager()
          .findMessageByMessageSequenceNumber(data.messageSequenceNumber);
  if (message != nullptr) {
    EventLoopManagerSingleton::get()
        ->getHostCommsManager()
        .onMessageToHostCompleteInternal(message);
  }

  return true;
}

void HostCommsManager::onMessageToHostCompleteInternal(
    const MessageToHost *message) {
  // Removing const on message since we own the memory and will deallocate it;
  // the caller (HostLink) only gets a const pointer
  auto *msgToHost = const_cast<MessageToHost *>(message);

  // If there's no free callback, we can free the message right away as the
  // message pool is thread-safe; otherwise, we need to do it from within the
  // EventLoop context.
  if (msgToHost->toHostData.nanoappFreeFunction == nullptr) {
    mMessagePool.deallocate(msgToHost);
  } else if (inEventLoopThread()) {
    // If we're already within the event loop context, it is safe to call the
    // free callback synchronously.
    EventLoopManagerSingleton::get()->getHostCommsManager().freeMessageToHost(
        msgToHost);
  } else {
    auto freeMsgCallback = [](uint16_t /*type*/, void *data,
                              void * /*extraData*/) {
      EventLoopManagerSingleton::get()->getHostCommsManager().freeMessageToHost(
          static_cast<MessageToHost *>(data));
    };

    if (!EventLoopManagerSingleton::get()->deferCallback(
            SystemCallbackType::MessageToHostComplete, msgToHost,
            freeMsgCallback)) {
      EventLoopManagerSingleton::get()->getHostCommsManager().freeMessageToHost(
          static_cast<MessageToHost *>(msgToHost));
    }
  }
}

}  // namespace chre
