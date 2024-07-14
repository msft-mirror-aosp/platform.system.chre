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

#include "chre/util/duplicate_message_detector.h"
#include "chre/util/lock_guard.h"

#include <cstdint>

namespace chre {

Optional<chreError> DuplicateMessageDetector::findOrAdd(
    uint32_t messageSequenceNumber, uint16_t hostEndpoint) {
  LockGuard<Mutex> lock(mMutex);

  DuplicateMessageDetector::ReliableMessageRecord *record =
      findLocked(messageSequenceNumber, hostEndpoint);
  if (record == nullptr) {
    record = addLocked(messageSequenceNumber, hostEndpoint);
    if (record == nullptr) {
      return Optional<chreError>();
    }
  }
  return record->error;
}

bool DuplicateMessageDetector::findAndSetError(
    uint32_t messageSequenceNumber, uint16_t hostEndpoint,
    chreError error) {
  LockGuard<Mutex> lock(mMutex);

  DuplicateMessageDetector::ReliableMessageRecord *record =
      findLocked(messageSequenceNumber, hostEndpoint);
  if (record == nullptr) {
    return false;
  }

  record->error = error;
  return true;
}

void DuplicateMessageDetector::removeOldEntries() {
  LockGuard<Mutex> lock(mMutex);

  Nanoseconds now = SystemTime::getMonotonicTime();
  while (!mReliableMessageRecordQueue.empty()) {
    ReliableMessageRecord &record = mReliableMessageRecordQueue.top();
    if (record.timestamp + kTimeout <= now) {
      mReliableMessageRecordQueue.pop();
    } else {
      break;
    }
  }
}

DuplicateMessageDetector::ReliableMessageRecord*
    DuplicateMessageDetector::addLocked(
        uint32_t messageSequenceNumber,
        uint16_t hostEndpoint) {
  bool success = mReliableMessageRecordQueue.push(
      ReliableMessageRecord{
          .timestamp = SystemTime::getMonotonicTime(),
          .messageSequenceNumber = messageSequenceNumber,
          .hostEndpoint = hostEndpoint,
          .error = Optional<chreError>()});
  return success
      ? findLocked(messageSequenceNumber, hostEndpoint, /* reverse= */ true)
      : nullptr;
}

DuplicateMessageDetector::ReliableMessageRecord*
  DuplicateMessageDetector::findLocked(uint32_t messageSequenceNumber,
                                       uint16_t hostEndpoint,
                                       bool reverse) {
  for (size_t i = 0; i < mReliableMessageRecordQueue.size(); ++i) {
    size_t index = reverse ? mReliableMessageRecordQueue.size() - i - 1 : i;
    ReliableMessageRecord &record = mReliableMessageRecordQueue[index];
    if (record.messageSequenceNumber == messageSequenceNumber &&
        record.hostEndpoint == hostEndpoint) {
      return &record;
    }
  }
  return nullptr;
}

}  // namespace chre
