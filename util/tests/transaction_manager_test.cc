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

constexpr uint16_t kMaxNumRetries = 3;

class TransactionManagerTest;

struct TransactionData {
  TransactionManagerTest *test;
  bool *transactionStarted;
  uint32_t *numTimesTransactionStarted;
  uint32_t data;
};

struct TransactionCompleted {
  TransactionData data;
  uint8_t errorCode;
};

class TransactionManagerTest : public testing::Test {
 public:
  bool transactionStartCallback(TransactionData &data, bool doFaultyStart) {
    {
      std::lock_guard<std::mutex> lock(mMutex);

      if (data.transactionStarted != nullptr) {
        *data.transactionStarted = true;
      }
      if (data.numTimesTransactionStarted != nullptr) {
        ++(*data.numTimesTransactionStarted);
      }
    }

    mCondVar.notify_all();
    return !doFaultyStart || *data.numTimesTransactionStarted != 1;
  }

  bool transactionCallback(const TransactionData &data, uint8_t errorCode) {
    {
      std::lock_guard<std::mutex> lock(mMutex);

      EXPECT_FALSE(mTransactionCallbackCalled);
      mTransactionCallbackCalled = true;
      mTransactionCompleted.data = data;
      mTransactionCompleted.errorCode = errorCode;
    }

    mCondVar.notify_all();
    return true;
  }

