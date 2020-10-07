/*
 * Copyright (C) 2020 The Android Open Source Project
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

#ifndef CHRE_PLATFORM_SHARED_LOG_BUFFER_H_
#define CHRE_PLATFORM_SHARED_LOG_BUFFER_H_

#include <cinttypes>
#include <cstdarg>
#include <cstring>

#include "chre/platform/mutex.h"
#include "chre/util/fixed_size_vector.h"
#include "chre/util/singleton.h"

namespace chre {

/**
 * Values that represent a preferred setting for when the LogBuffer should
 * notify the platform that logs are ready to be copied.
 *
 * ALWAYS - The LogBuffer should immediately notify the platform when a new log
 *          is received.
 * NEVER -  The LogBuffer should never alert the platform that logs have been
 *          received. It is up to the platform to decide when to copy logs out.
 * THRESHOLD - The LogBuffer should notify the platform when a certain thresold
 *             of memory has been allocated for logs in the buffer.
 */
enum class LogBufferNotificationSetting : uint8_t { ALWAYS, NEVER, THRESHOLD };

/**
 * The log level options for logs stored in a log buffer.
 */
enum class LogBufferLogLevel : uint8_t { ERROR, WARN, INFO, DEBUG, VERBOSE };

// Forward declaration for LogBufferCallbackInterface.
class LogBuffer;

/**
 * Callback objects that are implemented by the platform code and passed to the
 * log buffer instances are notified of changes in the state of the buffer
 * through this callback interface.
 */
class LogBufferCallbackInterface {
 public:
  /**
   * Notify the platform code that is using the buffer manager that it should
   * call copyLogs because the buffer internal state has changed to suit the
   * requirements for alerting the platform that logs are ready to be copied
   * out of buffer.
   *
   * @param logBuffer The log buffer instance that the platform should copy logs
   * from.
   */
  virtual void onLogsReady(LogBuffer *logBuffer) = 0;
};

/**
 * The class responsible for batching logs in memory until the notification
 * callback is triggered and the platform copies log data out of the buffer.
 */
class LogBuffer {
 public:
  /**
   * @param callback The callback object that will receive notifications about
   *                 the state of the log buffer.
   * @param buffer The buffer location that will store log data.
   *                    message.
   * @param bufferSize The number of bytes in the buffer. This value must be >
   *                   kBufferMinSize
   */
  LogBuffer(LogBufferCallbackInterface *callback, void *buffer,
            size_t bufferSize);

  /**
   * Buffer this log and possibly call on logs ready callback depending on the
   * notification setting in place.  The method is thread-safe and will ensure
   * that logs are buffered in a FIFO ordering. If the buffer is full then drop
   * the oldest log.
   *
   * @param logLevel The log level.
   * @param timestampMs The timestamp that the log was collected as in
   *                    milliseconds. Monotonically increasing and in
   *                    milliseconds since boot.
   * @param log The ASCII log that is buffered.
   */
  void handleLog(LogBufferLogLevel logLevel, uint32_t timestampMs,
                 const char *log);

  /**
   * Copy out as many logs as will fit into destination buffer as they are
   * formatted internally. The memory where the logs were stored will be freed.
   * This method is thread-safe and will ensure that copyLogs will only copy
   * out the logs in a FIFO ordering.
   *
   * @param destination Pointer to the destination memory address.
   * @param size The max number of bytes to copy.
   *
   * @return The number of bytes copied from buffer to destination which may be
   *         less than size because partial logs are not copied into
   *         destination or the number of bytes left in the buffer is less than
   *         size.
   */
  size_t copyLogs(void *destination, size_t size);

  /**
   * Transfer all data from one log buffer to another. The destination log
   * buffer must have equal or greater capacity than this buffer. This method is
   * thread-safe and will ensure that logs are kept in FIFO ordering during a
   * transfer operation.
   *
   * @param otherBuffer The log buffer that is transferred to.
   */
  void transferTo(LogBuffer &otherBuffer);

  /**
   * Update the current log buffer notification setting which will determine
   * when the platform is notified to copy logs out of the buffer. Thread-safe.
   *
   * @param setting The new notification setting value.
   * @param thresholdBytes If the nofification setting is THRESHOLD, then if
   *                       the buffer allocates this many bytes the notification
   *                       callback will be triggerd, otherwise this parameter
   *                       is ignored.
   */
  void updateNotificationSetting(LogBufferNotificationSetting setting,
                                 size_t thresholdBytes = 0);

 private:
  /**
   * @return The current buffer size.
   */
  size_t getBufferSize() const;

  /**
   * Increment the value and take the modulus of the max size of the buffer.
   *
   * @param originalVal The original value to increment and mod.
   * @param incrementBy The amount to increment by.
   * @return The final value after incrementing and modulus.
   */
  size_t incrementAndModByBufferMaxSize(size_t originalVal,
                                        size_t incrementBy) const;

  /**
   * Copy from the source memory location to the buffer data ensuring that
   * the copy wraps around the buffer data if needed.
   *
   * @param size The number of bytes to copy into the buffer.
   * @param source The memory location to copy from.
   */
  void copyToBuffer(size_t size, const void *source);

  /**
   * Copy from the buffer data to a destination memory location ensuring that
   * the copy wraps around the buffer data if needed.
   *
   * @param size The number of bytes to copy into the buffer.
   * @param destination The memory location to copy to.
   */
  void copyFromBuffer(size_t size, void *destination);

  /**
   * Get next index indicating the start of a log entry from the starting
   * index of a previous log entry.
   *
   * @param startingIndex The starting index given.
   * @param logSize Non-null pointer that will be set to the size of the current
   *        log message.
   * @return The next starting log index.
   */
  size_t getNextLogIndex(size_t startingIndex, size_t *logSize);

  //! The number of bytes in a log entry of the buffer before the log size data
  //! is encountered.
  static constexpr size_t kLogSizeOffset = 5;
  //! The number of bytes that the log size data takes up in a log entry.
  static constexpr size_t kLogSizeBytes = 1;
  //! The max size of a single log entry which must fit in a single byte.
  static constexpr size_t kLogMaxSize = 255;

  /**
   * The buffer data is stored in the format
   *
   * [ logLevel (1B) , timestamp (4B), dataLength (1B), data (dataLenB) ]
   *
   * Since dataLength cannot be greater than uint8_t the max size of the data
   * portion can be max 255.
   */
  uint8_t *mBufferData;

  // TODO(b/170870354): Create a cirular buffer class to reuse this concept in
  // other parts of CHRE
  //! The buffer data head index
  size_t mBufferDataHeadIndex = 0;
  //! The buffer data tail index
  size_t mBufferDataTailIndex = 0;
  //! The current size of the data buffer
  size_t mBufferDataSize = 0;
  //! The buffer max size
  size_t mBufferMaxSize;
  //! The buffer min size
  // TODO(b/170870354): Setup a more appropriate min size
  static constexpr size_t kBufferMinSize = 1024;  // 1KB

  //! The callback object
  LogBufferCallbackInterface *mCallback;
  //! The notification setting object
  LogBufferNotificationSetting mNotificationSetting =
      LogBufferNotificationSetting::NEVER;
  //! The number of bytes that will trigger the threshold notification
  size_t mNotificationThresholdBytes = 0;

  // TODO(srok): Optimize the locking scheme
  //! The mutex guarding the buffer data bytes array
  Mutex mBufferDataLock;
};

}  // namespace chre

#endif  // CHRE_PLATFORM_SHARED_LOG_BUFFER_H_
