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

#ifndef CHRE_LOG_MESSAGE_PARSER_H_
#define CHRE_LOG_MESSAGE_PARSER_H_

#include <endian.h>
#include <cinttypes>
#include <memory>
#include "chre/util/time.h"
#include "chre_host/bt_snoop_log_parser.h"
#include "chre_host/nanoapp_load_listener.h"

#include <android/log.h>

#include "pw_tokenizer/detokenize.h"

using pw::tokenizer::DetokenizedString;
using pw::tokenizer::Detokenizer;

namespace android {
namespace chre {

class LogMessageParser : public INanoappLoadListener {
 public:
  LogMessageParser();

  /**
   * Allow the user to enable verbose logging during instantiation.
   */
  LogMessageParser(bool enableVerboseLogging)
      : mVerboseLoggingEnabled(enableVerboseLogging) {}

  /**
   * Initializes the log message parser by reading the log token database,
   * and instantiates a detokenizer to handle encoded log messages.
   *
   * @param nanoappImageHeaderSize Offset in bytes between the address of the
   * nanoapp binary and the real start of the ELF header.
   */
  void init(size_t nanoappImageHeaderSize = 0);

  //! Logs from a log buffer containing one or more log messages (version 1)
  void log(const uint8_t *logBuffer, size_t logBufferSize);

  //! Logs from a log buffer containing one or more log messages (version 2)
  void logV2(const uint8_t *logBuffer, size_t logBufferSize,
             uint32_t numLogsDropped);

  /**
   * With verbose logging enabled (either during instantiation via a
   * constructor argument, or during compilation via N_DEBUG being defined
   * and set), dump a binary log buffer to a human-readable string.
   *
   * @param logBuffer buffer to be output as a string
   * @param logBufferSize size of the buffer being output
   */
  void dump(const uint8_t *logBuffer, size_t logBufferSize);

  /**
   * Stores a pigweed detokenizer for decoding logs from a given nanoapp.
   *
   * @param appId The app ID associated with the nanoapp.
   * @param instanceId The instance ID assigned to this nanoapp by the CHRE
   * event loop.
   * @param databaseOffset The size offset of the token database from the start
   * of the address of the ELF binary in bytes.
   * @param databaseSize The size of the token database section in the ELF
   * binary in bytes.
   */
  void addNanoappDetokenizer(uint64_t appId, uint16_t instanceId,
                             uint64_t databaseOffset, size_t databaseSize);

  /**
   * Remove an existing detokenizer associated with a nanoapp if it exists.
   *
   * @param appId The app ID associated with the nanoapp.
   * @param removeBinary Remove the nanoapp binary associated with the app ID if
   * true.
   */
  void removeNanoappDetokenizerAndBinary(uint64_t appId);

  /**
   * Reset all nanoapp log detokenizers.
   */
  void resetNanoappDetokenizerState();

  // Functions from INanoappLoadListener.
  void onNanoappLoadStarted(
      uint64_t appId,
      std::shared_ptr<const std::vector<uint8_t>> nanoappBinary) override;

  void onNanoappLoadFailed(uint64_t appId) override;

  void onNanoappUnloaded(uint64_t appId) override;

 private:
  static constexpr char kHubLogFormatStr[] = "@ %3" PRIu32 ".%03" PRIu32 ": %s";

  enum LogLevel : uint8_t {
    ERROR = 1,
    WARNING = 2,
    INFO = 3,
    DEBUG = 4,
    VERBOSE = 5,
  };

  //! See host_messages.fbs for the definition of this struct.
  struct LogMessage {
    enum LogLevel logLevel;
    uint64_t timestampNanos;
    char logMessage[];
  } __attribute__((packed));

  //! See host_messages.fbs for the definition of this struct.
  struct LogMessageV2 {
    uint8_t metadata;
    uint32_t timestampMillis;
    char logMessage[];
  } __attribute__((packed));

  /**
   * Helper struct for readable decoding of a tokenized log message payload,
   * essentially encapsulates the 'logMessage' field in LogMessageV2 for an
   * encoded log.
   */
  struct EncodedLog {
    uint8_t size;
    char data[];
  };

  /**
   * Helper struct for readable decoding of a tokenized log message from a
   * nanoapp.
   */
  struct NanoappTokenizedLog {
    uint16_t instanceId;
    uint8_t size;
    char data[];
  } __attribute__((packed));

  bool mVerboseLoggingEnabled;

  //! The number of logs dropped since CHRE start.
  uint32_t mNumLogsDropped = 0;

