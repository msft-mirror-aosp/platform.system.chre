/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "chre/core/event_loop.h"
#include <cinttypes>
#include <cstdint>
#include <cstdlib>
#include <type_traits>

#include "chre/core/event.h"
#include "chre/core/event_loop_manager.h"
#include "chre/core/nanoapp.h"
#include "chre/platform/assert.h"
#include "chre/platform/context.h"
#include "chre/platform/event_loop_hooks.h"
#include "chre/platform/fatal_error.h"
#include "chre/platform/system_time.h"
#include "chre/util/conditional_lock_guard.h"
#include "chre/util/lock_guard.h"
#include "chre/util/system/debug_dump.h"
#include "chre/util/system/event_callbacks.h"
#include "chre/util/system/message_common.h"
#include "chre/util/system/stats_container.h"
#include "chre/util/throttle.h"
#include "chre/util/time.h"
#include "chre_api/chre/version.h"

using ::chre::message::EndpointInfo;
using ::chre::message::EndpointType;
using ::chre::message::RpcFormat;
using ::chre::message::ServiceInfo;

namespace chre {

// Out of line declaration required for nonintegral static types
constexpr Nanoseconds EventLoop::kIntervalWakeupBucket;

namespace {

#ifndef CHRE_STATIC_EVENT_LOOP
using DynamicMemoryPool =
    SynchronizedExpandableMemoryPool<Event, CHRE_EVENT_PER_BLOCK,
                                     CHRE_MAX_EVENT_BLOCKS>;
#endif
// TODO(b/264108686): Make this a compile time parameter.
// How many low priority event to remove if the event queue is full
// and a new event needs to be pushed.
constexpr size_t targetLowPriorityEventRemove = 4;

/**
 * Populates a chreNanoappInfo structure using info from the given Nanoapp
 * instance.
 *
 * @param app A potentially null pointer to the Nanoapp to read from
 * @param info The structure to populate - should not be null, but this function
 *        will handle that input
 *
 * @return true if neither app nor info were null, and info was populated
 */
bool populateNanoappInfo(const Nanoapp *app, struct chreNanoappInfo *info) {
  bool success = false;

  if (app != nullptr && info != nullptr) {
    info->appId = app->getAppId();
    info->version = app->getAppVersion();
    info->instanceId = app->getInstanceId();
    if (app->getTargetApiVersion() >= CHRE_API_VERSION_1_8) {
      CHRE_ASSERT(app->getRpcServices().size() <= Nanoapp::kMaxRpcServices);
      info->rpcServiceCount =
          static_cast<uint8_t>(app->getRpcServices().size());
      info->rpcServices = app->getRpcServices().data();
      memset(&info->reserved, 0, sizeof(info->reserved));
    }
    success = true;
  }

  return success;
}

#ifndef CHRE_STATIC_EVENT_LOOP
/**
 * @return true if a event is a low priority event and is not from nanoapp.
 * Note: data and extraData are needed here to match the
 * matching function signature. Both are not used here, but
 * are used in other applications of
 * SegmentedQueue::removeMatchedFromBack.
 */
bool isNonNanoappLowPriorityEvent(Event *event, void * /* data */,
                                  void * /* extraData */) {
  CHRE_ASSERT_NOT_NULL(event);
  return event->isLowPriority && event->senderInstanceId == kSystemInstanceId;
}

void deallocateFromMemoryPool(Event *event, void *memoryPool) {
  static_cast<DynamicMemoryPool *>(memoryPool)->deallocate(event);
}
#endif

}  // anonymous namespace

bool EventLoop::findNanoappInstanceIdByAppId(uint64_t appId,
                                             uint16_t *instanceId) const {
  CHRE_ASSERT(instanceId != nullptr);
  ConditionalLockGuard<Mutex> lock(mNanoappsLock, !inEventLoopThread());

  bool found = false;
  for (const UniquePtr<Nanoapp> &app : mNanoapps) {
    if (app->getAppId() == appId) {
      *instanceId = app->getInstanceId();
      found = true;
      break;
    }
  }

  return found;
}

void EventLoop::forEachNanoapp(NanoappCallbackFunction *callback, void *data) {
  ConditionalLockGuard<Mutex> lock(mNanoappsLock, !inEventLoopThread());

  for (const UniquePtr<Nanoapp> &nanoapp : mNanoapps) {
    callback(nanoapp.get(), data);
  }
}

void EventLoop::invokeMessageFreeFunction(uint64_t appId,
                                          chreMessageFreeFunction *freeFunction,
                                          void *message, size_t messageSize) {
  Nanoapp *nanoapp = lookupAppByAppId(appId);
  if (nanoapp == nullptr) {
    LOGE("Couldn't find app 0x%016" PRIx64 " for message free callback", appId);
  } else {
    auto prevCurrentApp = mCurrentApp;
    mCurrentApp = nanoapp;
    freeFunction(message, messageSize);
    mCurrentApp = prevCurrentApp;
  }
}

void EventLoop::run() {
  LOGI("EventLoop start");

  while (mRunning) {
    // Events are delivered in a single stage: they arrive in the inbound event
    // queue mEvents (potentially posted from another thread), then within
    // this context these events are distributed to all interested Nanoapps,
    // with their free callback invoked after distribution.
    mEventPoolUsage.addValue(static_cast<uint32_t>(mEvents.size()));

    // mEvents.pop() will be a blocking call if mEvents.empty()
    Event *event = mEvents.pop();
    // Need size() + 1 since the to-be-processed event has already been removed.
    mPowerControlManager.preEventLoopProcess(mEvents.size() + 1);
    distributeEvent(event);

    mPowerControlManager.postEventLoopProcess(mEvents.size());
  }

  // Purge the main queue of events pending distribution. All nanoapps should be
  // prevented from sending events or messages at this point via
  // currentNanoappIsStopping() returning true.
  while (!mEvents.empty()) {
    freeEvent(mEvents.pop());
  }

  // Unload all running nanoapps
  while (!mNanoapps.empty()) {
    unloadNanoappAtIndex(mNanoapps.size() - 1);
  }

  LOGI("Exiting EventLoop");
}

bool EventLoop::startNanoapp(UniquePtr<Nanoapp> &nanoapp) {
  CHRE_ASSERT(!nanoapp.isNull());
  bool success = false;
  auto *eventLoopManager = EventLoopManagerSingleton::get();
  EventLoop &eventLoop = eventLoopManager->getEventLoop();
  uint16_t existingInstanceId;

  if (nanoapp.isNull()) {
    // no-op, invalid argument
  } else if (nanoapp->getTargetApiVersion() <
             CHRE_FIRST_SUPPORTED_API_VERSION) {
    LOGE("Incompatible nanoapp (target ver 0x%" PRIx32
         ", first supported ver 0x%" PRIx32 ")",
         nanoapp->getTargetApiVersion(),
         static_cast<uint32_t>(CHRE_FIRST_SUPPORTED_API_VERSION));
  } else if (eventLoop.findNanoappInstanceIdByAppId(nanoapp->getAppId(),
                                                    &existingInstanceId)) {
    LOGE("App with ID 0x%016" PRIx64 " already exists as instance ID %" PRIu16,
         nanoapp->getAppId(), existingInstanceId);
  } else {
    Nanoapp *newNanoapp = nanoapp.get();
    {
      LockGuard<Mutex> lock(mNanoappsLock);
      success = mNanoapps.push_back(std::move(nanoapp));
      // After this point, nanoapp is null as we've transferred ownership into
      // mNanoapps.back() - use newNanoapp to reference it
    }
    if (!success) {
      LOG_OOM();
    } else {
      mCurrentApp = newNanoapp;
      success = newNanoapp->start();
      mCurrentApp = nullptr;
      if (!success) {
        LOGE("Nanoapp %" PRIu16 " failed to start",
             newNanoapp->getInstanceId());
        unloadNanoapp(newNanoapp->getInstanceId(),
                      /*allowSystemNanoappUnload=*/true,
                      /*nanoappStarted=*/false);
      } else {
        notifyAppStatusChange(CHRE_EVENT_NANOAPP_STARTED, *newNanoapp);

#ifdef CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED
        eventLoopManager->getChreMessageHubManager()
            .getMessageHub()
            .registerEndpoint(newNanoapp->getAppId());
#endif  // CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED
      }
    }
  }

  return success;
}

bool EventLoop::unloadNanoapp(uint16_t instanceId,
                              bool allowSystemNanoappUnload,
                              bool nanoappStarted) {
  bool unloaded = false;

  for (size_t i = 0; i < mNanoapps.size(); i++) {
    if (instanceId == mNanoapps[i]->getInstanceId()) {
      if (!allowSystemNanoappUnload && mNanoapps[i]->isSystemNanoapp()) {
        LOGE("Refusing to unload system nanoapp");
      } else {
        // Make sure all messages sent by this nanoapp at least have their
        // associated free callback processing pending in the event queue (i.e.
        // there are no messages pending delivery to the host)
        EventLoopManagerSingleton::get()
            ->getHostCommsManager()
            .flushNanoappMessages(*mNanoapps[i]);

        // Mark that this nanoapp is stopping early, so it can't send events or
        // messages during the nanoapp event queue flush
        mStoppingNanoapp = mNanoapps[i].get();

        if (nanoappStarted) {
          // Distribute all inbound events we have at this time - here we're
          // interested in handling any message free callbacks generated by
          // flushNanoappMessages()
          flushInboundEventQueue();

          // Post the unload event now (so we can reference the Nanoapp instance
          // directly), but nanoapps won't get it until after the unload
          // completes. No need to notify status change if nanoapps failed to
          // start.
          notifyAppStatusChange(CHRE_EVENT_NANOAPP_STOPPED, *mStoppingNanoapp);
        }

        // Finally, we are at a point where there should not be any pending
        // events or messages sent by the app that could potentially reference
        // the nanoapp's memory, so we are safe to unload it
        unloadNanoappAtIndex(i, nanoappStarted);
        mStoppingNanoapp = nullptr;

        LOGD("Unloaded nanoapp with instanceId %" PRIu16, instanceId);
        unloaded = true;
      }
      break;
    }
  }

  return unloaded;
}

bool EventLoop::removeNonNanoappLowPriorityEventsFromBack(
    [[maybe_unused]] size_t removeNum) {
#ifdef CHRE_STATIC_EVENT_LOOP
  return false;
#else
  if (removeNum == 0) {
    return true;
  }

  size_t numRemovedEvent = mEvents.removeMatchedFromBack(
      isNonNanoappLowPriorityEvent, /* data= */ nullptr,
      /* extraData= */ nullptr, removeNum, deallocateFromMemoryPool,
      &mEventPool);
  if (numRemovedEvent == 0 || numRemovedEvent == SIZE_MAX) {
    LOGW("Cannot remove any low priority event");
  } else {
    mNumDroppedLowPriEvents += numRemovedEvent;
  }
  return numRemovedEvent > 0;
#endif
}

bool EventLoop::hasNoSpaceForHighPriorityEvent() {
  return mEventPool.full() && !removeNonNanoappLowPriorityEventsFromBack(
                                  targetLowPriorityEventRemove);
}

bool EventLoop::distributeEventSync(uint16_t eventType, void *eventData,
                                    uint16_t targetInstanceId,
                                    uint16_t targetGroupMask) {
  CHRE_ASSERT(inEventLoopThread());
  Event event(eventType, eventData,
              /* freeCallback= */ nullptr,
              /* isLowPriority= */ false,
              /* senderInstanceId= */ kSystemInstanceId, targetInstanceId,
              targetGroupMask);
  return distributeEventCommon(&event);
}

// TODO(b/264108686): Refactor this function and postSystemEvent
void EventLoop::postEventOrDie(uint16_t eventType, void *eventData,
                               chreEventCompleteFunction *freeCallback,
                               uint16_t targetInstanceId,
                               uint16_t targetGroupMask) {
  if (mRunning) {
    if (hasNoSpaceForHighPriorityEvent() ||
        !allocateAndPostEvent(eventType, eventData, freeCallback,
                              /* isLowPriority= */ false, kSystemInstanceId,
                              targetInstanceId, targetGroupMask)) {
      CHRE_HANDLE_FAILED_SYSTEM_EVENT_ENQUEUE(
          this, eventType, eventData, freeCallback, kSystemInstanceId,
          targetInstanceId, targetGroupMask);
      FATAL_ERROR("Failed to post critical system event 0x%" PRIx16, eventType);
    }
  } else if (freeCallback != nullptr) {
    freeCallback(eventType, eventData);
  }
}

bool EventLoop::postSystemEvent(uint16_t eventType, void *eventData,
                                SystemEventCallbackFunction *callback,
                                void *extraData) {
  if (!mRunning) {
    return false;
  }

  if (hasNoSpaceForHighPriorityEvent()) {
    CHRE_HANDLE_EVENT_QUEUE_FULL_DURING_SYSTEM_POST(this, eventType, eventData,
                                                    callback, extraData);
    FATAL_ERROR("Failed to post critical system event 0x%" PRIx16
                ": Full of high priority "
                "events",
                eventType);
  }

  Event *event = mEventPool.allocate(eventType, eventData, callback, extraData);
  if (event == nullptr || !mEvents.push(event)) {
    CHRE_HANDLE_FAILED_SYSTEM_EVENT_ENQUEUE(
        this, eventType, eventData, callback, kSystemInstanceId,
        kBroadcastInstanceId, kDefaultTargetGroupMask);
    FATAL_ERROR("Failed to post critical system event 0x%" PRIx16
                ": out of memory",
                eventType);
  }

  return true;
}

bool EventLoop::postLowPriorityEventOrFree(
    uint16_t eventType, void *eventData,
    chreEventCompleteFunction *freeCallback, uint16_t senderInstanceId,
    uint16_t targetInstanceId, uint16_t targetGroupMask) {
  bool eventPosted = false;

  if (mRunning) {
    eventPosted =
        allocateAndPostEvent(eventType, eventData, freeCallback,
                             /* isLowPriority= */ true, senderInstanceId,
                             targetInstanceId, targetGroupMask);
    if (!eventPosted) {
      LOGE("Failed to allocate event 0x%" PRIx16 " to instanceId %" PRIu16,
           eventType, targetInstanceId);
      CHRE_HANDLE_LOW_PRIORITY_ENQUEUE_FAILURE(
          this, eventType, eventData, freeCallback, senderInstanceId,
          targetInstanceId, targetGroupMask);
      ++mNumDroppedLowPriEvents;
    }
  }

  if (!eventPosted && freeCallback != nullptr) {
    freeCallback(eventType, eventData);
  }

  return eventPosted;
}

void EventLoop::stop() {
  auto callback = [](uint16_t /*type*/, void *data, void * /*extraData*/) {
    auto *obj = static_cast<EventLoop *>(data);
    obj->onStopComplete();
  };

  // Stop accepting new events and tell the main loop to finish
  postSystemEvent(static_cast<uint16_t>(SystemCallbackType::Shutdown),
                  /*eventData=*/this, callback, /*extraData=*/nullptr);
}

void EventLoop::onStopComplete() {
  mRunning = false;
}

Nanoapp *EventLoop::findNanoappByInstanceId(uint16_t instanceId) const {
  ConditionalLockGuard<Mutex> lock(mNanoappsLock, !inEventLoopThread());
  return lookupAppByInstanceId(instanceId);
}

Nanoapp *EventLoop::findNanoappByAppId(uint64_t appId) const {
  ConditionalLockGuard<Mutex> lock(mNanoappsLock, !inEventLoopThread());
  return lookupAppByAppId(appId);
}

bool EventLoop::populateNanoappInfoForAppId(
    uint64_t appId, struct chreNanoappInfo *info) const {
  ConditionalLockGuard<Mutex> lock(mNanoappsLock, !inEventLoopThread());
  Nanoapp *app = lookupAppByAppId(appId);
  return populateNanoappInfo(app, info);
}

bool EventLoop::populateNanoappInfoForInstanceId(
    uint16_t instanceId, struct chreNanoappInfo *info) const {
  ConditionalLockGuard<Mutex> lock(mNanoappsLock, !inEventLoopThread());
  Nanoapp *app = lookupAppByInstanceId(instanceId);
  return populateNanoappInfo(app, info);
}

bool EventLoop::currentNanoappIsStopping() const {
  return (mCurrentApp == mStoppingNanoapp || !mRunning);
}

void EventLoop::logStateToBuffer(DebugDumpWrapper &debugDump) const {
  debugDump.print("\nEvent Loop:\n");
  debugDump.print("  Max event pool usage: %" PRIu32 "/%zu\n",
                  mEventPoolUsage.getMax(), kMaxEventCount);
  debugDump.print("  Number of low priority events dropped: %" PRIu32 "\n",
                  mNumDroppedLowPriEvents);

  Nanoseconds timeSince =
      SystemTime::getMonotonicTime() - mTimeLastWakeupBucketCycled;
  uint64_t timeSinceMins =
      timeSince.toRawNanoseconds() / kOneMinuteInNanoseconds;
  uint64_t durationMins =
      kIntervalWakeupBucket.toRawNanoseconds() / kOneMinuteInNanoseconds;
  debugDump.print("  Nanoapp host wakeup tracking: cycled %" PRIu64
                  " mins ago, bucketDuration=%" PRIu64 "mins\n",
                  timeSinceMins, durationMins);

  debugDump.print("\nNanoapps:\n");

  if (mNanoapps.size()) {
    for (const UniquePtr<Nanoapp> &app : mNanoapps) {
      app->logStateToBuffer(debugDump);
    }

    mNanoapps[0]->logMemAndComputeHeader(debugDump);
    for (const UniquePtr<Nanoapp> &app : mNanoapps) {
      app->logMemAndComputeEntry(debugDump);
    }

    mNanoapps[0]->logMessageHistoryHeader(debugDump);
    for (const UniquePtr<Nanoapp> &app : mNanoapps) {
      app->logMessageHistoryEntry(debugDump);
    }
  }
}

void EventLoop::onMatchingNanoappEndpoint(
    const pw::Function<bool(const EndpointInfo &)> &function) {
  ConditionalLockGuard<Mutex> lock(mNanoappsLock, !inEventLoopThread());

  for (const UniquePtr<Nanoapp> &app : mNanoapps) {
    if (function(getEndpointInfoFromNanoappLocked(*app.get()))) {
      break;
    }
  }
}

void EventLoop::onMatchingNanoappService(
    const pw::Function<bool(const EndpointInfo &, const ServiceInfo &)>
        &function) {
  ConditionalLockGuard<Mutex> lock(mNanoappsLock, !inEventLoopThread());

  // Format for legacy service descriptors:
  // serviceDescriptor = FORMAT_STRING(
  //     "chre.nanoapp_0x%016" PRIX64 ".service_0x%016" PRIX64, nanoapp_id,
  //     service_id)
  // The length of the buffer is the length of the string above, plus one for
  // the null terminator.
  // The arguments to the ServiceInfo constructor are specified in the CHRE API.
  // @see chrePublishRpcServices
  constexpr size_t kBufferSize = 59;
  char buffer[kBufferSize];

  for (const UniquePtr<Nanoapp> &app : mNanoapps) {
    const DynamicVector<struct chreNanoappRpcService> &services =
        app->getRpcServices();
    for (const struct chreNanoappRpcService &service : services) {
      std::snprintf(buffer, kBufferSize,
                    "chre.nanoapp_0x%016" PRIX64 ".service_0x%016" PRIX64,
                    app->getAppId(), service.id);
      ServiceInfo serviceInfo(buffer, service.version, /* minorVersion= */ 0,
                              RpcFormat::PW_RPC_PROTOBUF);
      if (function(getEndpointInfoFromNanoappLocked(*app.get()), serviceInfo)) {
        return;
      }
    }
  }
}

std::optional<EndpointInfo> EventLoop::getEndpointInfo(uint64_t appId) {
  ConditionalLockGuard<Mutex> lock(mNanoappsLock, !inEventLoopThread());
  Nanoapp *app = lookupAppByAppId(appId);
  return app == nullptr
             ? std::nullopt
             : std::make_optional(getEndpointInfoFromNanoappLocked(*app));
}

bool EventLoop::allocateAndPostEvent(uint16_t eventType, void *eventData,
                                     chreEventCompleteFunction *freeCallback,
                                     bool isLowPriority,
                                     uint16_t senderInstanceId,
                                     uint16_t targetInstanceId,
                                     uint16_t targetGroupMask) {
  bool success = false;

  Event *event =
      mEventPool.allocate(eventType, eventData, freeCallback, isLowPriority,
                          senderInstanceId, targetInstanceId, targetGroupMask);
  if (event != nullptr) {
    success = mEvents.push(event);
  }
  if (!success) {
    LOG_OOM();
  }

  return success;
}

void EventLoop::deliverNextEvent(const UniquePtr<Nanoapp> &app, Event *event) {
  constexpr Seconds kLatencyThreshold = Seconds(1);
  constexpr Seconds kThrottleInterval(1);
  constexpr uint16_t kThrottleCount = 10;

  // Handle time rollover. If Event ever changes the type used to store the
  // received time, this will need to be updated.
  uint32_t now = Event::getTimeMillis();
  static_assert(
      std::is_same<decltype(event->receivedTimeMillis), const uint16_t>::value);
  if (now < event->receivedTimeMillis) {
    now += UINT16_MAX + 1;
  }
  Milliseconds latency(now - event->receivedTimeMillis);

  if (latency >= kLatencyThreshold) {
    CHRE_THROTTLE(LOGW("Delayed event 0x%" PRIx16 " from instanceId %" PRIu16
                       "->%" PRIu16 " took %" PRIu64 "ms to deliver",
                       event->eventType, event->senderInstanceId,
                       event->targetInstanceId, latency.getMilliseconds()),
                  kThrottleInterval, kThrottleCount,
                  SystemTime::getMonotonicTime());
  }

  // TODO: cleaner way to set/clear this? RAII-style?
  mCurrentApp = app.get();
  app->processEvent(event);
  mCurrentApp = nullptr;
}

void EventLoop::distributeEvent(Event *event) {
  distributeEventCommon(event);
  CHRE_ASSERT(event->isUnreferenced());
  freeEvent(event);
}

bool EventLoop::distributeEventCommon(Event *event) {
  bool eventDelivered = false;
  if (event->targetInstanceId == kBroadcastInstanceId) {
    for (const UniquePtr<Nanoapp> &app : mNanoapps) {
      if (app->isRegisteredForBroadcastEvent(event)) {
        eventDelivered = true;
        deliverNextEvent(app, event);
      }
    }
  } else {
    for (const UniquePtr<Nanoapp> &app : mNanoapps) {
      if (event->targetInstanceId == app->getInstanceId()) {
        eventDelivered = true;
        deliverNextEvent(app, event);
        break;
      }
    }
  }
  // Log if an event unicast to a nanoapp isn't delivered, as this is could be
  // a bug (e.g. something isn't properly keeping track of when nanoapps are
  // unloaded), though it could just be a harmless transient issue (e.g. race
  // condition with nanoapp unload, where we post an event to a nanoapp just
  // after queues are flushed while it's unloading)
  if (!eventDelivered && event->targetInstanceId != kBroadcastInstanceId &&
      event->targetInstanceId != kSystemInstanceId) {
    LOGW("Dropping event 0x%" PRIx16 " from instanceId %" PRIu16 "->%" PRIu16,
         event->eventType, event->senderInstanceId, event->targetInstanceId);
  }
  return eventDelivered;
}

void EventLoop::flushInboundEventQueue() {
  while (!mEvents.empty()) {
    distributeEvent(mEvents.pop());
  }
}

void EventLoop::freeEvent(Event *event) {
  if (event->hasFreeCallback()) {
    // TODO: find a better way to set the context to the creator of the event
    mCurrentApp = lookupAppByInstanceId(event->senderInstanceId);
    event->invokeFreeCallback();
    mCurrentApp = nullptr;
  }

  mEventPool.deallocate(event);
}

Nanoapp *EventLoop::lookupAppByAppId(uint64_t appId) const {
  for (const UniquePtr<Nanoapp> &app : mNanoapps) {
    if (app->getAppId() == appId) {
      return app.get();
    }
  }

  return nullptr;
}

Nanoapp *EventLoop::lookupAppByInstanceId(uint16_t instanceId) const {
  // The system instance ID always has nullptr as its Nanoapp pointer, so can
  // skip iterating through the nanoapp list for that case
  if (instanceId != kSystemInstanceId) {
    for (const UniquePtr<Nanoapp> &app : mNanoapps) {
      if (app->getInstanceId() == instanceId) {
        return app.get();
      }
    }
  }

  return nullptr;
}

void EventLoop::notifyAppStatusChange(uint16_t eventType,
                                      const Nanoapp &nanoapp) {
  auto *info = memoryAlloc<chreNanoappInfo>();
  if (info == nullptr) {
    LOG_OOM();
  } else {
    info->appId = nanoapp.getAppId();
    info->version = nanoapp.getAppVersion();
    info->instanceId = nanoapp.getInstanceId();

    postEventOrDie(eventType, info, freeEventDataCallback);
  }
}

void EventLoop::unloadNanoappAtIndex(size_t index, bool nanoappStarted) {
  const UniquePtr<Nanoapp> &nanoapp = mNanoapps[index];

  // Lock here to prevent the nanoapp instance from being accessed between the
  // time it is ended and fully erased
  LockGuard<Mutex> lock(mNanoappsLock);

  // Let the app know it's going away
  mCurrentApp = nanoapp.get();

  // nanoappEnd() is not invoked for nanoapps that return false in
  // nanoappStart(), per CHRE API
  if (nanoappStarted) {
    nanoapp->end();
  }

  // Cleanup resources.
#ifdef CHRE_WIFI_SUPPORT_ENABLED
  const uint32_t numDisabledWifiSubscriptions =
      EventLoopManagerSingleton::get()
          ->getWifiRequestManager()
          .disableAllSubscriptions(nanoapp.get());
  logDanglingResources("WIFI subscriptions", numDisabledWifiSubscriptions);
#endif  // CHRE_WIFI_SUPPORT_ENABLED

#ifdef CHRE_GNSS_SUPPORT_ENABLED
  const uint32_t numDisabledGnssSubscriptions =
      EventLoopManagerSingleton::get()
          ->getGnssManager()
          .disableAllSubscriptions(nanoapp.get());
  logDanglingResources("GNSS subscriptions", numDisabledGnssSubscriptions);
#endif  // CHRE_GNSS_SUPPORT_ENABLED

#ifdef CHRE_SENSORS_SUPPORT_ENABLED
  const uint32_t numDisabledSensorSubscriptions =
      EventLoopManagerSingleton::get()
          ->getSensorRequestManager()
          .disableAllSubscriptions(nanoapp.get());
  logDanglingResources("Sensor subscriptions", numDisabledSensorSubscriptions);
#endif  // CHRE_SENSORS_SUPPORT_ENABLED

#ifdef CHRE_AUDIO_SUPPORT_ENABLED
  const uint32_t numDisabledAudioRequests =
      EventLoopManagerSingleton::get()
          ->getAudioRequestManager()
          .disableAllAudioRequests(nanoapp.get());
  logDanglingResources("Audio requests", numDisabledAudioRequests);
#endif  // CHRE_AUDIO_SUPPORT_ENABLED

#ifdef CHRE_BLE_SUPPORT_ENABLED
  const uint32_t numDisabledBleScans = EventLoopManagerSingleton::get()
                                           ->getBleRequestManager()
                                           .disableActiveScan(nanoapp.get());
  logDanglingResources("BLE scan", numDisabledBleScans);
#endif  // CHRE_BLE_SUPPORT_ENABLED

#ifdef CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED
  EventLoopManagerSingleton::get()
      ->getChreMessageHubManager()
      .unregisterEndpoint(nanoapp->getAppId());
#endif  // CHRE_MESSAGE_ROUTER_SUPPORT_ENABLED

  const uint32_t numCancelledTimers =
      getTimerPool().cancelAllNanoappTimers(nanoapp.get());
  logDanglingResources("timers", numCancelledTimers);

  const uint32_t numFreedBlocks =
      EventLoopManagerSingleton::get()->getMemoryManager().nanoappFreeAll(
          nanoapp.get());
  logDanglingResources("heap blocks", numFreedBlocks);

  // Destroy the Nanoapp instance
  mNanoapps.erase(index);

  mCurrentApp = nullptr;
}

void EventLoop::handleNanoappWakeupBuckets() {
  Nanoseconds now = SystemTime::getMonotonicTime();
  Nanoseconds duration = now - mTimeLastWakeupBucketCycled;
  if (duration > kIntervalWakeupBucket) {
    mTimeLastWakeupBucketCycled = now;
    for (auto &nanoapp : mNanoapps) {
      nanoapp->cycleWakeupBuckets(now);
    }
  }
}

void EventLoop::logDanglingResources(const char *name, uint32_t count) {
  if (count > 0) {
    LOGE("App 0x%016" PRIx64 " had %" PRIu32 " remaining %s at unload",
         mCurrentApp->getAppId(), count, name);
  }
}

EndpointInfo EventLoop::getEndpointInfoFromNanoappLocked(
    const Nanoapp &nanoapp) {
  return EndpointInfo(
      /* id= */ nanoapp.getAppId(),
      /* name= */ nanoapp.getAppName(),
      /* version= */ nanoapp.getAppVersion(),
      /* type= */ EndpointType::NANOAPP,
      /* requiredPermissions= */ nanoapp.getAppPermissions());
}

}  // namespace chre
