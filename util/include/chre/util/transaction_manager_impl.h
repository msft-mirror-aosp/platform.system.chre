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
#include "chre/util/hash.h"
#include "chre/util/lock_guard.h"
#include "chre/util/nested_data_ptr.h"
#include "chre/util/time.h"
#include "chre/util/transaction_manager.h"
#include "chre_api/chre.h"

namespace chre {

using ::chre::Nanoseconds;
using ::chre::Seconds;

template <typename TransactionData, size_t kMaxTransactions>
bool TransactionManager<TransactionData, kMaxTransactions>::completeTransaction(
    uint32_t transactionId, uint8_t errorCode) {
  bool success = false;

  {
    LockGuard<Mutex> lock(mMutex);
    for (size_t i = 0; i < mTransactions.size(); ++i) {
      Transaction &transaction = mTransactions[i];
      if (transaction.id == transactionId) {
        if (errorCode == CHRE_ERROR_TRANSIENT) {
          transaction.nextRetryTime = Nanoseconds(0);
        } else {
          transaction.errorCode = errorCode;
        }
        success = true;
        break;
      }
    }
  }

  if (success) {
    deferProcessTransactions();
  } else {
    LOGE("Unable to complete transaction with ID: %" PRIu32, transactionId);
  }
  return success;
}

template <typename TransactionData, size_t kMaxTransactions>
size_t TransactionManager<TransactionData, kMaxTransactions>::flushTransactions(
    FlushCallback callback, void *data) {
  if (callback == nullptr) {
    return 0;
  }

  deferProcessTransactions();

  LockGuard<Mutex> lock(mMutex);
  size_t numFlushed = 0;
  for (size_t i = 0; i < mTransactions.size();) {
    if (callback(mTransactions[i].data, data)) {
      mTransactions.remove(i);
      ++numFlushed;
    } else {
      ++i;
    }
  }
  return numFlushed;
}

template <typename TransactionData, size_t kMaxTransactions>
bool TransactionManager<TransactionData, kMaxTransactions>::startTransaction(
    const TransactionData &data, Nanoseconds timeout, uint32_t *id) {
  CHRE_ASSERT(id != nullptr);

  if (timeout.toRawNanoseconds() != 0 && timeout <= mRetryWaitTime) {
    LOGE("Timeout: %" PRIu64 "ns is <= retry wait time: %" PRIu64 "ns",
         timeout.toRawNanoseconds(), mRetryWaitTime.toRawNanoseconds());
    return false;
  }

  {
    LockGuard<Mutex> lock(mMutex);
    if (mTransactions.full()) {
      LOGE("The transaction queue is full");
      return false;
    }

    if (!mNextTransactionId.has_value()) {
      mNextTransactionId = generatePseudoRandomId();
    }
    uint32_t transactionId = (mNextTransactionId.value())++;
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

    mTransactions.push(transaction);
  }

  deferProcessTransactions();
  return true;
}

template <typename TransactionData, size_t kMaxTransactions>
void TransactionManager<TransactionData,
                        kMaxTransactions>::deferProcessTransactions() {
  bool status = mDeferCallback(
      [](uint16_t /* type */, void *data, void * /* extraData */) {
        auto transactionManagerPtr = static_cast<TransactionManager *>(data);
        if (transactionManagerPtr == nullptr) {
          LOGE("Could not get transaction manager to process transactions");
          return;
        }

        transactionManagerPtr->processTransactions();
      },
      this,
      /* extraData= */ nullptr,
      /* delay= */ Nanoseconds(0),
      /* outTimerHandle= */ nullptr);

  if (!status) {
    LOGE("Could not defer callback to process transactions");
  }
}

template <typename TransactionData, size_t kMaxTransactions>
uint32_t TransactionManager<TransactionData,
                        kMaxTransactions>::generatePseudoRandomId() {
  uint64_t data = SystemTime::getMonotonicTime().toRawNanoseconds() +
      SystemTime::getEstimatedHostTimeOffset();
  uint32_t hash = fnv1a32Hash(reinterpret_cast<const uint8_t*>(&data),
                              sizeof(uint64_t));

  // We mix the top 2 bits back into the middle of the hash to provide a value
  // that leaves a gap of at least ~1 billion sequence numbers before
  // overflowing a signed int32 (as used on the Java side).
  constexpr uint32_t kMask = 0xC0000000;
  constexpr uint32_t kShiftAmount = 17;
  uint32_t extraBits = hash & kMask;
  hash ^= extraBits >> kShiftAmount;
  return hash & ~kMask;
}

template <typename TransactionData, size_t kMaxTransactions>
void TransactionManager<TransactionData,
                        kMaxTransactions>::processTransactions() {
  if (mTimerHandle != CHRE_TIMER_INVALID) {
    CHRE_ASSERT(mDeferCancelCallback(mTimerHandle));
    mTimerHandle = CHRE_TIMER_INVALID;
  }

  Nanoseconds now = SystemTime::getMonotonicTime();
  Nanoseconds nextExecutionTime(UINT64_MAX);

  {
    LockGuard<Mutex> lock(mMutex);
    if (mTransactions.empty()) {
      return;
    }

    for (size_t i = 0; i < mTransactions.size();) {
      Transaction &transaction = mTransactions[i];
      if (transaction.timeoutTime <= now ||
          (transaction.nextRetryTime <= now &&
           transaction.numCompletedStartCalls >= mMaxNumRetries + 1) ||
          transaction.errorCode.has_value()) {
        uint8_t errorCode = CHRE_ERROR_TIMEOUT;
        if (transaction.errorCode.has_value()) {
          errorCode = *transaction.errorCode;
        }

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
  }

  Nanoseconds waitTime = nextExecutionTime - SystemTime::getMonotonicTime();
  if (waitTime.toRawNanoseconds() > 0) {
    mDeferCallback(
        TransactionManager<TransactionData, kMaxTransactions>::onTimerFired,
        /* data= */ this, /* extraData= */ nullptr, waitTime, &mTimerHandle);
    CHRE_ASSERT(mTimerHandle != CHRE_TIMER_INVALID);
  }
}

}  // namespace chre

#endif  // CHRE_UTIL_TRANSACTION_MANAGER_IMPL_H_