  static bool deferCallback(
      TransactionManager<TransactionData>::DeferCallbackFunction func,
      void *data, void *extraData, Nanoseconds delay,
      uint32_t *outTimerHandle) {
    if (func == nullptr) {
      return false;
    }

    std::optional<uint32_t> taskId = TaskManagerSingleton::get()->addTask(
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

  static bool deferCancelCallback(uint32_t timerHandle) {
    return TaskManagerSingleton::get()->cancelTask(timerHandle);
  }

  std::unique_ptr<TransactionManager<TransactionData>> getTransactionManager(
      bool doFaultyStart, uint16_t maxNumRetries = kMaxNumRetries) {
    return std::make_unique<TransactionManager<TransactionData>>(
        doFaultyStart
            ? [](TransactionData &data) {
              return data.test != nullptr && data.test->transactionStartCallback(data,
                  /* doFaultyStart= */ true);
            }
            : [](TransactionData &data) {
              return data.test != nullptr && data.test->transactionStartCallback(data,
                  /* doFaultyStart= */ false);
            },
        [](const TransactionData &data, uint8_t errorCode) {
          return data.test != nullptr && data.test->transactionCallback(data, errorCode);
        },
        TransactionManagerTest::deferCallback,
        TransactionManagerTest::deferCancelCallback,
        /* retryWaitTime= */ Milliseconds(1),
        maxNumRetries);
  }

 protected:
  void SetUp() override {
    TaskManagerSingleton::init();
  }

  void TearDown() override {}

  std::mutex mMutex;
  std::condition_variable mCondVar;
  bool mTransactionCallbackCalled = false;
  TransactionCompleted mTransactionCompleted;
};

TEST_F(TransactionManagerTest, TransactionShouldComplete) {
  std::unique_lock<std::mutex> lock(mMutex);
  std::unique_ptr<TransactionManager<TransactionData>> transactionManager =
      getTransactionManager(/* doFaultyStart= */ false);

  bool transactionStarted1 = false;
  bool transactionStarted2 = false;
  uint32_t transactionId1;
  uint32_t transactionId2;
  EXPECT_TRUE(transactionManager->startTransaction(
      {
          .test = this,
          .transactionStarted = &transactionStarted1,
          .numTimesTransactionStarted = nullptr,
          .data = 1,
      },
      /* timeout= */ Nanoseconds(0), &transactionId1));
  mCondVar.wait(lock, [&transactionStarted1]() { return transactionStarted1; });
  EXPECT_TRUE(transactionStarted1);

  EXPECT_TRUE(transactionManager->startTransaction(
      {
          .test = this,
          .transactionStarted = &transactionStarted2,
          .numTimesTransactionStarted = nullptr,
          .data = 2,
      },
      /* timeout= */ Nanoseconds(0), &transactionId2));
  mCondVar.wait(lock, [&transactionStarted2]() { return transactionStarted2; });
  EXPECT_TRUE(transactionStarted2);

  mTransactionCallbackCalled = false;
  EXPECT_TRUE(transactionManager->completeTransaction(
      transactionId2, CHRE_ERROR_INVALID_ARGUMENT));
  mCondVar.wait(lock, [this]() { return mTransactionCallbackCalled; });
  EXPECT_TRUE(mTransactionCallbackCalled);
  EXPECT_EQ(mTransactionCompleted.data.data, 2);
  EXPECT_EQ(mTransactionCompleted.errorCode, CHRE_ERROR_INVALID_ARGUMENT);

  mTransactionCallbackCalled = false;
  EXPECT_TRUE(
      transactionManager->completeTransaction(transactionId1, CHRE_ERROR_NONE));
  mCondVar.wait(lock, [this]() { return mTransactionCallbackCalled; });
  EXPECT_TRUE(mTransactionCallbackCalled);
  EXPECT_EQ(mTransactionCompleted.data.data, 1);
  EXPECT_EQ(mTransactionCompleted.errorCode, CHRE_ERROR_NONE);
  TaskManagerSingleton::get()->flushTasks();
}

TEST_F(TransactionManagerTest, TransactionShouldCompleteOnlyOnce) {
  std::unique_lock<std::mutex> lock(mMutex);
  std::unique_ptr<TransactionManager<TransactionData>> transactionManager =
      getTransactionManager(/* doFaultyStart= */ false);

  uint32_t transactionId;
  bool transactionStarted = false;
  EXPECT_TRUE(transactionManager->startTransaction(
      {
          .test = this,
          .transactionStarted = &transactionStarted,
          .numTimesTransactionStarted = nullptr,
          .data = 1,
      },
      /* timeout= */ Nanoseconds(0), &transactionId));
  mCondVar.wait(lock, [&transactionStarted]() { return transactionStarted; });
  EXPECT_TRUE(transactionStarted);

  mTransactionCallbackCalled = false;
  EXPECT_TRUE(transactionManager->completeTransaction(
      transactionId, CHRE_ERROR_INVALID_ARGUMENT));
  mCondVar.wait(lock, [this]() { return mTransactionCallbackCalled; });
  EXPECT_TRUE(mTransactionCallbackCalled);

  mTransactionCallbackCalled = false;
  EXPECT_FALSE(transactionManager->completeTransaction(
      transactionId, CHRE_ERROR_INVALID_ARGUMENT));
  EXPECT_FALSE(mTransactionCallbackCalled);
  TaskManagerSingleton::get()->flushTasks();
}

TEST_F(TransactionManagerTest, TransactionShouldTimeout) {
  std::unique_lock<std::mutex> lock(mMutex);
  std::unique_ptr<TransactionManager<TransactionData>> transactionManager =
      getTransactionManager(/* doFaultyStart= */ false);

  uint32_t numTimesTransactionStarted = 0;
  uint32_t transactionId;
  mTransactionCallbackCalled = false;
  EXPECT_TRUE(transactionManager->startTransaction(
      {
          .test = this,
          .transactionStarted = nullptr,
          .numTimesTransactionStarted = &numTimesTransactionStarted,
          .data = 456,
      },
      /* timeout= */ Milliseconds(10), &transactionId));
  mCondVar.wait(lock, [&numTimesTransactionStarted]() {
    return numTimesTransactionStarted == kMaxNumRetries + 1;
  });
  EXPECT_EQ(numTimesTransactionStarted, kMaxNumRetries + 1);

  mTransactionCallbackCalled = false;
  mCondVar.wait(lock, [this]() { return mTransactionCallbackCalled; });
  EXPECT_TRUE(mTransactionCallbackCalled);
  EXPECT_EQ(mTransactionCompleted.data.data, 456);
  EXPECT_EQ(mTransactionCompleted.errorCode, CHRE_ERROR_TIMEOUT);
  TaskManagerSingleton::get()->flushTasks();
}

TEST_F(TransactionManagerTest,
       TransactionShouldRetryWhenTransactCallbackFails) {
  std::unique_lock<std::mutex> lock(mMutex);
  std::unique_ptr<TransactionManager<TransactionData>> transactionManager =
      getTransactionManager(/* doFaultyStart= */ true);

  uint32_t numTimesTransactionStarted = 0;
  const NestedDataPtr<uint32_t> kData(456);
  uint32_t transactionId;
  mTransactionCallbackCalled = false;
  EXPECT_TRUE(transactionManager->startTransaction(
      {
          .test = this,
          .transactionStarted = nullptr,
          .numTimesTransactionStarted = &numTimesTransactionStarted,
          .data = kData,
      },
      /* timeout= */ Milliseconds(10), &transactionId));
  mCondVar.wait(lock, [&numTimesTransactionStarted]() {
    return numTimesTransactionStarted == 2;
  });
  EXPECT_EQ(numTimesTransactionStarted, 2);

  mTransactionCallbackCalled = false;
  EXPECT_TRUE(
      transactionManager->completeTransaction(transactionId, CHRE_ERROR_NONE));
  mCondVar.wait(lock, [this]() { return mTransactionCallbackCalled; });
  EXPECT_TRUE(mTransactionCallbackCalled);
  EXPECT_EQ(mTransactionCompleted.data.data, 456);
  EXPECT_EQ(mTransactionCompleted.errorCode, CHRE_ERROR_NONE);
  TaskManagerSingleton::get()->flushTasks();
}

TEST_F(TransactionManagerTest, TransactionShouldTimeoutWithNoRetries) {
  std::unique_lock<std::mutex> lock(mMutex);
  std::unique_ptr<TransactionManager<TransactionData>> transactionManager =
      getTransactionManager(/* doFaultyStart= */ false, /* maxNumRetries= */ 0);

  uint32_t numTimesTransactionStarted = 0;
  uint32_t transactionId;
  mTransactionCallbackCalled = false;
  EXPECT_TRUE(transactionManager->startTransaction(
      {
          .test = this,
          .transactionStarted = nullptr,
          .numTimesTransactionStarted = &numTimesTransactionStarted,
          .data = 456,
      },
      /* timeout= */ Milliseconds(10), &transactionId));
  mCondVar.wait(lock, [&numTimesTransactionStarted]() {
    return numTimesTransactionStarted == 1;
  });
  EXPECT_EQ(numTimesTransactionStarted, 1);

  mTransactionCallbackCalled = false;
  mCondVar.wait(lock, [this]() { return mTransactionCallbackCalled; });
  EXPECT_TRUE(mTransactionCallbackCalled);
  EXPECT_EQ(mTransactionCompleted.data.data, 456);
  EXPECT_EQ(mTransactionCompleted.errorCode, CHRE_ERROR_TIMEOUT);
  EXPECT_EQ(numTimesTransactionStarted, 1);  // No retries - only called once
  TaskManagerSingleton::get()->flushTasks();
}

}  // namespace
}  // namespace chre
