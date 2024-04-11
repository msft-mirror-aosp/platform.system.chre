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
#include <map>
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
constexpr size_t kMaxTransactions = 32;

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
 protected:
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
      TransactionManager<TransactionData,
                         kMaxTransactions>::DeferCallbackFunction func,
      void *data, void *extraData, Nanoseconds delay,
      uint32_t *outTimerHandle) {
    if (func == nullptr) {
      return false;
    }

    std::lock_guard<std::mutex> lock(sMapMutex);
    auto iter = sMap.find(
        testing::UnitTest::GetInstance()->current_test_info()->name());
    if (iter == sMap.end()) {
      if (outTimerHandle != nullptr) {
        *outTimerHandle = 0xDEADBEEF;
      }
      return true;  // Test is ending - no need to defer callback
    }

    std::optional<uint32_t> taskId = iter->second->getTaskManager()->addTask(
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
    std::lock_guard<std::mutex> lock(sMapMutex);
    auto iter = sMap.find(
        testing::UnitTest::GetInstance()->current_test_info()->name());
    if (iter == sMap.end()) {
      return true;  // Test is ending - no need to cancel defer callback
    }

    return iter->second->getTaskManager()->cancelTask(timerHandle);
  }

  TaskManager *getTaskManager() const {
    return mTaskManager.get();
  }

  std::unique_ptr<TransactionManager<TransactionData, kMaxTransactions>>
  getTransactionManager(bool doFaultyStart,
                        uint16_t maxNumRetries = kMaxNumRetries) const {
    return std::make_unique<TransactionManager<TransactionData, kMaxTransactions>>(
        doFaultyStart
            ? [](TransactionData &data) {
              return data.test != nullptr &&
                  data.test->transactionStartCallback(data,
                      /* doFaultyStart= */ true);
            }
            : [](TransactionData &data) {
              return data.test != nullptr &&
                  data.test->transactionStartCallback(data,
                      /* doFaultyStart= */ false);
            },
        [](const TransactionData &data, uint8_t errorCode) {
          return data.test != nullptr &&
              data.test->transactionCallback(data, errorCode);
        },
        TransactionManagerTest::deferCallback,
        TransactionManagerTest::deferCancelCallback,
        /* retryWaitTime= */ Milliseconds(1),
        maxNumRetries);
  }

  void SetUp() override {
    {
      std::lock_guard<std::mutex> lock(sMapMutex);
      sMap.insert_or_assign(
          testing::UnitTest::GetInstance()->current_test_info()->name(), this);
    }

    mTransactionManager = getTransactionManager(/* doFaultyStart= */ false);
    mFaultyStartTransactionManager =
        getTransactionManager(/* doFaultyStart= */ true);
    mZeroRetriesTransactionManager =
        getTransactionManager(/* doFaultyStart= */ false,
                              /* maxNumRetries= */ 0);

    mTaskManager = std::make_unique<TaskManager>();
  }

  void TearDown() override {
    {
      std::lock_guard<std::mutex> lock(sMapMutex);
      sMap.erase(testing::UnitTest::GetInstance()->current_test_info()->name());
    }

    mTaskManager.reset();
    mZeroRetriesTransactionManager.reset();
    mFaultyStartTransactionManager.reset();
    mTransactionManager.reset();
  }

  static std::mutex sMapMutex;
  static std::map<std::string, const TransactionManagerTest *> sMap;

  std::mutex mMutex;
  std::condition_variable mCondVar;
  std::atomic<bool> mTransactionCallbackCalled = false;
  std::unique_ptr<TaskManager> mTaskManager = nullptr;
  TransactionCompleted mTransactionCompleted;

  std::unique_ptr<TransactionManager<TransactionData, kMaxTransactions>>
      mTransactionManager = nullptr;
  std::unique_ptr<TransactionManager<TransactionData, kMaxTransactions>>
      mFaultyStartTransactionManager = nullptr;
  std::unique_ptr<TransactionManager<TransactionData, kMaxTransactions>>
      mZeroRetriesTransactionManager = nullptr;
};
std::mutex TransactionManagerTest::sMapMutex;
std::map<std::string, const TransactionManagerTest *>
    TransactionManagerTest::sMap;