  //! Log detokenizer used for CHRE system logs.
  std::unique_ptr<Detokenizer> mSystemDetokenizer;

  /**
   * Helper struct for keep track of nanoapp's log detokenizer with appIDs.
   */
  struct NanoappDetokenizer {
    std::unique_ptr<Detokenizer> detokenizer;
    uint64_t appId;
  };

  //! Maps nanoapp instance IDs to the corresponding app ID and pigweed
  //! detokenizer.
  std::unordered_map<uint16_t /*instanceId*/, NanoappDetokenizer>
      mNanoappDetokenizers;

  //! This is used to find the binary associated with a nanoapp with its app ID.
  std::unordered_map<uint64_t /*appId*/,
                     std::shared_ptr<const std::vector<uint8_t>>>
      mNanoappAppIdToBinary;

  static android_LogPriority chreLogLevelToAndroidLogPriority(uint8_t level);

  BtSnoopLogParser mBtLogParser;

  //! Offset in bytes between the address of the nanoapp binary and the real
  //! start of the ELF header.
  size_t mNanoappImageHeaderSize = 0;

  void updateAndPrintDroppedLogs(uint32_t numLogsDropped);

  //! Method for parsing unencoded (string) log messages.
  void parseAndEmitLogMessage(const LogMessageV2 *message);

  /**
   * Parses and emits an encoded log message while also returning the size of
   * the parsed message for buffer index bookkeeping.
   *
   * @return Size of the encoded log message payload. Note that the size
   * includes the 1 byte header that we use for encoded log messages to track
   * message size.
   */
  size_t parseAndEmitTokenizedLogMessageAndGetSize(const LogMessageV2 *message);

  /**
   * Similar to parseAndEmitTokenizedLogMessageAndGetSize, but used for encoded
   * log message from nanoapps.
   *
   * @return Size of the encoded log message payload. Note that the size
   * includes the 1 byte header that we use for encoded log messages to track
   * message size, and the 2 byte instance ID that the host uses to find the
   * correct detokenizer.
   */
  size_t parseAndEmitNanoappTokenizedLogMessageAndGetSize(
      const LogMessageV2 *message);

  void emitLogMessage(uint8_t level, uint32_t timestampMillis,
                      const char *logMessage);

  /**
   * Initialize the Log Detokenizer
   *
   * The log detokenizer reads a binary database file that contains key value
   * pairs of hash-keys <--> Decoded log messages, and creates an instance
   * of the Detokenizer.
   *
   * @return an instance of the Detokenizer
   */
  std::unique_ptr<Detokenizer> logDetokenizerInit();

  /**
   * Helper function to get the logging level from the log message metadata.
   *
   * @param metadata A byte from the log message payload containing the
   *        log level and encoding information.
   *
   * @return The log level of the current log message.
   */
  uint8_t getLogLevelFromMetadata(uint8_t metadata);

  /**
   * Helper function to check the metadata whether the log message was encoded.
   *
   * @param metadata A byte from the log message payload containing the
   *        log level and encoding information.
   *
   * @return true if an encoding was used on the log message payload.
   */
  bool isLogMessageEncoded(uint8_t metadata);

  /**
   * Helper function to check the metadata whether the log message is a BT snoop
   * log.
   *
   * @param metadata A byte from the log message payload containing the
   *        log level and log type information.
   *
   * @return true if the log message type is BT Snoop log.
   */
  bool isBtSnoopLogMessage(uint8_t metadata);

  /**
   * Helper function to check the metadata whether the log message is tokenized
   * and sent from a nanoapp
   *
   * @param metadata A byte from the log message payload containing the
   *        log level and log type information.
   *
   * @return true if the log message is tokenzied and sent from a nanoapp.
   */
  bool isNanoappTokenizedLogMessage(uint8_t metadata);

  /**
   * Helper function to check nanoapp log token database for memory overflow and
   * wraparound.
   *
   * @param databaseOffset The size offset of the token database from the start
   * of the address of the ELF binary in bytes.
   * @param databaseSize The size of the token database section in the ELF
   * binary in bytes.
   * @param binarySize. The size of the nanoapp binary in bytes.
   *
   * @return True if the token database passes memory bounds checks.
   */
  bool checkTokenDatabaseOverflow(uint32_t databaseOffset, size_t databaseSize,
                                  size_t binarySize);
};

}  // namespace chre
}  // namespace android

#endif  // CHRE_LOG_MESSAGE_PARSER_H_
