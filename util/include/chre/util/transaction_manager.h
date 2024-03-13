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
#include <type_traits>

#include "chre/platform/mutex.h"
#include "chre/util/lock_guard.h"
#include "chre/util/nested_data_ptr.h"
#include "chre/util/non_copyable.h"
#include "chre/util/optional.h"
#include "chre/util/segmented_queue.h"
#include "chre/util/time.h"

namespace chre {

/**
 * TransactionManager tracks pending transactions.
 *
 * Transactions are long running operations identified by an ID.
 * The TransactionManager makes sure that the transactions will complete only
 * once after a call to completeTransaction or after the optional timeout
 * expires whichever comes first. A transaction will be retried by calling
 * the start callback after no complete call has been made before the retry
 * wait time.
 *
 * Typical usage:
 * 1. Start a transaction. Get the ID back.
 * 2. TransactionManager will run start callback with the data.
 * 3. TransactionManager will retry if needed.
 * 4. Call completeTransaction with the ID.
 * 5. TransactionManager will call the complete callback with the data.
 *
 * @param TransactionData The data passed to the start and complete callbacks.
 */
template <typename TransactionData>
class TransactionManager : public NonCopyable {
 public:
  /**
   * Type of the callback called on transaction completion. This callback is
   * called  in the event loop thread.
   *
   * @param data The data for the transaction.
   * @param errorCode The error code passed to completeTransaction.
   * @return whether the callback succeeded.
   */
  using CompleteCallback = typename std::conditional<
      std::is_pointer<TransactionData>::value ||
          std::is_fundamental<TransactionData>::value,
      bool (*)(TransactionData data, uint8_t errorCode),
      bool (*)(const TransactionData &data, uint8_t errorCode)>::type;

  /**
   * Type of the callback called to start the transaction. This is the action
   * that will be repeated on a retry of the transaction. This callback is
   * called in the event loop thread.
   *
   * @param data The data for the transaction.
   * @return whether the callback succeeded.
   */
  using StartCallback =
      typename std::conditional<std::is_pointer<TransactionData>::value ||
                                    std::is_fundamental<TransactionData>::value,
                                bool (*)(TransactionData data),
                                bool (*)(const TransactionData &data)>::type;

  /**
   * The type of function used to defer a callback. See DeferCallback.
   *
   * @param type The type passed from the DeferCallback.
   * @param data The data passed from the DeferCallback.
   * @param extraData The extra data passed from the DeferCallback.
   */
  using DeferCallbackFunction = void (*)(uint16_t type, void *data,
                                         void *extraData);

  /**
   * Type of the callback used to defer the call of func with data and extraData
   * after waiting for delay. extraData is ignored if delay > 0 ns.
   *
   * @param func The function to call when the callback is executed.
   * @param data The data to pass to the function.
   * @param extraData The extra data to pass to the function.
   * @param delay The nanoseconds delay to wait before calling the function.
   * @param outTimerHandle The output timer handle if delay > 0 ns.
   * @return whether the callback succeeded.
   */
  using DeferCallback = bool (*)(DeferCallbackFunction func, void *data,
                                 void *extraData, Nanoseconds delay,
                                 uint32_t *outTimerHandle);

  /**
   * Type of the callback used to cancel a defer call made using the
   * DeferCallback.
   *
   * @param timerHandle the timer handle returned using the DeferCallback.
   * @return whether the callback was successfully cancelled.
   */
  using DeferCancelCallback = bool (*)(uint32_t timerHandle);

  /**
   * The callback used to determine which elements to remove
   * during a flush.
   */
  using FlushCallback = StartCallback;

  /**
   * The base wait time for a retry. This is used during the transaction retry
   * process.
   */
  static constexpr chre::Nanoseconds kDefaultRetryWaitTime =
      chre::Nanoseconds(chre::Seconds(1));

  /**
   * The maximum number of retries for a transaction.
   */
  static constexpr uint16_t kMaxNumRetries = 3;

  /**
   * The function called when the transaction processing timer is fired.
   * @see DeferCallbackFunction() for parameter information.
   */
  static void onTimerFired(uint16_t /* type */, void *data,
                           void * /* extraData */) {
    auto transactionManagerPtr = static_cast<TransactionManager *>(data);
    if (transactionManagerPtr == nullptr) {
      LOGE("Could not get transaction manager to process transactions");
      return;
    }

    transactionManagerPtr->deferProcessTransactions(data,
                                                    /* timerFired= */ true);
  }

  TransactionManager() = delete;

  TransactionManager(StartCallback startCallback,
                     CompleteCallback completeCallback,
                     DeferCallback deferCallback,
                     DeferCancelCallback deferCancelCallback)
      : mStartCallback(startCallback),
        mCompleteCallback(completeCallback),
        mDeferCallback(deferCallback),
        mDeferCancelCallback(deferCancelCallback) {
    CHRE_ASSERT(startCallback != nullptr);
    CHRE_ASSERT(completeCallback != nullptr);
    CHRE_ASSERT(deferCallback != nullptr);
    CHRE_ASSERT(deferCancelCallback != nullptr);
  }

