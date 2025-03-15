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

#include "chre/util/system/transaction_manager.h"

#include <algorithm>
#include <map>

#include "chre/core/timer_pool.h"
#include "chre/platform/linux/system_time.h"
#include "chre/util/system/system_callback_type.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using chre::platform_linux::SystemTimeOverride;
using testing::_;
using testing::Return;

namespace chre {
namespace {

constexpr size_t kMaxTransactions = 32;
constexpr Nanoseconds kTimeout = Milliseconds(10);
constexpr uint16_t kMaxAttempts = 3;

}  // anonymous namespace

class MockTimerPool {
 public:
  MOCK_METHOD(TimerHandle, setSystemTimer,
              (Nanoseconds, SystemEventCallbackFunction, SystemCallbackType,
               void *));
  MOCK_METHOD(bool, cancelSystemTimer, (TimerHandle));
};

class FakeTimerPool {
 public:
  TimerHandle setSystemTimer(Nanoseconds duration,
                             SystemEventCallbackFunction *callback,
                             SystemCallbackType /*callbackType*/, void *data) {
    Timer timer = {
        .expiry = SystemTime::getMonotonicTime() + duration,
        .callback = callback,
        .data = data,
    };
    TimerHandle handle = mNextHandle++;
    mTimers[handle] = timer;
    return handle;
  }
  bool cancelSystemTimer(TimerHandle handle) {
    return mTimers.erase(handle) == 1;
  }

  //! Advance the time to the next expiring timer and invoke its callback
  //! @return false if no timers exist
  bool invokeNextTimer(SystemTimeOverride &time,
                       Nanoseconds additionalDelay = Nanoseconds(0)) {
    auto it = std::min_element(mTimers.begin(), mTimers.end(),
                               [](const auto &a, const auto &b) {
                                 return a.second.expiry < b.second.expiry;
                               });
    if (it == mTimers.end()) {
      return false;
    }
    Timer timer = it->second;
    mTimers.erase(it);
    time.update(timer.expiry + additionalDelay);
    timer.callback(/*type=*/0, timer.data, /*extraData=*/nullptr);
    return true;
  }

  struct Timer {
    Nanoseconds expiry;
    SystemEventCallbackFunction *callback;
    void *data;
  };

  TimerHandle mNextHandle = 1;
  std::map<TimerHandle, Timer> mTimers;
};

class MockTransactionManagerCallback : public TransactionManagerCallback {
 public:
  MOCK_METHOD(void, onTransactionAttempt, (uint32_t, uint16_t), (override));
  MOCK_METHOD(void, onTransactionFailure, (uint32_t, uint16_t), (override));
};

class FakeTransactionManagerCallback : public TransactionManagerCallback {
 public:
  void onTransactionAttempt(uint32_t transactionId,
                            uint16_t /*groupId*/) override {
    mTries.push_back(transactionId);
  }
  void onTransactionFailure(uint32_t transactionId,
                            uint16_t /*groupId*/) override {
    mFailures.push_back(transactionId);
  }

  std::vector<uint32_t> mTries;
  std::vector<uint32_t> mFailures;
};

using TxnMgr = TransactionManager<kMaxTransactions, MockTimerPool>;
using TxnMgrF = TransactionManager<kMaxTransactions, FakeTimerPool>;

class TransactionManagerTest : public testing::Test {
 public:
 protected:
  TxnMgr defaultTxnMgr() {
    return TxnMgr(mFakeCb, mTimerPool, kTimeout, kMaxAttempts);
  }

  TxnMgrF defaultTxnMgrF() {
    return TxnMgrF(mFakeCb, mFakeTimerPool, kTimeout, kMaxAttempts);
  }

  static constexpr uint32_t kTimerId = 1;

