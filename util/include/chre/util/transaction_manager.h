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

#ifndef CHRE_UTIL_TRANSACTION_MANAGER_H_
#define CHRE_UTIL_TRANSACTION_MANAGER_H_

#include <cstdint>

#include "chre/util/array_queue.h"
#include "chre/util/non_copyable.h"
#include "chre/util/optional.h"
#include "chre/util/time.h"

namespace chre {

class TransactionManagerCallback {
 public:
  virtual ~TransactionManagerCallback() = default;

  //! Initiate or retry an operation associated with the given transaction ID
  virtual void onTransactionAttempt(uint32_t transactionId,
                                    uint16_t groupId) = 0;

  //! Invoked when a transaction fails to complete after the max attempt limit
  virtual void onTransactionFailure(uint32_t transactionId,
                                    uint16_t groupId) = 0;
};

/**
 * TransactionManager helps track operations which should be retried if not
 * completed within a set amount of time.
 *
 * Transactions are long running operations identified by an ID. Further,
 * transactions can be grouped to ensure that only one transaction within a
 * group is outstanding at a time.
 *
 * This class is not thread-safe, so the caller must ensure all its methods are
 * invoked on the same thread that TimerPool callbacks are invoked on.
 *
 * Usage summary:
 *  - Call add() to initiate the transaction and assign an ID
 *    - TransactionManager will invoke the onTransactionAttempt() callback,
 *      either synchronously and immediately, or after any previous transactions
 *      in the same group have completed
 *  - Call remove() when the transaction's operation completes or is canceled
 *    - If not called within the timeout, TransactionManager will call
 *      onTransactionAttempt() again
 *    - If the operation times out after the specified maximum number of
 *      attempts, TransactionManager will call onTransactionFailure() and
 *      remove the transaction (note that this is the only circumstance under
 *      which onTransactionFailure() is called)
 *
 * @param kMaxTransactions The maximum number of pending transactions
 * (statically allocated)
 * @param TimerPoolType A chre::TimerPool-like class, which supports methods
 * with the same signature and semantics as TimerPool::setSystemTimer() and
 * TimerPool::cancelSystemTimer()
 */
template <size_t kMaxTransactions, class TimerPoolType>
class TransactionManager : public NonCopyable {
 public:
  /**
   * @param cb Callback
   * @param timerPool TimerPool-like object to use for retry timers
   * @param timeout How long to wait for remove() to be called after
   *        onTransactionAttempt() before trying again or failing
   * @param maxAttempts Maximum number of times to try the transaction before
   *        giving up
   */
  TransactionManager(TransactionManagerCallback &cb, TimerPoolType &timerPool,
                     Nanoseconds timeout, uint8_t maxAttempts = 3)
      : kTimeout(timeout),
        kMaxAttempts(maxAttempts),
        mTimerPool(timerPool),
        mCb(cb) {
    CHRE_ASSERT(timeout.toRawNanoseconds() > 0);
  }

  /**
   * This destructor only guarantees that no transaction callbacks will be
   * invoked after it returns – it does not invoke any callbacks on its own.
   * Users of this class should typically ensure that all pending transactions
   * are cleaned up (i.e. removed) prior to destroying this object.
   */
  ~TransactionManager();

  /**
   * Initiate a transaction, assigning it a globally unique transactionId and
   * invoking the onTransactionAttempt() callback from within this function if
   * it is the only pending transaction in the groupId.
   *
   * This must not be called from within a callback method, like
   * onTransactionFailed().
   *
   * @param groupId ID used to serialize groups of transactions
   * @param[out] transactionId Assigned ID, set prior to calling
   *         onTransactionAttempt()
   * @return false if kMaxTransactions are pending, true otherwise
   */
  bool add(uint16_t groupId, uint32_t *transactionId);

  /**
   * Complete a transaction, by removing it from the active set of transactions.
   *
   * After this returns, it is guaranteed that callbacks will not be invoked for
   * this transaction ID. If another transaction is pending with the same group
   * ID as this one, onTransactionAttempt() is invoked for it from within this
   * function.
   *
   * This should be called on successful completion or cancelation of a
   * transaction, but is automatically handled when a transaction fails due to
   * timeout.
   *
   * This must not be called from within a callback method, like
   * onTransactionAttempt().
   *
   * @param transactionId
   * @return true if the transactionId was found and removed from the queue
   */
  bool remove(uint32_t transactionId);

 private:
  //! Stores transaction-related data.
  struct Transaction {
    Transaction(uint32_t id_, uint16_t groupId_) : id(id_), groupId(groupId_) {}

    uint32_t id;
    uint16_t groupId;

    //! Counts up by 1 on each attempt, 0 when pending first attempt
    uint8_t attemptCount = 0;

    //! Absolute time when the next retry should be attempted or the transaction
    //! should be considered failed. Defaults to max so it's never the next
    //! timeout if something else is active.
    Nanoseconds timeout = Nanoseconds(UINT64_MAX);
  };

  //! RAII helper to set a boolean to true and restore to false at end of scope
  class ScopedFlag {
   public:
    ScopedFlag(bool &flag) : mFlag(flag) {
      mFlag = true;
    }
    ~ScopedFlag() {
      mFlag = false;
    }

   private:
    bool &mFlag;
  };

  const Nanoseconds kTimeout;
  const uint8_t kMaxAttempts;

  TimerPoolType &mTimerPool;
  TransactionManagerCallback &mCb;

  //! Delayed assignment to start at a pseudo-random value
  Optional<uint32_t> mNextTransactionId;

  //! Helps catch misuse, e.g. trying to remove a transaction from a callback
  bool mInCallback = false;

  //! Handle of timer that expires, or CHRE_TIMER_INVALID if none
  uint32_t mTimerHandle = CHRE_TIMER_INVALID;

  //! Set of active transactions
  ArrayQueue<Transaction, kMaxTransactions> mTransactions;

  //! Callback given to mTimerPool, invoked when the next expiring transaction
  //! has timed out
  static void onTimerExpired(uint16_t /*type*/, void *data,
                             void * /*extraData*/) {
    auto *obj =
        static_cast<TransactionManager<kMaxTransactions, TimerPoolType> *>(
            data);
    obj->handleTimerExpiry();
  }

  //! @return a pseudorandom ID for a transaction in the range of [0, 2^30 - 1]
  uint32_t generatePseudoRandomId();

  //! If the last added transaction is the only one in its group, start it;
  //! otherwise do nothing
  void maybeStartLastTransaction();

  //! If there's a pending transaction in this group, start the next one;
  //! otherwise do nothing
  void startNextTransactionInGroup(uint16_t groupId);

  //! Update the transaction state and invoke the attempt callback, but doesn't
  //! set the timer
  void startTransaction(Transaction &transaction);

  //! Updates the timer to the proper state for mTransactions
  void updateTimer();

  //! Sets the timer to expire after a delay
  void setTimer(Nanoseconds delay);

  //! Sets the timer to expire at the given time, or effectively immediately if
  //! expiry is in the past
  void setTimerAbsolute(Nanoseconds expiry);

  //! Processes any timed out transactions and reset the timer as needed
  void handleTimerExpiry();

  //! Invokes the failure callback and starts the next transaction in the group,
  //! but does not remove the transaction (it should already be removed)
  void handleTransactionFailure(Transaction &transaction);
};

}  // namespace chre

#include "chre/util/transaction_manager_impl.h"  // IWYU pragma: export

#endif  // CHRE_UTIL_TRANSACTION_MANAGER_H_
