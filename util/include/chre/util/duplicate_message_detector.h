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

#ifndef CHRE_UTIL_DUPLICATE_MESSAGE_DETECTOR_H_
#define CHRE_UTIL_DUPLICATE_MESSAGE_DETECTOR_H_

#include "chre/platform/mutex.h"
#include "chre/platform/system_time.h"
#include "chre/util/non_copyable.h"
#include "chre/util/optional.h"
#include "chre/util/priority_queue.h"
#include "chre/util/time.h"
#include "chre_api/chre.h"

#include <functional>

namespace chre {

/**
 * This class is used to detect duplicate reliable messages. It keeps a record
 * of all reliable messages that have been sent from the host. If a message with
 * the same message sequence number and host endpoint is sent again, it is
 * considered a duplicate. This class is thread-safe.
 *
 * A typical usage of this class would be as follows:
 *
 * Call findOrAdd() to add a new message to the detector. If the message is a
 * duplicate, the detector will return the error code previously recorded.
 * If the message is not a duplicate, the detector will return an empty
 * Optional.
 *
 * Call findAndSetError() to set the error code for a message that has already
 * been added to the detector.
 *
 * Call removeOldEntries() to remove any messages that have been in the detector
 * for longer than the timeout specified in the constructor.
 */
class DuplicateMessageDetector : public NonCopyable {
 public:
  struct ReliableMessageRecord {
    Nanoseconds timestamp;
    uint32_t messageSequenceNumber;
    uint16_t hostEndpoint;
    Optional<chreError> error;

    inline bool operator>(const ReliableMessageRecord &rhs) const {
      return timestamp > rhs.timestamp;
    }
  };

  DuplicateMessageDetector() = delete;
  DuplicateMessageDetector(Nanoseconds timeout):
      kTimeout(timeout) {}
  ~DuplicateMessageDetector() = default;

  //! Finds the message with the given message sequence number and host
  //! endpoint. If the message is not found, a new message is added to the
  //! detector. Returns the error code previously recorded for the message, or
  //! an empty Optional if the message is not a duplicate. If outIsDuplicate is
  //! not nullptr, it will be set to true if the message is a duplicate (was
  //! found), or false otherwise.
  Optional<chreError> findOrAdd(uint32_t messageSequenceNumber,
                                uint16_t hostEndpoint,
                                bool *outIsDuplicate = nullptr);

  //! Sets the error code for a message that has already been added to the
  //! detector. Returns true if the message was found and the error code was
  //! set, or false if the message was not found.
  bool findAndSetError(uint32_t messageSequenceNumber, uint16_t hostEndpoint,
                       chreError error);

  //! Removes any messages that have been in the detector for longer than the
  //! timeout specified in the constructor.
  void removeOldEntries();

 private:
  //! The timeout specified in the constructor. This should be the reliable
  //! message timeout.
  Nanoseconds kTimeout;

  //! The mutex used to protect the queue.
  Mutex mMutex;

  //! The queue of reliable message records.
  PriorityQueue<ReliableMessageRecord, std::greater<ReliableMessageRecord>>
      mReliableMessageRecordQueue;

  //! Adds a new message to the detector. Returns the message record, or nullptr
  //! if the message could not be added. Not thread safe.
  ReliableMessageRecord *addLocked(uint32_t messageSequenceNumber,
                                   uint16_t hostEndpoint);

  //! Finds the message with the given message sequence number and host
  //! endpoint, else returns nullptr. Not thread safe. If reverse is true,
  //! this function searches from the end of the queue.
  ReliableMessageRecord *findLocked(uint32_t messageSequenceNumber,
                                    uint16_t hostEndpoint,
                                    bool reverse = false);
};

}  // namespace chre

#endif  // CHRE_UTIL_DUPLICATE_MESSAGE_DETECTOR_H_
