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

#include <cinttypes>

#include "chre/util/macros.h"
#include "chre/util/nanoapp/log.h"
#include "chre_api/chre.h"

#define LOG_TAG "[MsgWorld]"

#ifdef CHRE_NANOAPP_INTERNAL
namespace chre {
namespace {
#endif  // CHRE_NANOAPP_INTERNAL

namespace {

enum MessageType { kDefault = 1, kCustomReplyMessageSize = 2 };

#ifdef CHRE_RELIABLE_MESSAGE_SUPPORT_ENABLED
constexpr uint32_t gMaxReplyMessageSize = CHRE_LARGE_PAYLOAD_MAX_SIZE;
#else
constexpr uint32_t gMaxReplyMessageSize = CHRE_MESSAGE_TO_HOST_MAX_SIZE;
#endif

uint8_t gMessageData[gMaxReplyMessageSize] = {};

void messageFreeCallback(void *message, size_t messageSize) {
  LOGI("Got message free callback for message @ %p (%s) size %zu", message,
       (message == gMessageData) ? "matched" : "unmatched", messageSize);
  if (!chreSendEvent(CHRE_EVENT_FIRST_USER_VALUE, /* eventData= */ nullptr,
                     /* freeCallback= */ nullptr, chreGetInstanceId())) {
    LOGE("Failed to send event");
  }
}

}  // anonymous namespace

bool nanoappStart() {
  LOGI("App started as instance %" PRIu32, chreGetInstanceId());
  // initialize gMessageData
  for (uint32_t i = 0; i < gMaxReplyMessageSize; ++i) {
    gMessageData[i] = i % 10;
  }
  bool success = chreSendMessageToHostEndpoint(
      gMessageData, sizeof(gMessageData), MessageType::kDefault,
      CHRE_HOST_ENDPOINT_BROADCAST, messageFreeCallback);
  LOGI("Sent message of size %zu to host from start callback: %s",
       sizeof(gMessageData), success ? "success" : "failure");
  return true;
}

void nanoappHandleEvent(uint32_t senderInstanceId, uint16_t eventType,
                        const void *eventData) {
  if (eventType != CHRE_EVENT_MESSAGE_FROM_HOST) {
    return;
  }
  auto *msg = static_cast<const chreMessageFromHostData *>(eventData);
  LOGI("Got message from host with type %" PRIu32 " size %" PRIu32
       " data @ %p hostEndpoint 0x%" PRIx16,
       msg->messageType, msg->messageSize, msg->message, msg->hostEndpoint);
  if (senderInstanceId != CHRE_INSTANCE_ID) {
    LOGE("Message from host came from unexpected instance ID %" PRIu32,
         senderInstanceId);
  }

  uint32_t messageSize = gMaxReplyMessageSize;
  if (msg->messageType == MessageType::kCustomReplyMessageSize) {
    messageSize =
        MIN(messageSize, *(static_cast<const uint32_t *>(msg->message)));
  }

  bool success = chreSendMessageToHostEndpoint(
      gMessageData, messageSize, MessageType::kDefault, msg->hostEndpoint,
      messageFreeCallback);
  LOGI("Result of sending reply (size=%" PRIu32 "): %s", messageSize,
       success ? "success" : "failure");
}

void nanoappEnd() {
  LOGI("Stopped");
}

#ifdef CHRE_NANOAPP_INTERNAL
}  // anonymous namespace
}  // namespace chre

#include "chre/platform/static_nanoapp_init.h"
#include "chre/util/nanoapp/app_id.h"
#include "chre/util/system/napp_permissions.h"

CHRE_STATIC_NANOAPP_INIT(MessageWorld, chre::kMessageWorldAppId, 0,
                         chre::NanoappPermissions::CHRE_PERMS_NONE);
#endif  // CHRE_NANOAPP_INTERNAL
