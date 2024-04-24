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
#include "chre/util/array_queue.h"
#include "chre/util/lock_guard.h"
#include "chre/util/non_copyable.h"
#include "chre/util/optional.h"
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
 * 3. If the start callback fails or if the transaction is not completed,
 *    TransactionManager will call the start callback again after the retry
 *    wait time.
 * 4. Call completeTransaction with the ID.
 * 5. TransactionManager will call the complete callback with the data.
 *
 * If completeTransaction is not called before the timeout, the transaction
 * will be completed with a CHRE_ERROR_TIMEOUT.
 *
 * Ensure the thread processing the deferred callbacks is completed before the
 * destruction of the TransactionManager.
 *
 * @param TransactionData The data passed to the start and complete callbacks.
 * @param kMaxTransactions The maximum number of pending transactions.
 */
template <typename TransactionData, size_t kMaxTransactions>
class TransactionManager : public NonCopyable {
 public:
  /**
   * Type of the callback called on transaction completion. This callback is
   * called in the defer callback thread.
   *
   * This callback cannot call any of the TransactionManager methods.
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
   * called in the defer callback thread.
   *
   * This callback cannot call any of the TransactionManager methods.
   *
   * @param data The data for the transaction.
   * @return whether the callback succeeded.
   */
  using StartCallback =
      typename std::conditional<std::is_pointer<TransactionData>::value ||
                                    std::is_fundamental<TransactionData>::value,
                                bool (*)(TransactionData data),
                                bool (*)(TransactionData &data)>::type;

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
   * This callback cannot call any of the TransactionManager methods.
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
   * This callback cannot call any of the TransactionManager methods.
   *
   * @param timerHandle the timer handle returned using the DeferCallback.
   * @return whether the callback was successfully cancelled.
   */
  using DeferCancelCallback = bool (*)(uint32_t timerHandle);

  /**
   * The callback used to determine which elements to remove
   * during a flush.
   *
   * This callback cannot call any of the TransactionManager methods.
   */
  using FlushCallback = typename std::conditional<
      std::is_pointer<TransactionData>::value ||
          std::is_fundamental<TransactionData>::value,
      bool (*)(TransactionData data, void *callbackData),
      bool (*)(const TransactionData &data, void *callbackData)>::type;

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

    transactionManagerPtr->mTimerHandle = CHRE_TIMER_INVALID;
    transactionManagerPtr->processTransactions();
  }

  TransactionManager() = delete;

  TransactionManager(StartCallback startCallback,
                     CompleteCallback completeCallback,
                     DeferCallback deferCallback,
                     DeferCancelCallback deferCancelCallback,
                     Nanoseconds retryWaitTime, uint16_t maxNumRetries = 3)
      : mStartCallback(startCallback),
        mCompleteCallback(completeCallback),
        mDeferCallback(deferCallback),
        mDeferCancelCallback(deferCancelCallback),
        mRetryWaitTime(retryWaitTime),
        mMaxNumRetries(maxNumRetries) {
    CHRE_ASSERT(startCallback != nullptr);
    CHRE_ASSERT(completeCallback != nullptr);
    CHRE_ASSERT(deferCallback != nullptr);
    CHRE_ASSERT(deferCancelCallback != nullptr);
    CHRE_ASSERT(retryWaitTime.toRawNanoseconds() > 0);
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
   * @param data The data to be passed to the flush callback.
   * @return The number of flushed transactions.
   */
  size_t flushTransactions(FlushCallback flushCallback, void *data);

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

 private:
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
   * Defers processing transactions in the defer callback thread.
   */
  void deferProcessTransactions();

  /**
   * Generates a pseudo random ID for a transaction in the range of
   * [0, 2^30 - 1].
   * @return The generated ID.
   */
  uint32_t generatePseudoRandomId();

  /**
   * Processes transactions. This function will call the start callback and
   * complete callback where appropriate and keep track of which transactions
   * need to be retried next. This function is called in the defer callback
   * thread and will defer a call to itself at the next time needed to processes
   * the next transaction.
   */
  void processTransactions();

  //! The start callback.
  const StartCallback mStartCallback;

  //! The complete callback.
  const CompleteCallback mCompleteCallback;

  //! The defer callback.
  const DeferCallback mDeferCallback;

  //! The defer cancel callback.
  const DeferCancelCallback mDeferCancelCallback;

  //! The mutex protecting mTransactions and mTimerHandle.
  Mutex mMutex;

  //! The next ID for use when creating a transaction.
  Optional<uint32_t> mNextTransactionId;

  //! The retry wait time.
  const Nanoseconds mRetryWaitTime;

  //! The maximum number of retries for a transaction.
  const uint16_t mMaxNumRetries;

  //! The timer handle for the timer tracking execution of processTransactions.
  //! Can only be modified in the defer callback thread.
  uint32_t mTimerHandle = CHRE_TIMER_INVALID;

  //! The list of transactions.
  ArrayQueue<Transaction, kMaxTransactions> mTransactions;
};

}  // namespace chre

#include "chre/util/transaction_manager_impl.h"

#endif  // CHRE_UTIL_TRANSACTION_MANAGER_H_