TEST_F(TransactionManagerTest, TransactionShouldComplete) {
  std::unique_lock<std::mutex> lock(mMutex);

  bool transactionStarted1 = false;
  bool transactionStarted2 = false;
  uint32_t transactionId1;
  uint32_t transactionId2;
  EXPECT_TRUE(mTransactionManager->startTransaction(
      {
          .test = this,
          .transactionStarted = &transactionStarted1,
          .numTimesTransactionStarted = nullptr,
          .data = 1,
      },
      /* timeout= */ Nanoseconds(0), &transactionId1));
  mCondVar.wait(lock, [&transactionStarted1]() { return transactionStarted1; });
  EXPECT_TRUE(transactionStarted1);

  EXPECT_TRUE(mTransactionManager->startTransaction(
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
  EXPECT_TRUE(mTransactionManager->completeTransaction(
      transactionId2, CHRE_ERROR_INVALID_ARGUMENT));
  mCondVar.wait(lock, [this]() { return mTransactionCallbackCalled.load(); });
  EXPECT_TRUE(mTransactionCallbackCalled);
  EXPECT_EQ(mTransactionCompleted.data.data, 2);
  EXPECT_EQ(mTransactionCompleted.errorCode, CHRE_ERROR_INVALID_ARGUMENT);

  mTransactionCallbackCalled = false;
  EXPECT_TRUE(mTransactionManager->completeTransaction(transactionId1,
                                                       CHRE_ERROR_NONE));
  mCondVar.wait(lock, [this]() { return mTransactionCallbackCalled.load(); });
  EXPECT_TRUE(mTransactionCallbackCalled);
  EXPECT_EQ(mTransactionCompleted.data.data, 1);
  EXPECT_EQ(mTransactionCompleted.errorCode, CHRE_ERROR_NONE);
}

TEST_F(TransactionManagerTest, TransactionShouldCompleteOnlyOnce) {
  std::unique_lock<std::mutex> lock(mMutex);

  uint32_t transactionId;
  bool transactionStarted = false;
  EXPECT_TRUE(mTransactionManager->startTransaction(
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
  EXPECT_TRUE(mTransactionManager->completeTransaction(
      transactionId, CHRE_ERROR_INVALID_ARGUMENT));
  mCondVar.wait(lock, [this]() { return mTransactionCallbackCalled.load(); });
  EXPECT_TRUE(mTransactionCallbackCalled);

  mTransactionCallbackCalled = false;
  EXPECT_FALSE(mTransactionManager->completeTransaction(
      transactionId, CHRE_ERROR_INVALID_ARGUMENT));
  EXPECT_FALSE(mTransactionCallbackCalled);
}

TEST_F(TransactionManagerTest, TransactionShouldTimeout) {
  std::unique_lock<std::mutex> lock(mMutex);

  uint32_t numTimesTransactionStarted = 0;
  uint32_t transactionId;
  mTransactionCallbackCalled = false;
  EXPECT_TRUE(mTransactionManager->startTransaction(
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
  mCondVar.wait(lock, [this]() { return mTransactionCallbackCalled.load(); });
  EXPECT_TRUE(mTransactionCallbackCalled);
  EXPECT_EQ(mTransactionCompleted.data.data, 456);
  EXPECT_EQ(mTransactionCompleted.errorCode, CHRE_ERROR_TIMEOUT);
}

TEST_F(TransactionManagerTest,
       TransactionShouldRetryWhenTransactCallbackFails) {
  std::unique_lock<std::mutex> lock(mMutex);

  uint32_t numTimesTransactionStarted = 0;
  const NestedDataPtr<uint32_t> kData(456);
  uint32_t transactionId;
  mTransactionCallbackCalled = false;
  EXPECT_TRUE(mFaultyStartTransactionManager->startTransaction(
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
  EXPECT_TRUE(mFaultyStartTransactionManager->completeTransaction(
      transactionId, CHRE_ERROR_NONE));
  mCondVar.wait(lock, [this]() { return mTransactionCallbackCalled.load(); });
  EXPECT_TRUE(mTransactionCallbackCalled);
  EXPECT_EQ(mTransactionCompleted.data.data, 456);
  EXPECT_EQ(mTransactionCompleted.errorCode, CHRE_ERROR_NONE);
}

TEST_F(TransactionManagerTest, TransactionShouldTimeoutWithNoRetries) {
  std::unique_lock<std::mutex> lock(mMutex);

  uint32_t numTimesTransactionStarted = 0;
  uint32_t transactionId;
  mTransactionCallbackCalled = false;
  EXPECT_TRUE(mZeroRetriesTransactionManager->startTransaction(
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
  mCondVar.wait(lock, [this]() { return mTransactionCallbackCalled.load(); });
  EXPECT_TRUE(mTransactionCallbackCalled);
  EXPECT_EQ(mTransactionCompleted.data.data, 456);
  EXPECT_EQ(mTransactionCompleted.errorCode, CHRE_ERROR_TIMEOUT);
  EXPECT_EQ(numTimesTransactionStarted, 1);  // No retries - only called once
}

TEST_F(TransactionManagerTest, FlushedTransactionShouldNotComplete) {
  std::unique_lock<std::mutex> lock(mMutex);

  bool transactionStarted1 = false;
  bool transactionStarted2 = false;
  uint32_t transactionId1;
  uint32_t transactionId2;
  EXPECT_TRUE(mTransactionManager->startTransaction(
      {
          .test = this,
          .transactionStarted = &transactionStarted1,
          .numTimesTransactionStarted = nullptr,
          .data = 1,
      },
      /* timeout= */ Nanoseconds(0), &transactionId1));
  mCondVar.wait(lock, [&transactionStarted1]() { return transactionStarted1; });
  EXPECT_TRUE(transactionStarted1);

  EXPECT_TRUE(mTransactionManager->startTransaction(
      {
          .test = this,
          .transactionStarted = &transactionStarted2,
          .numTimesTransactionStarted = nullptr,
          .data = 2,
      },
      /* timeout= */ Nanoseconds(0), &transactionId2));
  mCondVar.wait(lock, [&transactionStarted2]() { return transactionStarted2; });
  EXPECT_TRUE(transactionStarted2);

  EXPECT_EQ(mTransactionManager->flushTransactions(
                [](const TransactionData &data, void *callbackData) {
                  NestedDataPtr<uint32_t> magicNum(callbackData);
                  return magicNum == 456 && data.data == 2;
                },
                NestedDataPtr<uint32_t>(456)),
            1);

  EXPECT_FALSE(mTransactionManager->completeTransaction(
      transactionId2, CHRE_ERROR_INVALID_ARGUMENT));

  mTransactionCallbackCalled = false;
  EXPECT_TRUE(mTransactionManager->completeTransaction(transactionId1,
                                                       CHRE_ERROR_NONE));
  mCondVar.wait(lock, [this]() { return mTransactionCallbackCalled.load(); });
  EXPECT_TRUE(mTransactionCallbackCalled);
  EXPECT_EQ(mTransactionCompleted.data.data, 1);
  EXPECT_EQ(mTransactionCompleted.errorCode, CHRE_ERROR_NONE);
}

}  // namespace
}  // namespace chre
