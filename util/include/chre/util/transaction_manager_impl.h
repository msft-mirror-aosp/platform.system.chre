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

// IWYU pragma: private, include "transaction_manager.h"
#include "chre/util/transaction_manager.h"

#include "chre/platform/system_time.h"
#include "chre/util/hash.h"

namespace chre {

template <size_t kMaxTransactions, class TimerPoolType>
TransactionManager<kMaxTransactions, TimerPoolType>::~TransactionManager() {
  if (mTimerHandle != CHRE_TIMER_INVALID) {
    LOGI("At least one pending transaction at destruction");
    mTimerPool.cancelSystemTimer(mTimerHandle);
  }
}

template <size_t kMaxTransactions, class TimerPoolType>
bool TransactionManager<kMaxTransactions, TimerPoolType>::add(uint16_t groupId,
                                                              uint32_t *id) {
  CHRE_ASSERT(id != nullptr);
  CHRE_ASSERT(!mInCallback);

  if (mTransactions.full()) {
    LOGE("Can't add new transaction: storage is full");
    return false;
  }

  if (!mNextTransactionId.has_value()) {
    mNextTransactionId = generatePseudoRandomId();
  }
  *id = (mNextTransactionId.value())++;
  mTransactions.emplace(*id, groupId);

  maybeStartLastTransaction();
  if (mTransactions.size() == 1) {
    setTimerAbsolute(mTransactions.back().timeout);
  }
  return true;
}

template <size_t kMaxTransactions, class TimerPoolType>
bool TransactionManager<kMaxTransactions, TimerPoolType>::remove(
    uint32_t transactionId) {
  CHRE_ASSERT(!mInCallback);
  for (size_t i = 0; i < mTransactions.size(); ++i) {
    Transaction &transaction = mTransactions[i];
    if (transaction.id == transactionId) {
      uint16_t groupId = transaction.groupId;
      mTransactions.remove(i);
      startNextTransactionInGroup(groupId);
      updateTimer();
      return true;
    }
  }
  return false;
}

template <size_t kMaxTransactions, class TimerPoolType>
uint32_t
TransactionManager<kMaxTransactions, TimerPoolType>::generatePseudoRandomId() {
  uint64_t data =
      SystemTime::getMonotonicTime().toRawNanoseconds() +
      static_cast<uint64_t>(SystemTime::getEstimatedHostTimeOffset());
  uint32_t hash =
      fnv1a32Hash(reinterpret_cast<const uint8_t *>(&data), sizeof(data));

  // We mix the top 2 bits back into the middle of the hash to provide a value
  // that leaves a gap of at least ~1 billion sequence numbers before
  // overflowing a signed int32 (as used on the Java side).
  constexpr uint32_t kMask = 0xC0000000;
  constexpr uint32_t kShiftAmount = 17;
  uint32_t extraBits = hash & kMask;
  hash ^= extraBits >> kShiftAmount;
  return hash & ~kMask;
}

template <size_t kMaxTransactions, class TimerPoolType>
void TransactionManager<kMaxTransactions,
                        TimerPoolType>::maybeStartLastTransaction() {
  Transaction &lastTransaction = mTransactions.back();

  for (const Transaction &transaction : mTransactions) {
    if (transaction.groupId == lastTransaction.groupId &&
        transaction.id != lastTransaction.id) {
      // Have at least one pending request for this group, so this transaction
      // will only be started via removeTransaction()
      return;
    }
  }

  startTransaction(lastTransaction);
}

template <size_t kMaxTransactions, class TimerPoolType>
void TransactionManager<kMaxTransactions, TimerPoolType>::
    startNextTransactionInGroup(uint16_t groupId) {
  for (Transaction &transaction : mTransactions) {
    if (transaction.groupId == groupId) {
      startTransaction(transaction);
      return;
    }
  }
}

template <size_t kMaxTransactions, class TimerPoolType>
void TransactionManager<kMaxTransactions, TimerPoolType>::startTransaction(
    Transaction &transaction) {
  CHRE_ASSERT(transaction.attemptCount == 0);
  transaction.attemptCount = 1;
  transaction.timeout = SystemTime::getMonotonicTime() + kTimeout;
  {
    ScopedFlag f(mInCallback);
    mCb.onTransactionAttempt(transaction.id, transaction.groupId);
  }
}

template <size_t kMaxTransactions, class TimerPoolType>
void TransactionManager<kMaxTransactions, TimerPoolType>::updateTimer() {
  mTimerPool.cancelSystemTimer(mTimerHandle);
  if (mTransactions.empty()) {
    mTimerHandle = CHRE_TIMER_INVALID;
  } else {
    Nanoseconds nextTimeout(UINT64_MAX);
    for (const Transaction &transaction : mTransactions) {
      if (transaction.timeout < nextTimeout) {
        nextTimeout = transaction.timeout;
      }
    }
    // If we hit this assert, we only have transactions that haven't been
    // started yet
    CHRE_ASSERT(nextTimeout.toRawNanoseconds() != UINT64_MAX);
    setTimerAbsolute(nextTimeout);
  }
}

template <size_t kMaxTransactions, class TimerPoolType>
void TransactionManager<kMaxTransactions, TimerPoolType>::setTimer(
    Nanoseconds duration) {
  mTimerHandle = mTimerPool.setSystemTimer(
      duration, onTimerExpired, SystemCallbackType::TransactionManagerTimeout,
      /*data=*/this);
}

template <size_t kMaxTransactions, class TimerPoolType>
void TransactionManager<kMaxTransactions, TimerPoolType>::setTimerAbsolute(
    Nanoseconds expiry) {
  constexpr Nanoseconds kMinDelay(100);
  Nanoseconds now = SystemTime::getMonotonicTime();
  Nanoseconds delay = (expiry > now) ? expiry - now : kMinDelay;
  setTimer(delay);
}

template <size_t kMaxTransactions, class TimerPoolType>
void TransactionManager<kMaxTransactions, TimerPoolType>::handleTimerExpiry() {
  mTimerHandle = CHRE_TIMER_INVALID;
  if (mTransactions.empty()) {
    LOGW("Got timer callback with no pending transactions");
    return;
  }

  // - If a transaction has reached its timeout, try again
  // - If a transaction has timed out for the final time, fail it
  //   - If another transaction in the same group is pending, start it
  // - Keep track of the transaction with the shortest timeout, use that to
  //   update the timer
  Nanoseconds now = SystemTime::getMonotonicTime();
  Nanoseconds nextTimeout(UINT64_MAX);
  for (size_t i = 0; i < mTransactions.size(); /* ++i at end of scope */) {
    Transaction &transaction = mTransactions[i];
    if (transaction.timeout <= now) {
      if (++transaction.attemptCount > kMaxAttempts) {
        Transaction transactionCopy = transaction;
        mTransactions.remove(i);  // Invalidates transaction reference
        handleTransactionFailure(transactionCopy);
        // Since mTransactions is FIFO, any pending transactions in this group
        // will appear after this one, so we don't need to restart the loop
        continue;
      } else {
        transaction.timeout = now + kTimeout;
        {
          ScopedFlag f(mInCallback);
          mCb.onTransactionAttempt(transaction.id, transaction.groupId);
        }
      }
    }
    if (transaction.timeout < nextTimeout) {
      nextTimeout = transaction.timeout;
    }
    ++i;
  }

  if (!mTransactions.empty()) {
    setTimerAbsolute(nextTimeout);
  }
}

template <size_t kMaxTransactions, class TimerPoolType>
void TransactionManager<kMaxTransactions, TimerPoolType>::
    handleTransactionFailure(Transaction &transaction) {
  {
    ScopedFlag f(mInCallback);
    mCb.onTransactionFailure(transaction.id, transaction.groupId);
  }
  startNextTransactionInGroup(transaction.groupId);
}

}  // namespace chre

#endif  // CHRE_UTIL_TRANSACTION_MANAGER_IMPL_H_
