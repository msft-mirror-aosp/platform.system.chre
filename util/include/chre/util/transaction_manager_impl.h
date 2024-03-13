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

#ifndef CHRE_UTIL_TRANSACTION_MANAGER_IMPL_H_
#define CHRE_UTIL_TRANSACTION_MANAGER_IMPL_H_

#include <inttypes.h>
#include <algorithm>
#include <cmath>

#include "chre/platform/assert.h"
#include "chre/platform/system_time.h"
#include "chre/util/lock_guard.h"
#include "chre/util/nested_data_ptr.h"
#include "chre/util/time.h"
#include "chre/util/transaction_manager.h"
#include "chre_api/chre.h"

namespace chre {

using ::chre::Nanoseconds;
using ::chre::Seconds;

template <typename TransactionData>
bool TransactionManager<TransactionData>::completeTransaction(
    uint32_t transactionId, uint8_t errorCode) {
  LockGuard lock(mMutex);

  for (size_t i = 0; i < mTransactions.size(); ++i) {
    Transaction &transaction = mTransactions[i];
    if (transaction.id == transactionId) {
      if (errorCode == CHRE_ERROR_TRANSIENT) {
        transaction.nextRetryTime = Nanoseconds(0);
      } else {
        transaction.errorCode = errorCode;
      }
      deferProcessTransactions(this, /* timerFired= */ false);
      return true;
    }
  }

  LOGE("Unable to complete transaction with ID: %" PRIu32, transactionId);
  return false;
}

template <typename TransactionData>
uint32_t TransactionManager<TransactionData>::flushTransactions(
    FlushCallback *callback) {
  if (callback == nullptr) {
    return 0;
  }

  LockGuard lock(mMutex);

  deferProcessTransactions(this, /* timerFired= */ false);
  return mTransactions.removeMatchedFromBack(
      [](Transaction &transaction, void *data) {
        FlushCallback *callback = static_cast<FlushCallback *>(data);
        if (callback == nullptr) {
          return false;
        }

        return callback(transaction.data);
      },
      callback, mTransactions.size());
}

template <typename TransactionData>
bool TransactionManager<TransactionData>::startTransaction(
    const TransactionData &data, Nanoseconds timeout, uint32_t *id) {
  CHRE_ASSERT(id != nullptr);

  LockGuard lock(mMutex);

  if (timeout.toRawNanoseconds() != 0 && timeout <= mRetryWaitTime) {
    LOGE("Timeout: %" PRIu64 "ns is <= retry wait time: %" PRIu64 "ns",
         timeout.toRawNanoseconds(), mRetryWaitTime.toRawNanoseconds());
    return false;
  }

  if (mTransactions.full()) {
    LOGE("The transaction queue is full");
    return false;
  }

  uint32_t transactionId = mNextTransactionId++;
  *id = transactionId;

  Transaction transaction{
      .id = transactionId,
      .data = data,
      .nextRetryTime = Nanoseconds(0),
      .timeoutTime = timeout.toRawNanoseconds() == 0
                         ? Nanoseconds(UINT64_MAX)
                         : SystemTime::getMonotonicTime() + timeout,
      .numCompletedStartCalls = 0,
      .errorCode = Optional<uint8_t>(),
  };

  mTransactions.push_back(transaction);

  deferProcessTransactions(this, /* timerFired= */ false);
  return true;
}

template <typename TransactionData>
void TransactionManager<TransactionData>::deferProcessTransactions(
    void *data, bool timerFired) {
  bool status = mDeferCallback(
      [](uint16_t /* type */, void *data, void *extraData) {
        auto transactionManagerPtr = static_cast<TransactionManager *>(data);
        if (transactionManagerPtr == nullptr) {
          LOGE("Could not get transaction manager to process transactions");
          return;
        }

        NestedDataPtr<bool> timerFiredFromExtraData(extraData);
        transactionManagerPtr->processTransactions(timerFiredFromExtraData);
      },
      data, NestedDataPtr<bool>(timerFired),
      /* delay= */ Nanoseconds(0),
      /* outTimerHandle= */ nullptr);

  if (!status) {
    LOGE("Could not defer callback to process transactions");
  }
}

template <typename TransactionData>
void TransactionManager<TransactionData>::processTransactions(bool timerFired) {
  LockGuard lock(mMutex);

  if (!timerFired && mTimerHandle != CHRE_TIMER_INVALID) {
    mDeferCancelCallback(mTimerHandle);
  }
  mTimerHandle = CHRE_TIMER_INVALID;

  if (mTransactions.size() == 0) {
    return;
  }

  Nanoseconds now = SystemTime::getMonotonicTime();
  Nanoseconds nextExecutionTime(UINT64_MAX);
  for (size_t i = 0; i < mTransactions.size();) {
    Transaction &transaction = mTransactions[i];
    if (transaction.timeoutTime <= now ||
        (transaction.nextRetryTime <= now &&
         transaction.numCompletedStartCalls >= kMaxNumRetries + 1) ||
        transaction.errorCode.has_value()) {
      uint8_t errorCode = transaction.errorCode.has_value()
                              ? *transaction.errorCode
                              : CHRE_ERROR_TIMEOUT;

      bool status = mCompleteCallback(transaction.data, errorCode);
      if (status) {
        LOGI("Transaction %" PRIu32 " completed with error code: %" PRIu8,
             transaction.id, errorCode);
      } else {
        LOGE("Could not complete transaction %" PRIu32, transaction.id);
      }

      mTransactions.remove(i);
    } else {
      if (transaction.nextRetryTime <= now) {
        bool status = mStartCallback(transaction.data);
        if (status) {
          LOGI("Transaction %" PRIu32 " started", transaction.id);
        } else {
          LOGE("Could not start transaction %" PRIu32, transaction.id);
        }

        ++transaction.numCompletedStartCalls;
        transaction.nextRetryTime = now + mRetryWaitTime;
      }

      nextExecutionTime = std::min(
          nextExecutionTime,
          std::min(transaction.nextRetryTime, transaction.timeoutTime));
      ++i;
    }
  }

  Nanoseconds waitTime = nextExecutionTime - SystemTime::getMonotonicTime();
  if (waitTime.toRawNanoseconds() > 0) {
    mDeferCallback(TransactionManager<TransactionData>::onTimerFired, this,
                   /* extraData= */ nullptr, waitTime, &mTimerHandle);
    CHRE_ASSERT(mTimerHandle != CHRE_TIMER_INVALID);
  }
}

}  // namespace chre

#endif  // CHRE_UTIL_TRANSACTION_MANAGER_IMPL_H_
