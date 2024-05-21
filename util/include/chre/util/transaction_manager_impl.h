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

#include <algorithm>
#include <inttypes.h>

#include "chre/platform/system_time.h"
#include "chre/util/hash.h"
#include "chre/util/lock_guard.h"
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
    const TransactionData &data, uint16_t cookie, uint32_t *id) {
  CHRE_ASSERT(id != nullptr);

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
        .timeoutTime = Nanoseconds(0),
        .cookie = cookie,
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
  bool success = kDeferCallback(
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

  if (!success) {
    LOGE("Could not defer callback to process transactions");
  }
}

template <typename TransactionData, size_t kMaxTransactions>
void TransactionManager<TransactionData, kMaxTransactions>::
    doCompleteTransactionLocked(Transaction &transaction) {
  uint8_t errorCode = CHRE_ERROR_TIMEOUT;
  if (transaction.errorCode.has_value()) {
    errorCode = *transaction.errorCode;
  }

  bool success = kCompleteCallback(transaction.data, errorCode);
  if (success) {
    LOGI("Transaction %" PRIu32 " completed with error code: %" PRIu8,
         transaction.id, errorCode);
  } else {
    LOGE("Could not complete transaction %" PRIu32, transaction.id);
  }
}

template <typename TransactionData, size_t kMaxTransactions>
void TransactionManager<TransactionData, kMaxTransactions>::
    doStartTransactionLocked(Transaction &transaction, size_t i,
                             Nanoseconds now) {
  // Ensure only one pending transaction per unique cookie.
  bool canStart = true;
  for (size_t j = 0; j < mTransactions.size(); ++j) {
    if (i != j && mTransactions[j].cookie == transaction.cookie &&
        mTransactions[j].numCompletedStartCalls > 0) {
      canStart = false;
      break;
    }
  }
  if (!canStart) {
    return;
  }

  if (transaction.timeoutTime.toRawNanoseconds() != 0) {
    transaction.timeoutTime = now + kTimeout;
  }

  bool success = kStartCallback(transaction.data);
  if (success) {
    LOGI("Transaction %" PRIu32 " started", transaction.id);
  } else {
    LOGE("Could not start transaction %" PRIu32, transaction.id);
  }

  ++transaction.numCompletedStartCalls;
  transaction.nextRetryTime = now + kRetryWaitTime;
}

template <typename TransactionData, size_t kMaxTransactions>
uint32_t TransactionManager<TransactionData,
                        kMaxTransactions>::generatePseudoRandomId() {
  uint64_t data =
      SystemTime::getMonotonicTime().toRawNanoseconds() +
      static_cast<uint64_t>(SystemTime::getEstimatedHostTimeOffset());
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
    CHRE_ASSERT(kDeferCancelCallback(mTimerHandle));
    mTimerHandle = CHRE_TIMER_INVALID;
  }

  Nanoseconds now = SystemTime::getMonotonicTime();
  Nanoseconds nextExecutionTime(UINT64_MAX);

  {
    LockGuard<Mutex> lock(mMutex);
    if (mTransactions.empty()) {
      return;
    }

    // If a transaction is completed, it will be removed from the queue.
    // The loop continues processing in this case as there may be another
    // transaction that is ready to start with the same cookie that was
    // blocked from starting by the completed transaction.
    bool continueProcessing;
    do {
      continueProcessing = false;
      for (size_t i = 0; i < mTransactions.size();) {
        Transaction &transaction = mTransactions[i];
        if ((transaction.timeoutTime.toRawNanoseconds() != 0 &&
             transaction.timeoutTime <= now) ||
            (transaction.nextRetryTime <= now &&
             transaction.numCompletedStartCalls > kMaxNumRetries) ||
            transaction.errorCode.has_value()) {
          doCompleteTransactionLocked(transaction);
          mTransactions.remove(i);
          continueProcessing = true;
        } else {
          if (transaction.nextRetryTime <= now) {
            doStartTransactionLocked(transaction, i, now);
          }

          nextExecutionTime =
              std::min(nextExecutionTime, transaction.nextRetryTime);
          if (transaction.timeoutTime.toRawNanoseconds() != 0) {
            nextExecutionTime =
                std::min(nextExecutionTime, transaction.timeoutTime);
          }
          ++i;
        }
      }
    } while (continueProcessing);
  }

  Nanoseconds waitTime = nextExecutionTime - SystemTime::getMonotonicTime();
  if (waitTime.toRawNanoseconds() > 0) {
    kDeferCallback(
        TransactionManager<TransactionData, kMaxTransactions>::onTimerFired,
        /* data= */ this, /* extraData= */ nullptr, waitTime, &mTimerHandle);
    CHRE_ASSERT(mTimerHandle != CHRE_TIMER_INVALID);
  }
}

}  // namespace chre

#endif  // CHRE_UTIL_TRANSACTION_MANAGER_IMPL_H_
