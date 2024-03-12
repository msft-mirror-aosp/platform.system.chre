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

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>

#include "chre/platform/linux/task_util/task_manager.h"
#include "chre/util/nested_data_ptr.h"
#include "chre/util/time.h"
#include "chre/util/transaction_manager.h"

#include "gtest/gtest.h"

namespace chre {
namespace {

struct TransactionData {
  bool *transactionStarted;
  uint32_t *numTimesTransactionStarted;
  uint32_t data;
};

struct TransactionCompleted {
  TransactionData data;
  uint8_t errorCode;
};

bool gTransactionCallbackCalled = false;
TransactionCompleted gTransactionCompleted;
TaskManager *gTaskManager = nullptr;

std::mutex gMutex;
std::condition_variable gCondVar;

bool transactionStartCallback(const TransactionData &data) {
  {
    std::lock_guard<std::mutex> lock(gMutex);

    if (data.transactionStarted != nullptr) {
      *data.transactionStarted = true;
    }
    if (data.numTimesTransactionStarted != nullptr) {
      ++(*data.numTimesTransactionStarted);
    }
  }

  gCondVar.notify_all();
  return true;
}

bool transactionCallback(const TransactionData &data, uint8_t errorCode) {
  {
    std::lock_guard<std::mutex> lock(gMutex);

    EXPECT_FALSE(gTransactionCallbackCalled);
    gTransactionCallbackCalled = true;
    gTransactionCompleted.data = data;
    gTransactionCompleted.errorCode = errorCode;
  }

  gCondVar.notify_all();
  return true;
}

bool deferCallback(
    TransactionManager<TransactionData>::DeferCallbackFunction func, void *data,
    void *extraData, Nanoseconds delay, uint32_t *outTimerHandle) {
  if (func == nullptr || gTaskManager == nullptr) {
    return false;
  }

  std::optional<uint32_t> taskId = gTaskManager->addTask(
      [func, data, extraData]() { func(/* type= */ 0, data, extraData); },
      std::chrono::nanoseconds(delay.toRawNanoseconds()),
      /* isOneShot= */ true);

  if (!taskId.has_value()) {
    return false;
  }

  if (outTimerHandle != nullptr) {
    *outTimerHandle = *taskId;
  }
  return true;
}

bool deferCancelCallback(uint32_t timerHandle) {
  return gTaskManager != nullptr && gTaskManager->cancelTask(timerHandle);
}

TransactionManager<TransactionData> gTransactionManager(
    transactionStartCallback, transactionCallback, deferCallback,
    deferCancelCallback);

TransactionManager<TransactionData> gFaultyStartTransactionManager(
    [](const TransactionData &data) {
      return transactionStartCallback(data) &&
             *data.numTimesTransactionStarted != 1;
    },
    transactionCallback, deferCallback, deferCancelCallback);

TEST(TransactionManager, TransactionShouldComplete) {
  gTaskManager = new TaskManager();
  gTransactionManager.setRetryWaitTime(Milliseconds(1));

  bool transactionStarted1 = false;
  bool transactionStarted2 = false;
  uint32_t transactionId1;
  uint32_t transactionId2;
  std::unique_lock<std::mutex> lock(gMutex);
  EXPECT_TRUE(gTransactionManager.startTransaction(
      {
          .transactionStarted = &transactionStarted1,
          .numTimesTransactionStarted = nullptr,
          .data = 1,
      },
      /* timeout= */ Nanoseconds(0), &transactionId1));
  gCondVar.wait(lock, [&transactionStarted1]() { return transactionStarted1; });
  EXPECT_TRUE(transactionStarted1);

  EXPECT_TRUE(gTransactionManager.startTransaction(
      {
          .transactionStarted = &transactionStarted2,
          .numTimesTransactionStarted = nullptr,
          .data = 2,
      },
      /* timeout= */ Nanoseconds(0), &transactionId2));
  gCondVar.wait(lock, [&transactionStarted2]() { return transactionStarted2; });
  EXPECT_TRUE(transactionStarted2);

  gTransactionCallbackCalled = false;
  EXPECT_TRUE(gTransactionManager.completeTransaction(
      transactionId2, CHRE_ERROR_INVALID_ARGUMENT));
  gCondVar.wait(lock);
  EXPECT_EQ(gTransactionCompleted.data.data, 2);
  EXPECT_EQ(gTransactionCompleted.errorCode, CHRE_ERROR_INVALID_ARGUMENT);

  gTransactionCallbackCalled = false;
  EXPECT_TRUE(
      gTransactionManager.completeTransaction(transactionId1, CHRE_ERROR_NONE));
  gCondVar.wait(lock);
  EXPECT_EQ(gTransactionCompleted.data.data, 1);
  EXPECT_EQ(gTransactionCompleted.errorCode, CHRE_ERROR_NONE);

  delete gTaskManager;
  gTaskManager = nullptr;
}

TEST(TransactionManager, TransactionShouldCompleteOnlyOnce) {
  gTaskManager = new TaskManager();

  gTransactionManager.setRetryWaitTime(Milliseconds(1));

  uint32_t transactionId;
  bool transactionStarted = false;
  std::unique_lock<std::mutex> lock(gMutex);
  EXPECT_TRUE(gTransactionManager.startTransaction(
      {
          .transactionStarted = &transactionStarted,
          .numTimesTransactionStarted = nullptr,
          .data = 1,
      },
      /* timeout= */ Nanoseconds(0), &transactionId));
  gCondVar.wait(lock, [&transactionStarted]() { return transactionStarted; });
  EXPECT_TRUE(transactionStarted);

  gTransactionCallbackCalled = false;
  EXPECT_TRUE(gTransactionManager.completeTransaction(
      transactionId, CHRE_ERROR_INVALID_ARGUMENT));
  gCondVar.wait(lock);

  gTransactionCallbackCalled = false;
  EXPECT_FALSE(gTransactionManager.completeTransaction(
      transactionId, CHRE_ERROR_INVALID_ARGUMENT));
  EXPECT_FALSE(gTransactionCallbackCalled);

  delete gTaskManager;
  gTaskManager = nullptr;
}

TEST(TransactionManager, TransactionShouldTimeout) {
  gTaskManager = new TaskManager();
  gTransactionManager.setRetryWaitTime(Milliseconds(1));

  uint32_t numTimesTransactionStarted = 0;
  uint32_t transactionId;
  gTransactionCallbackCalled = false;
  std::unique_lock<std::mutex> lock(gMutex);
  EXPECT_TRUE(gTransactionManager.startTransaction(
      {
          .transactionStarted = nullptr,
          .numTimesTransactionStarted = &numTimesTransactionStarted,
          .data = 456,
      },
      /* timeout= */ Milliseconds(10), &transactionId));
  gCondVar.wait(lock, [&numTimesTransactionStarted]() {
    return numTimesTransactionStarted ==
           TransactionManager<TransactionData>::kMaxNumRetries + 1;
  });
  EXPECT_EQ(numTimesTransactionStarted,
            TransactionManager<TransactionData>::kMaxNumRetries + 1);

  gTransactionCallbackCalled = false;
  gCondVar.wait(lock);
  EXPECT_EQ(gTransactionCompleted.data.data, 456);
  EXPECT_EQ(gTransactionCompleted.errorCode, CHRE_ERROR_TIMEOUT);
  EXPECT_TRUE(gTransactionCallbackCalled);

  delete gTaskManager;
  gTaskManager = nullptr;
}

TEST(TransactionManager, TransactionShouldRetryWhenTransactCallbackFails) {
  gTaskManager = new TaskManager();
  gFaultyStartTransactionManager.setRetryWaitTime(Milliseconds(1));

  uint32_t numTimesTransactionStarted = 0;
  const NestedDataPtr<uint32_t> kData(456);
  uint32_t transactionId;
  std::unique_lock<std::mutex> lock(gMutex);
  gTransactionCallbackCalled = false;
  EXPECT_TRUE(gFaultyStartTransactionManager.startTransaction(
      {
          .transactionStarted = nullptr,
          .numTimesTransactionStarted = &numTimesTransactionStarted,
          .data = kData,
      },
      /* timeout= */ Milliseconds(10), &transactionId));
  gCondVar.wait(lock, [&numTimesTransactionStarted]() {
    return numTimesTransactionStarted == 2;
  });
  EXPECT_EQ(numTimesTransactionStarted, 2);

  gTransactionCallbackCalled = false;
  EXPECT_TRUE(gFaultyStartTransactionManager.completeTransaction(
      transactionId, CHRE_ERROR_NONE));
  gCondVar.wait(lock);
  EXPECT_EQ(gTransactionCompleted.data.data, 456);
  EXPECT_EQ(gTransactionCompleted.errorCode, CHRE_ERROR_NONE);
  EXPECT_TRUE(gTransactionCallbackCalled);

  delete gTaskManager;
  gTaskManager = nullptr;
}

}  // namespace
}  // namespace chre