  MockTimerPool mTimerPool;
  FakeTimerPool mFakeTimerPool;
  FakeTransactionManagerCallback mFakeCb;
  MockTransactionManagerCallback mMockCb;
  SystemTimeOverride mTime = SystemTimeOverride(0);
};

TEST_F(TransactionManagerTest, StartSingleTransaction) {
  TxnMgr tm = defaultTxnMgr();

  EXPECT_CALL(mTimerPool, setSystemTimer(kTimeout, _, _, _))
      .Times(1)
      .WillOnce(Return(kTimerId));

  uint32_t id;
  EXPECT_TRUE(tm.add(/*groupId=*/0, &id));

  ASSERT_EQ(mFakeCb.mTries.size(), 1);
  EXPECT_EQ(mFakeCb.mTries[0], id);
  EXPECT_EQ(mFakeCb.mFailures.size(), 0);
}

TEST_F(TransactionManagerTest, RemoveSingleTransaction) {
  TxnMgr tm = defaultTxnMgr();

  EXPECT_CALL(mTimerPool, setSystemTimer(_, _, _, _))
      .Times(1)
      .WillOnce(Return(kTimerId));

  uint32_t id;
  ASSERT_TRUE(tm.add(/*groupId=*/0, &id));

  EXPECT_CALL(mTimerPool, cancelSystemTimer(kTimerId))
      .Times(1)
      .WillOnce(Return(true));

  EXPECT_TRUE(tm.remove(id));
  EXPECT_EQ(mFakeCb.mTries.size(), 1);
  EXPECT_EQ(mFakeCb.mFailures.size(), 0);
}

TEST_F(TransactionManagerTest, SingleTransactionSuccessOnRetry) {
  TxnMgrF tm = defaultTxnMgrF();

  uint32_t id;
  ASSERT_TRUE(tm.add(0, &id));
  EXPECT_TRUE(mFakeTimerPool.invokeNextTimer(mTime));
  EXPECT_EQ(mFakeCb.mTries.size(), 2);

  EXPECT_TRUE(tm.remove(id));
  ASSERT_EQ(mFakeCb.mTries.size(), 2);
  EXPECT_EQ(mFakeCb.mTries[0], id);
  EXPECT_EQ(mFakeCb.mTries[1], id);
  EXPECT_EQ(mFakeCb.mFailures.size(), 0);
  EXPECT_FALSE(mFakeTimerPool.invokeNextTimer(mTime));
}

TEST_F(TransactionManagerTest, SingleTransactionTimeout) {
  TxnMgrF tm = defaultTxnMgrF();

  uint32_t id;
  ASSERT_TRUE(tm.add(0, &id));
  size_t count = 0;
  while (mFakeTimerPool.invokeNextTimer(mTime) && count++ < kMaxAttempts * 2);
  EXPECT_EQ(count, kMaxAttempts);
  EXPECT_EQ(std::count(mFakeCb.mTries.begin(), mFakeCb.mTries.end(), id),
            kMaxAttempts);
  ASSERT_EQ(mFakeCb.mFailures.size(), 1);
  EXPECT_EQ(mFakeCb.mFailures[0], id);

  // The transaction should actually be gone
  EXPECT_FALSE(tm.remove(id));
  EXPECT_FALSE(mFakeTimerPool.invokeNextTimer(mTime));
}

TEST_F(TransactionManagerTest, TwoTransactionsDifferentGroups) {
  TxnMgrF tm = defaultTxnMgrF();

  uint32_t id1;
  uint32_t id2;
  EXPECT_TRUE(tm.add(/*groupId=*/0, &id1));
  EXPECT_TRUE(tm.add(/*groupId=*/1, &id2));

  // Both should start
  ASSERT_EQ(mFakeCb.mTries.size(), 2);
  EXPECT_EQ(mFakeCb.mTries[0], id1);
  EXPECT_EQ(mFakeCb.mTries[1], id2);
  EXPECT_EQ(mFakeCb.mFailures.size(), 0);
}

TEST_F(TransactionManagerTest, TwoTransactionsSameGroup) {
  TxnMgrF tm = defaultTxnMgrF();

  uint32_t id1;
  uint32_t id2;
  EXPECT_TRUE(tm.add(/*groupId=*/0, &id1));
  EXPECT_TRUE(tm.add(/*groupId=*/0, &id2));

  // Only the first should start
  ASSERT_EQ(mFakeCb.mTries.size(), 1);
  EXPECT_EQ(mFakeCb.mTries[0], id1);

  // Second starts after the first finishes
  EXPECT_TRUE(tm.remove(id1));
  ASSERT_EQ(mFakeCb.mTries.size(), 2);
  EXPECT_EQ(mFakeCb.mTries[1], id2);

  // Second completes with no funny business
  EXPECT_TRUE(tm.remove(id2));
  EXPECT_EQ(mFakeCb.mTries.size(), 2);
  EXPECT_EQ(mFakeCb.mFailures.size(), 0);
  EXPECT_FALSE(mFakeTimerPool.invokeNextTimer(mTime));
}

TEST_F(TransactionManagerTest, TwoTransactionsSameGroupTimeout) {
  TxnMgrF tm = defaultTxnMgrF();

  uint32_t id1;
  uint32_t id2;
  EXPECT_TRUE(tm.add(/*groupId=*/0, &id1));
  EXPECT_TRUE(tm.add(/*groupId=*/0, &id2));

  // Time out the first transaction, which should kick off the second
  for (size_t i = 0; i < kMaxAttempts; i++) {
    EXPECT_TRUE(mFakeTimerPool.invokeNextTimer(mTime));
  }
  ASSERT_EQ(mFakeCb.mTries.size(), kMaxAttempts + 1);
  EXPECT_EQ(std::count(mFakeCb.mTries.begin(), mFakeCb.mTries.end(), id1),
            kMaxAttempts);
  EXPECT_EQ(mFakeCb.mTries.back(), id2);

  // Retry + time out behavior for second works the same as the first
  for (size_t i = 0; i < kMaxAttempts; i++) {
    EXPECT_TRUE(mFakeTimerPool.invokeNextTimer(mTime));
  }
  ASSERT_EQ(mFakeCb.mTries.size(), kMaxAttempts * 2);
  EXPECT_EQ(std::count(mFakeCb.mTries.begin(), mFakeCb.mTries.end(), id2),
            kMaxAttempts);
  ASSERT_EQ(mFakeCb.mFailures.size(), 2);
  EXPECT_EQ(mFakeCb.mFailures[0], id1);
  EXPECT_EQ(mFakeCb.mFailures[1], id2);
  EXPECT_FALSE(mFakeTimerPool.invokeNextTimer(mTime));
}

TEST_F(TransactionManagerTest, TwoTransactionsSameGroupRemoveReverseOrder) {
  TxnMgrF tm = defaultTxnMgrF();

  uint32_t id1;
  uint32_t id2;
  EXPECT_TRUE(tm.add(/*groupId=*/0, &id1));
  EXPECT_TRUE(tm.add(/*groupId=*/0, &id2));

  // Only the first should start
  ASSERT_EQ(mFakeCb.mTries.size(), 1);
  EXPECT_EQ(mFakeCb.mTries[0], id1);

  // Remove second one first
  EXPECT_TRUE(tm.remove(id2));

  // Finish the first one
  EXPECT_TRUE(tm.remove(id1));
  ASSERT_EQ(mFakeCb.mTries.size(), 1);
  EXPECT_EQ(mFakeCb.mTries[0], id1);
  EXPECT_EQ(mFakeCb.mFailures.size(), 0);
  EXPECT_FALSE(mFakeTimerPool.invokeNextTimer(mTime));
}

TEST_F(TransactionManagerTest, MultipleTimeouts) {
  TxnMgrF tm = defaultTxnMgrF();

  // Timeout both in a single callback
  uint32_t ids[2];
  EXPECT_TRUE(tm.add(/*groupId=*/0, &ids[0]));
  mTime.update(kTimeout.toRawNanoseconds() / 2);
  EXPECT_TRUE(tm.add(/*groupId=*/1, &ids[1]));
  EXPECT_TRUE(mFakeTimerPool.invokeNextTimer(mTime, kTimeout));
  EXPECT_EQ(mFakeCb.mTries.size(), 4);

  // Since both retries were dispatched at the same time, they should time out
  // again together
  EXPECT_TRUE(mFakeTimerPool.invokeNextTimer(mTime, kTimeout));
  EXPECT_EQ(mFakeCb.mTries.size(), 6);

  // If changing the max # of attempts, modify the below code too so it triggers
  // failure
  static_assert(kMaxAttempts == 3);
  EXPECT_TRUE(mFakeTimerPool.invokeNextTimer(mTime, kTimeout));
  EXPECT_EQ(mFakeCb.mTries.size(), 6);
  for (size_t i = 0; i < mFakeCb.mTries.size(); i++) {
    EXPECT_EQ(mFakeCb.mTries[i], ids[i % 2]);
  }
  ASSERT_EQ(mFakeCb.mFailures.size(), 2);
  EXPECT_EQ(mFakeCb.mFailures[0], ids[0]);
  EXPECT_EQ(mFakeCb.mFailures[1], ids[1]);
  EXPECT_FALSE(mFakeTimerPool.invokeNextTimer(mTime));
}

TEST_F(TransactionManagerTest, CallbackUsesCorrectGroupId) {
  TxnMgrF tm(mMockCb, mFakeTimerPool, kTimeout, /*maxAttempts=*/1);

  EXPECT_CALL(mMockCb, onTransactionAttempt(_, 1)).Times(1);
  EXPECT_CALL(mMockCb, onTransactionAttempt(_, 2)).Times(1);
  EXPECT_CALL(mMockCb, onTransactionAttempt(_, 3)).Times(1);

  uint32_t id;
  tm.add(1, &id);
  tm.add(2, &id);
  tm.add(3, &id);

  EXPECT_CALL(mMockCb, onTransactionFailure(_, 1)).Times(1);
  EXPECT_CALL(mMockCb, onTransactionFailure(_, 2)).Times(1);
  EXPECT_CALL(mMockCb, onTransactionFailure(_, 3)).Times(1);

  mFakeTimerPool.invokeNextTimer(mTime);
  mFakeTimerPool.invokeNextTimer(mTime);
  mFakeTimerPool.invokeNextTimer(mTime);
}

}  // namespace chre