  ~TransactionManager() {
    LockGuard lock(mMutex);

    while (!mTransactions.empty()) {
      mTransactions.pop();
    }

    if (mTimerHandle != CHRE_TIMER_INVALID) {
      mDeferCancelCallback(mTimerHandle);
      mTimerHandle = CHRE_TIMER_INVALID;
    }
  }

  /**
   * Completes a transaction.
   *
   * The callback registered when starting the transaction is called with the
   * errorCode if the error is not CHRE_ERROR_TRANSIENT. If the error is
   * CHRE_ERROR_TRANSIENT, this function marks the transaction as ready
   * to retry and processes transactions.
   *
   * This function is safe to call in any thread.
   *
   * Note that the callback will be called at most once on the first call to
   * this method. For example if the transaction timed out before an explicit
   * call to completeTransaction, the callback is only invoked for the timeout.
   *
   * @param transactionId ID of the transaction to complete.
   * @param errorCode Error code to pass to the callback.
   * @return Whether the transaction was completed successfully.
   */
  bool completeTransaction(uint32_t transactionId, uint8_t errorCode);

  /**
   * Flushes all the pending transactions that match the FlushCallback.
   *
   * This function is safe to call in any thread.
   *
   * The completion callback is not called.
   *
   * @param flushCallback The function that determines which transactions will
   * be flushed (upon return true).
   * @return The number of flushed transactions.
   */
  uint32_t flushTransactions(FlushCallback *flushCallback);

  /**
   * Starts a transaction. This function will mark the transaction as ready to
   * execute the StartCallback and processes transactions.
   *
   * This function is safe to call in any thread.
   *
   * @param data The transaction data and callbacks used to run the transaction.
   * @param timeout The transaction timeout. The transaction will complete with
   *        a CHRE_ERROR_TIMEOUT if completeTransaction has not been called
   *        before the timeout. This must be greater than the retry time.
   * @param id A pointer to the transaction ID that will be populated when
   *        startTransaction succeed. It must not be null.
   * @return Whether the transaction was started successfully.
   */
  bool startTransaction(const TransactionData &data, Nanoseconds timeout,
                        uint32_t *id);

  /**
   * Returns the retry wait time.
   *
   * @return The retry wait time.
   */
  Nanoseconds getRetryWaitTime() {
    LockGuard lock(mMutex);
    return mRetryWaitTime;
  }

  /**
   * Sets the retry time for the retry functionality. This is the amount of time
   * between retries for a transaction.
   *
   * This function is safe to call in any thread.
   *
   * @param waitTime The retry wait time.
   */
  void setRetryWaitTime(Nanoseconds waitTime) {
    LockGuard lock(mMutex);
    mRetryWaitTime = waitTime;
  }

  /**
   * Defers processing transactions in the event loop thread.
   *
   * @param data The pointer to the TransactionManager.
   * @param timerFired If the timer fired.
   */
  void deferProcessTransactions(void *data, bool timerFired);

 private:
  //! Size of a single block for the transaction queue.
  static constexpr size_t kTransactionQueueBlockSize = 64;

  //! Number of blocks for the transaction queue.
  static constexpr size_t kTransactionQueueNumBlocks = 5;

  //! Stores transaction-related data.
  struct Transaction {
    uint32_t id;
    TransactionData data;
    Nanoseconds nextRetryTime;
    Nanoseconds timeoutTime;
    uint16_t numCompletedStartCalls;
    Optional<uint8_t> errorCode;
  };

  /**
   * Processes transactions. This function will call the start callback and
   * complete callback where appropriate and keep track of which transactions
   * need to be retried next. This function is called in the event loop thread
   * and will defer a call to itself at the next time needed to processes the
   * next transaction.
   *
   * @param timerFired If the timer fired.
   */
  void processTransactions(bool timerFired);

  //! The start callback.
  StartCallback mStartCallback;

  //! The complete callback.
  CompleteCallback mCompleteCallback;

  //! The defer callback.
  DeferCallback mDeferCallback;

  //! The defer cancel callback.
  DeferCancelCallback mDeferCancelCallback;

  //! The mutex protecting mTransactions and mTimerHandle.
  Mutex mMutex;

  //! The next ID for use when creating a transaction.
  uint32_t mNextTransactionId = 0;

  //! The retry wait time.
  Nanoseconds mRetryWaitTime = kDefaultRetryWaitTime;

  //! The timer handle for the timer tracking execution of processTransactions.
  uint32_t mTimerHandle = CHRE_TIMER_INVALID;

  //! The list of transactions.
  SegmentedQueue<Transaction, kTransactionQueueBlockSize> mTransactions{
      kTransactionQueueNumBlocks};
};

}  // namespace chre

#include "chre/util/transaction_manager_impl.h"

#endif  // CHRE_UTIL_TRANSACTION_MANAGER_H_
