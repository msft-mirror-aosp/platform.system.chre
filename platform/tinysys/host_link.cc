/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "FreeRTOS.h"
#include "encoding.h"
#include "task.h"

#include "chre/core/event_loop_manager.h"
#include "chre/core/host_comms_manager.h"
#include "chre/platform/host_link.h"
#include "chre/platform/log.h"
#include "chre/platform/shared/host_protocol_chre.h"
#include "chre/platform/shared/log_buffer_manager.h"
#include "chre/platform/shared/nanoapp_load_manager.h"
#include "chre/platform/system_time.h"
#include "chre/platform/system_timer.h"
#include "chre/util/flatbuffers/helpers.h"
#include "chre/util/nested_data_ptr.h"
#include "chre_api/chre.h"

#include "dma_api.h"
#include "ipi.h"
#include "ipi_id.h"
#include "resource_req.h"
#include "scp_dram_region.h"

// Because the LOGx macros are being redirected to logcat through
// HostLink::sendLogMessageV2 and HostLink::send, calling them from
// inside HostLink impl could result in endless recursion.
// So redefine them to just printf function to SCP console.
#if CHRE_MINIMUM_LOG_LEVEL >= CHRE_LOG_LEVEL_ERROR
#undef LOGE
#define LOGE(fmt, arg...) PRINTF_E("[CHRE]" fmt "\n", ##arg)
#endif

#if CHRE_MINIMUM_LOG_LEVEL >= CHRE_LOG_LEVEL_WARN
#undef LOGW
#define LOGW(fmt, arg...) PRINTF_W("[CHRE]" fmt "\n", ##arg)
#endif

#if CHRE_MINIMUM_LOG_LEVEL >= CHRE_LOG_LEVEL_INFO
#undef LOGI
#define LOGI(fmt, arg...) PRINTF_I("[CHRE]" fmt "\n", ##arg)
#endif

#if CHRE_MINIMUM_LOG_LEVEL >= CHRE_LOG_LEVEL_DEBUG
#undef LOGD
#define LOGD(fmt, arg...) PRINTF_D("[CHRE]" fmt "\n", ##arg)
#endif

#if CHRE_MINIMUM_LOG_LEVEL >= CHRE_LOG_LEVEL_VERBOSE
#undef LOGV
#define LOGV(fmt, arg...) PRINTF_D("[CHRE]" fmt "\n", ##arg)
#endif

namespace chre {
namespace {

struct UnloadNanoappCallbackData {
  uint64_t appId;
  uint32_t transactionId;
  uint16_t hostClientId;
  bool allowSystemNanoappUnload;
};

SRAM_REGION_BSS uint32_t gChreIpiRecvData[2];

// SCP reply ack data (AP to SCP)
SRAM_REGION_BSS uint32_t gChreIpiAckToHost[2];

// SCP get ack data from AP (SCP to AP)
SRAM_REGION_BSS int gChreIpiAckFromHost[2];

#ifdef SCP_CHRE_USE_DMA
// The min total size of a message to trigger DMA for sending/receiving.
constexpr uint32_t kMinMessageSizeForDma = 0x1000;  // 4k
#endif

// The buffer used to receive messages from AP.
// The size should be consistent with the max sending size on the host side.
constexpr uint32_t kChreIpiRecvBufferSize = 0x8000;  // 32k
DRAM_REGION_VARIABLE uint8_t gChreRecvBuffer[kChreIpiRecvBufferSize]
    __attribute__((aligned(CACHE_LINE_SIZE)));

#ifdef SCP_CHRE_USE_DMA
// Rounds up the value to be aligned with CACHE_LINE_SIZE.
static inline uint32_t alignToCacheLine(uint32_t value) {
  // alignment must be a power of 2.
  static_assert(CACHE_LINE_SIZE > 0 &&
                (CACHE_LINE_SIZE & (CACHE_LINE_SIZE - 1)) == 0);
  return (value + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1);
}
#endif

void *gChreSubregionRecvAddr;
size_t gChreSubregionRecvSize;
void *gChreSubregionSendAddr;
size_t gChreSubregionSendSize;

#define SCP_CHRE_MAGIC 0x67728269
struct ScpChreIpiMsg {
  uint32_t magic;
  uint32_t size;
};

struct NanoappListData {
  ChreFlatBufferBuilder *builder;
  DynamicVector<NanoappListEntryOffset> nanoappEntries;
  uint16_t hostClientId;
};

enum class PendingMessageType {
  Shutdown,
  NanoappMessageToHost,
  HubInfoResponse,
  NanoappListResponse,
  LoadNanoappResponse,
  UnloadNanoappResponse,
  DebugDumpData,
  DebugDumpResponse,
  TimeSyncRequest,
  LowPowerMicAccessRequest,
  LowPowerMicAccessRelease,
  EncodedLogMessage,
  SelfTestResponse,
  MetricLog,
  NanConfigurationRequest,
  PulseRequest,
  PulseResponse,
  NanoappTokenDatabaseInfo,
  MessageDeliveryStatus,
};

struct PendingMessage {
  PendingMessage(PendingMessageType msgType, uint16_t hostClientId) {
    type = msgType;
    data.hostClientId = hostClientId;
  }

  PendingMessage(PendingMessageType msgType,
                 const HostMessage *msgToHost = nullptr) {
    type = msgType;
    data.msgToHost = msgToHost;
  }

  PendingMessage(PendingMessageType msgType, ChreFlatBufferBuilder *builder) {
    type = msgType;
    data.builder = builder;
  }

  PendingMessageType type;
  union {
    const HostMessage *msgToHost;
    uint16_t hostClientId;
    ChreFlatBufferBuilder *builder;
  } data;
};

constexpr size_t kOutboundQueueSize = 100;
SRAM_REGION_VARIABLE FixedSizeBlockingQueue<PendingMessage, kOutboundQueueSize>
    gOutboundQueue;

typedef void(MessageBuilderFunction)(ChreFlatBufferBuilder &builder,
                                     void *cookie);

inline HostCommsManager &getHostCommsManager() {
  return EventLoopManagerSingleton::get()->getHostCommsManager();
}

DRAM_REGION_FUNCTION bool generateMessageFromBuilder(
    ChreFlatBufferBuilder *builder) {
  CHRE_ASSERT(builder != nullptr);
  LOGV("%s: message size %d", __func__, builder->GetSize());
  bool result =
      HostLinkBase::send(builder->GetBufferPointer(), builder->GetSize());

  // clean up
  builder->~ChreFlatBufferBuilder();
  memoryFree(builder);
  return result;
}

DRAM_REGION_FUNCTION bool generateMessageToHost(const HostMessage *message) {
  LOGV("%s: message size %zu", __func__, message->message.size());
  // TODO(b/285219398): ideally we'd construct our flatbuffer directly in the
  // host-supplied buffer
  constexpr size_t kFixedReserveSize = 88;
  ChreFlatBufferBuilder builder(message->message.size() + kFixedReserveSize);
  HostProtocolChre::encodeNanoappMessage(
      builder, message->appId, message->toHostData.messageType,
      message->toHostData.hostEndpoint, message->message.data(),
      message->message.size(), message->toHostData.appPermissions,
      message->toHostData.messagePermissions, message->toHostData.wokeHost);
  bool result =
      HostLinkBase::send(builder.GetBufferPointer(), builder.GetSize());

  // clean up
  getHostCommsManager().onMessageToHostComplete(message);
  return result;
}

DRAM_REGION_FUNCTION int generateHubInfoResponse(uint16_t hostClientId) {
  constexpr size_t kInitialBufferSize = 192;

  constexpr char kHubName[] = "CHRE on Tinysys";
  constexpr char kVendor[] = "Google";
  constexpr char kToolchain[] =
      "Clang " STRINGIFY(__clang_major__) "." STRINGIFY(
          __clang_minor__) "." STRINGIFY(__clang_patchlevel__);
  constexpr uint32_t kLegacyPlatformVersion = 0;
  constexpr uint32_t kLegacyToolchainVersion =
      ((__clang_major__ & 0xFF) << 24) | ((__clang_minor__ & 0xFF) << 16) |
      (__clang_patchlevel__ & 0xFFFF);
  constexpr float kPeakMips = 350;
  constexpr float kStoppedPower = 0;
  constexpr float kSleepPower = 1;
  constexpr float kPeakPower = 15;
  bool supportsReliableMessages =
      IS_BIT_SET(chreGetCapabilities(), CHRE_CAPABILITIES_RELIABLE_MESSAGES);

  // Note that this may execute prior to EventLoopManager::lateInit() completing
  ChreFlatBufferBuilder builder(kInitialBufferSize);
  HostProtocolChre::encodeHubInfoResponse(
      builder, kHubName, kVendor, kToolchain, kLegacyPlatformVersion,
      kLegacyToolchainVersion, kPeakMips, kStoppedPower, kSleepPower,
      kPeakPower, chreGetMessageToHostMaxSize(), chreGetPlatformId(),
      chreGetVersion(), hostClientId, supportsReliableMessages);

  return HostLinkBase::send(builder.GetBufferPointer(), builder.GetSize());
}

DRAM_REGION_FUNCTION bool dequeueMessage(PendingMessage pendingMsg) {
  LOGV("%s: message type %d", __func__, pendingMsg.type);
  bool result = false;
  switch (pendingMsg.type) {
    case PendingMessageType::NanoappMessageToHost:
      result = generateMessageToHost(pendingMsg.data.msgToHost);
      break;

    case PendingMessageType::HubInfoResponse:
      result = generateHubInfoResponse(pendingMsg.data.hostClientId);
      break;
    default:
      result = generateMessageFromBuilder(pendingMsg.data.builder);
      break;
  }
  return result;
}

/**
 * Wrapper function to enqueue a message on the outbound message queue. All
 * outgoing message to the host must be called through this function.
 *
 * @param message The message to send to host.
 *
 * @return true if the message was successfully added to the queue.
 */
DRAM_REGION_FUNCTION bool enqueueMessage(PendingMessage pendingMsg) {
  return gOutboundQueue.push(pendingMsg);
}

/**
 * Helper function that takes care of the boilerplate for allocating a
 * ChreFlatBufferBuilder on the heap and adding it to the outbound message
 * queue.
 *
 * @param msgType Identifies the message while in the outbound queue
 * @param initialBufferSize Number of bytes to reserve when first allocating the
 *        ChreFlatBufferBuilder
 * @param buildMsgFunc Synchronous callback used to encode the FlatBuffer
 *        message. Will not be invoked if allocation fails.
 * @param cookie Opaque pointer that will be passed through to buildMsgFunc
 *
 * @return true if the message was successfully added to the queue
 */
DRAM_REGION_FUNCTION bool buildAndEnqueueMessage(
    PendingMessageType msgType, size_t initialBufferSize,
    MessageBuilderFunction *msgBuilder, void *cookie) {
  LOGV("%s: message type %d, size %zu", __func__, msgType, initialBufferSize);
  bool pushed = false;

  auto builder = MakeUnique<ChreFlatBufferBuilder>(initialBufferSize);
  if (builder.isNull()) {
    LOGE("Couldn't allocate memory for message type %d",
         static_cast<int>(msgType));
  } else {
    msgBuilder(*builder, cookie);

    if (!enqueueMessage(PendingMessage(msgType, builder.get()))) {
      LOGE("Couldn't push message type %d to outbound queue",
           static_cast<int>(msgType));
    } else {
      builder.release();
      pushed = true;
    }
  }

  return pushed;
}

/**
 * FlatBuffer message builder callback used with handleNanoappListRequest()
 */
DRAM_REGION_FUNCTION void buildPulseResponse(ChreFlatBufferBuilder &builder,
                                             void * /*cookie*/) {
  HostProtocolChre::encodePulseResponse(builder);
}

/**
 * FlatBuffer message builder callback used with handleNanoappListRequest()
 */
DRAM_REGION_FUNCTION void buildNanoappListResponse(
    ChreFlatBufferBuilder &builder, void *cookie) {
  LOGV("%s", __func__);
  auto nanoappAdderCallback = [](const Nanoapp *nanoapp, void *data) {
    auto *cbData = static_cast<NanoappListData *>(data);
    HostProtocolChre::addNanoappListEntry(
        *(cbData->builder), cbData->nanoappEntries, nanoapp->getAppId(),
        nanoapp->getAppVersion(), true /*enabled*/, nanoapp->isSystemNanoapp(),
        nanoapp->getAppPermissions(), nanoapp->getRpcServices());
  };

  // Add a NanoappListEntry to the FlatBuffer for each nanoapp
  auto *cbData = static_cast<NanoappListData *>(cookie);
  cbData->builder = &builder;
  EventLoop &eventLoop = EventLoopManagerSingleton::get()->getEventLoop();
  eventLoop.forEachNanoapp(nanoappAdderCallback, cbData);
  HostProtocolChre::finishNanoappListResponse(builder, cbData->nanoappEntries,
                                              cbData->hostClientId);
}

DRAM_REGION_FUNCTION void handleUnloadNanoappCallback(uint16_t /*type*/,
                                                      void *data,
                                                      void * /*extraData*/) {
  auto *cbData = static_cast<UnloadNanoappCallbackData *>(data);
  bool success = false;
  uint16_t instanceId;
  EventLoop &eventLoop = EventLoopManagerSingleton::get()->getEventLoop();
  if (!eventLoop.findNanoappInstanceIdByAppId(cbData->appId, &instanceId)) {
    LOGE("Couldn't unload app ID 0x%016" PRIx64 ": not found", cbData->appId);
  } else {
    success =
        eventLoop.unloadNanoapp(instanceId, cbData->allowSystemNanoappUnload);
  }

  constexpr size_t kInitialBufferSize = 52;
  auto builder = MakeUnique<ChreFlatBufferBuilder>(kInitialBufferSize);
  HostProtocolChre::encodeUnloadNanoappResponse(*builder, cbData->hostClientId,
                                                cbData->transactionId, success);

  if (!enqueueMessage(PendingMessage(PendingMessageType::UnloadNanoappResponse,
                                     builder.get()))) {
    LOGE("Failed to send unload response to host: %x transactionID: 0x%x",
         cbData->hostClientId, cbData->transactionId);
  } else {
    builder.release();
  }

  memoryFree(data);
}

DRAM_REGION_FUNCTION void sendDebugDumpData(uint16_t hostClientId,
                                            const char *debugStr,
                                            size_t debugStrSize) {
  struct DebugDumpMessageData {
    uint16_t hostClientId;
    const char *debugStr;
    size_t debugStrSize;
  };

  auto msgBuilder = [](ChreFlatBufferBuilder &builder, void *cookie) {
    const auto *data = static_cast<const DebugDumpMessageData *>(cookie);
    HostProtocolChre::encodeDebugDumpData(builder, data->hostClientId,
                                          data->debugStr, data->debugStrSize);
  };

  constexpr size_t kFixedSizePortion = 52;
  DebugDumpMessageData data;
  data.hostClientId = hostClientId;
  data.debugStr = debugStr;
  data.debugStrSize = debugStrSize;
  buildAndEnqueueMessage(PendingMessageType::DebugDumpData,
                         kFixedSizePortion + debugStrSize, msgBuilder, &data);
}

DRAM_REGION_FUNCTION void sendDebugDumpResponse(uint16_t hostClientId,
                                                bool success,
                                                uint32_t dataCount) {
  struct DebugDumpResponseData {
    uint16_t hostClientId;
    bool success;
    uint32_t dataCount;
  };

  auto msgBuilder = [](ChreFlatBufferBuilder &builder, void *cookie) {
    const auto *data = static_cast<const DebugDumpResponseData *>(cookie);
    HostProtocolChre::encodeDebugDumpResponse(builder, data->hostClientId,
                                              data->success, data->dataCount);
  };

  constexpr size_t kInitialSize = 52;
  DebugDumpResponseData data;
  data.hostClientId = hostClientId;
  data.success = success;
  data.dataCount = dataCount;
  buildAndEnqueueMessage(PendingMessageType::DebugDumpResponse, kInitialSize,
                         msgBuilder, &data);
}
}  // anonymous namespace

DRAM_REGION_FUNCTION void sendDebugDumpResultToHost(uint16_t hostClientId,
                                                    const char *debugStr,
                                                    size_t debugStrSize,
                                                    bool complete,
                                                    uint32_t dataCount) {
  LOGV("%s: host client id %d", __func__, hostClientId);
  if (debugStrSize > 0) {
    sendDebugDumpData(hostClientId, debugStr, debugStrSize);
  }
  if (complete) {
    sendDebugDumpResponse(hostClientId, /* success= */ true, dataCount);
  }
}

DRAM_REGION_FUNCTION HostLinkBase::HostLinkBase() {
  LOGV("HostLinkBase::%s", __func__);
  initializeIpi();
}

DRAM_REGION_FUNCTION HostLinkBase::~HostLinkBase() {
  LOGV("HostLinkBase::%s", __func__);
}

DRAM_REGION_FUNCTION void HostLinkBase::vChreReceiveTask(void *pvParameters) {
  int i = 0;
  int ret = 0;

  LOGV("%s", __func__);
  while (true) {
    LOGV("%s calling ipi_recv_reply(), Cnt=%d", __func__, i++);
    ret = ipi_recv_reply(IPI_IN_C_HOST_SCP_CHRE, (void *)&gChreIpiAckToHost[0],
                         1);
    if (ret != IPI_ACTION_DONE)
      LOGE("%s ipi_recv_reply() ret = %d", __func__, ret);
    LOGV("%s reply_end", __func__);
  }
}

DRAM_REGION_FUNCTION void HostLinkBase::vChreSendTask(void *pvParameters) {
  while (true) {
    auto msg = gOutboundQueue.pop();
    dequeueMessage(msg);
  }
}

DRAM_REGION_FUNCTION void HostLinkBase::chreIpiHandler(unsigned int id,
                                                       void *prdata, void *data,
                                                       unsigned int len) {
  /* receive magic and cmd */
  struct ScpChreIpiMsg msg = *(struct ScpChreIpiMsg *)data;

  // check the magic number and payload size need to be copy(if need) */
  LOGD("%s: Received a message from AP. Size=%u", __func__, msg.size);
  if (msg.magic != SCP_CHRE_MAGIC) {
    LOGE("Invalid magic number: 0x%x, skip message", msg.magic);
    gChreIpiAckToHost[0] = IPI_NO_MEMORY;
    gChreIpiAckToHost[1] = 0;
    return;
  }

  // Mapping the physical address of share memory for SCP
  uint32_t srcAddr =
      ap_to_scp(reinterpret_cast<uint32_t>(gChreSubregionRecvAddr));

#ifdef SCP_CHRE_USE_DMA
  if (msg.size < kMinMessageSizeForDma) {
    dvfs_enable_DRAM_resource(CHRE_MEM_ID);
    memcpy(static_cast<void *>(gChreRecvBuffer),
           reinterpret_cast<void *>(srcAddr), msg.size);
    dvfs_disable_DRAM_resource(CHRE_MEM_ID);
  } else {
    auto dstAddr = reinterpret_cast<uint32_t>(gChreRecvBuffer);

    // destination address for receiving data is in a cacheable memory, it
    // should be invalidated/flushed before transferring from share buffer to
    // SCP
    scp_dcache_flush(dstAddr, alignToCacheLine(msg.size));

    // Using SCP DMA HW to copy the data from share memory to SCP side.
    // The dstAddr could be a global variables or a SCP heap memory at SRAM/DRAM
    DMA_RESULT result = scp_dma_transaction_dram(dstAddr, srcAddr, msg.size,
                                                 DMA_MEM_ID, NO_RESERVED);

    if (result != DMA_RESULT_DONE) {
      LOGE("Failed to receive a message from AP using DMA");
    }
  }
#else  // SCP_CHRE_USE_DMA

  dvfs_enable_DRAM_resource(CHRE_MEM_ID);
  memcpy(static_cast<void *>(gChreRecvBuffer),
         reinterpret_cast<void *>(srcAddr), msg.size);
  dvfs_disable_DRAM_resource(CHRE_MEM_ID);

#endif  // SCP_CHRE_USE_DMA

  // process the message
  receive(static_cast<HostLinkBase *>(prdata), gChreRecvBuffer, msg.size);

  // After finishing the job, akc the message to host
  gChreIpiAckToHost[0] = IPI_ACTION_DONE;
  gChreIpiAckToHost[1] = msg.size;
}

DRAM_REGION_FUNCTION void HostLinkBase::initializeIpi(void) {
  bool success = false;
  int ret;
  constexpr size_t kBackgroundTaskStackSize = 1024;

#ifdef PRI_CHRE_BACKGROUND
  constexpr UBaseType_t kBackgroundTaskPriority = PRI_CHRE_BACKGROUND;
#else
  constexpr UBaseType_t kBackgroundTaskPriority = 2;
#endif

  // prepared share memory information and register the callback functions
  if (!(ret = scp_get_reserve_mem_by_id(SCP_CHRE_FROM_MEM_ID,
                                        &gChreSubregionRecvAddr,
                                        &gChreSubregionRecvSize))) {
    LOGE("%s: get SCP_CHRE_FROM_MEM_ID memory fail", __func__);
  } else if (!(ret = scp_get_reserve_mem_by_id(SCP_CHRE_TO_MEM_ID,
                                               &gChreSubregionSendAddr,
                                               &gChreSubregionSendSize))) {
    LOGE("%s: get SCP_CHRE_TO_MEM_ID memory fail", __func__);
  } else if (pdPASS != xTaskCreate(vChreReceiveTask, "CHRE_RECEIVE",
                                   kBackgroundTaskStackSize, (void *)0,
                                   kBackgroundTaskPriority, NULL)) {
    LOGE("%s failed to create ipi receiver task", __func__);
  } else if (pdPASS != xTaskCreate(vChreSendTask, "CHRE_SEND",
                                   kBackgroundTaskStackSize, (void *)0,
                                   kBackgroundTaskPriority, NULL)) {
    LOGE("%s failed to create ipi outbound message queue task", __func__);
  } else if (IPI_ACTION_DONE !=
             (ret = ipi_register(IPI_IN_C_HOST_SCP_CHRE, (void *)chreIpiHandler,
                                 (void *)this, (void *)&gChreIpiRecvData[0]))) {
    LOGE("ipi_register IPI_IN_C_HOST_SCP_CHRE failed, %d", ret);
  } else if (IPI_ACTION_DONE !=
             (ret = ipi_register(IPI_OUT_C_SCP_HOST_CHRE, NULL, (void *)this,
                                 (void *)&gChreIpiAckFromHost[0]))) {
    LOGE("ipi_register IPI_OUT_C_SCP_HOST_CHRE failed, %d", ret);
  } else {
    success = true;
  }

  if (!success) {
    FATAL_ERROR("HostLinkBase::initializeIpi() failed");
  }
}

DRAM_REGION_FUNCTION void HostLinkBase::receive(HostLinkBase *instance,
                                                void *message, int messageLen) {
  LOGV("%s: message len %d", __func__, messageLen);

  // TODO(b/277128368): A crude way to initially determine daemon's up - set
  // a flag on the first message received. This is temporary until a better
  // way to do this is available.
  instance->setInitialized(true);

  if (!HostProtocolChre::decodeMessageFromHost(message, messageLen)) {
    LOGE("Failed to decode msg %p of len %u", message, messageLen);
  }
}

DRAM_REGION_FUNCTION bool HostLinkBase::send(uint8_t *data, size_t dataLen) {
#ifndef HOST_LINK_IPI_SEND_TIMEOUT_MS
#define HOST_LINK_IPI_SEND_TIMEOUT_MS 100
#endif
#ifndef HOST_LINK_IPI_RESPONSE_TIMEOUT_MS
#define HOST_LINK_IPI_RESPONSE_TIMEOUT_MS 100
#endif
  LOGV("HostLinkBase::%s: %zu, %p", __func__, dataLen, data);
  struct ScpChreIpiMsg msg;
  msg.magic = SCP_CHRE_MAGIC;
  msg.size = dataLen;

  uint32_t dstAddr =
      ap_to_scp(reinterpret_cast<uint32_t>(gChreSubregionSendAddr));

#ifdef SCP_CHRE_USE_DMA
  if (dataLen < kMinMessageSizeForDma) {
    dvfs_enable_DRAM_resource(CHRE_MEM_ID);
    memcpy(reinterpret_cast<void *>(dstAddr), data, dataLen);
    dvfs_disable_DRAM_resource(CHRE_MEM_ID);
  } else {
    auto srcAddr = reinterpret_cast<uint32_t>(data);
    auto msgSize = reinterpret_cast<uint32_t>(msg.size);

    // Separate the message into 2 parts, copySize and dmaSize, and use memcpy
    // and dma to transfer them respectively. This is needed due to the
    // alignment requirement of the dma transfer.
    uint32_t dmaStartSrcAddr = alignToCacheLine(srcAddr);
    uint32_t copySize = dmaStartSrcAddr - srcAddr;
    uint32_t dmaSize = msgSize - copySize;

    if (copySize > 0) {
      dvfs_enable_DRAM_resource(CHRE_MEM_ID);
      memcpy(reinterpret_cast<void *>(dstAddr), data, copySize);
      dvfs_disable_DRAM_resource(CHRE_MEM_ID);
    }

    // source address for sending data is in a cacheable memory, it should
    // be invalidated/flushed before transferring from SCP to shared buffer
    scp_dcache_flush(dmaStartSrcAddr, alignToCacheLine(dmaSize));

    // Using SCP DMA HW to copy the data from SCP to shared memory.
    // The dstAddr could be a global variables or a SCP heap memory at SRAM/DRAM
    DMA_RESULT result = scp_dma_transaction_dram(
        dstAddr + copySize, dmaStartSrcAddr, dmaSize, DMA_MEM_ID, NO_RESERVED);

    if (result != DMA_RESULT_DONE) {
      LOGE("Failed to receive a message from AP using DMA");
    }
  }
#else
  dvfs_enable_DRAM_resource(CHRE_MEM_ID);
  memcpy(reinterpret_cast<void *>(dstAddr), data, dataLen);
  dvfs_disable_DRAM_resource(CHRE_MEM_ID);
#endif
  // NB: len param for ipi_send is in number of 32-bit words
  int ret = ipi_send_compl(
      IPI_OUT_C_SCP_HOST_CHRE, &msg, sizeof(msg) / sizeof(uint32_t),
      HOST_LINK_IPI_SEND_TIMEOUT_MS, HOST_LINK_IPI_RESPONSE_TIMEOUT_MS);
  if (ret) {
    LOGE("chre ipi send fail(%d)", ret);
  } else {
    /* check ack data for make sure IPI wasn't busy */
    if (gChreIpiAckFromHost[0] == IPI_ACTION_DONE) {
      LOGV("chre ipi send done, you can send another IPI");
    } else if (gChreIpiAckFromHost[0] == IPI_PIN_BUSY) {
      /* you may have to re-send the IPI, or drop this one */
      LOGW(
          "chre ipi send busy, user thread has not wait the IPI until job "
          "finished");
    } else if (gChreIpiAckFromHost[0] == IPI_NO_MEMORY) {
      LOGW("chre ipi send with wrong size(%zu)", dataLen);
    } else {
      LOGW("chre ipi send unknown case: 0x%x", gChreIpiAckFromHost[0]);
    }
  }

  return ret == IPI_ACTION_DONE;
}

DRAM_REGION_FUNCTION void HostLinkBase::sendTimeSyncRequest() {}

DRAM_REGION_FUNCTION void HostLinkBase::sendNanConfiguration(
    bool /* enabled */) {
  LOGE("%s is unsupported", __func__);
}

DRAM_REGION_FUNCTION void HostLinkBase::sendLogMessageV2(
    const uint8_t *logMessage, size_t logMessageSize, uint32_t numLogsDropped) {
  LOGV("%s: size %zu", __func__, logMessageSize);
  struct LogMessageData {
    const uint8_t *logMsg;
    size_t logMsgSize;
    uint32_t numLogsDropped;
  };

  LogMessageData logMessageData{logMessage, logMessageSize, numLogsDropped};

  auto msgBuilder = [](ChreFlatBufferBuilder &builder, void *cookie) {
    const auto *data = static_cast<const LogMessageData *>(cookie);
    HostProtocolChre::encodeLogMessagesV2(
        builder, data->logMsg, data->logMsgSize, data->numLogsDropped);
  };

  constexpr size_t kInitialSize = 128;
  bool result = false;
  if (isInitialized()) {
    result = buildAndEnqueueMessage(
        PendingMessageType::EncodedLogMessage,
        kInitialSize + logMessageSize + sizeof(numLogsDropped), msgBuilder,
        &logMessageData);
  }

#ifdef CHRE_USE_BUFFERED_LOGGING
  if (LogBufferManagerSingleton::isInitialized()) {
    LogBufferManagerSingleton::get()->onLogsSentToHost(result);
  }
#else
  UNUSED_VAR(result);
#endif
}

DRAM_REGION_FUNCTION bool HostLink::sendMessage(HostMessage const *message) {
  LOGV("HostLink::%s size(%zu)", __func__, message->message.size());
  bool success = false;

  if (isInitialized()) {
    success = enqueueMessage(
        PendingMessage(PendingMessageType::NanoappMessageToHost, message));
  } else {
    LOGW("Dropping outbound message: host link not initialized yet");
  }
  return success;
}

DRAM_REGION_FUNCTION bool HostLink::sendMessageDeliveryStatus(
    uint32_t messageSequenceNumber, uint8_t errorCode) {
  struct DeliveryStatusData {
    uint32_t messageSequenceNumber;
    uint8_t errorCode;
  } args{messageSequenceNumber, errorCode};

  auto msgBuilder = [](ChreFlatBufferBuilder &builder, void *cookie) {
    auto args = static_cast<const DeliveryStatusData *>(cookie);
    HostProtocolChre::encodeMessageDeliveryStatus(
        builder, args->messageSequenceNumber, args->errorCode);
  };

  return buildAndEnqueueMessage(PendingMessageType::MessageDeliveryStatus,
                                /* initialBufferSize= */ 64, msgBuilder, &args);
}

// TODO(b/285219398): HostMessageHandlers member function implementations are
// expected to be (mostly) identical for any platform that uses flatbuffers
// to encode messages - refactor the host link to merge the multiple copies
// we currently have.
DRAM_REGION_FUNCTION void HostMessageHandlers::handleNanoappMessage(
    uint64_t appId, uint32_t messageType, uint16_t hostEndpoint,
    const void *messageData, size_t messageDataLen, bool isReliable,
    uint32_t messageSequenceNumber) {
  LOGV("Parsed nanoapp message from host: app ID 0x%016" PRIx64
       ", endpoint "
       "0x%" PRIx16 ", msgType %" PRIu32 ", payload size %zu",
       appId, hostEndpoint, messageType, messageDataLen);

  getHostCommsManager().sendMessageToNanoappFromHost(
      appId, messageType, hostEndpoint, messageData, messageDataLen, isReliable,
      messageSequenceNumber);
}

DRAM_REGION_FUNCTION void HostMessageHandlers::handleMessageDeliveryStatus(
    uint32_t messageSequenceNumber, uint8_t errorCode) {
  getHostCommsManager().completeTransaction(messageSequenceNumber, errorCode);
}

DRAM_REGION_FUNCTION void HostMessageHandlers::handleHubInfoRequest(
    uint16_t hostClientId) {
  LOGV("%s: host client id %d", __func__, hostClientId);
  enqueueMessage(
      PendingMessage(PendingMessageType::HubInfoResponse, hostClientId));
}

DRAM_REGION_FUNCTION void HostMessageHandlers::handleNanoappListRequest(
    uint16_t hostClientId) {
  auto callback = [](uint16_t /*type*/, void *data, void * /*extraData*/) {
    uint16_t cbHostClientId = NestedDataPtr<uint16_t>(data);

    NanoappListData cbData = {};
    cbData.hostClientId = cbHostClientId;

    size_t expectedNanoappCount =
        EventLoopManagerSingleton::get()->getEventLoop().getNanoappCount();
    if (!cbData.nanoappEntries.reserve(expectedNanoappCount)) {
      LOG_OOM();
    } else {
      constexpr size_t kFixedOverhead = 48;
      constexpr size_t kPerNanoappSize = 32;
      size_t initialBufferSize =
          (kFixedOverhead + expectedNanoappCount * kPerNanoappSize);

      buildAndEnqueueMessage(PendingMessageType::NanoappListResponse,
                             initialBufferSize, buildNanoappListResponse,
                             &cbData);
    }
  };

  LOGD("Nanoapp list request from client ID %" PRIu16, hostClientId);
  EventLoopManagerSingleton::get()->deferCallback(
      SystemCallbackType::NanoappListResponse,
      NestedDataPtr<uint16_t>(hostClientId), callback);
}

DRAM_REGION_FUNCTION void HostMessageHandlers::sendFragmentResponse(
    uint16_t hostClientId, uint32_t transactionId, uint32_t fragmentId,
    bool success) {
  struct FragmentedLoadInfoResponse {
    uint16_t hostClientId;
    uint32_t transactionId;
    uint32_t fragmentId;
    bool success;
  };

  auto msgBuilder = [](ChreFlatBufferBuilder &builder, void *cookie) {
    auto *cbData = static_cast<FragmentedLoadInfoResponse *>(cookie);
    HostProtocolChre::encodeLoadNanoappResponse(
        builder, cbData->hostClientId, cbData->transactionId, cbData->success,
        cbData->fragmentId);
  };

  FragmentedLoadInfoResponse response = {
      .hostClientId = hostClientId,
      .transactionId = transactionId,
      .fragmentId = fragmentId,
      .success = success,
  };
  constexpr size_t kInitialBufferSize = 52;
  buildAndEnqueueMessage(PendingMessageType::LoadNanoappResponse,
                         kInitialBufferSize, msgBuilder, &response);
}

DRAM_REGION_FUNCTION void HostMessageHandlers::handlePulseRequest() {
  auto callback = [](uint16_t /*type*/, void * /*data*/, void * /*extraData*/) {
    buildAndEnqueueMessage(PendingMessageType::PulseResponse,
                           /*initialBufferSize= */ 48, buildPulseResponse,
                           /* cookie= */ nullptr);
  };
  EventLoopManagerSingleton::get()->deferCallback(
      SystemCallbackType::PulseResponse, /* data= */ nullptr, callback);
}

DRAM_REGION_FUNCTION void HostMessageHandlers::handleLoadNanoappRequest(
    uint16_t hostClientId, uint32_t transactionId, uint64_t appId,
    uint32_t appVersion, uint32_t appFlags, uint32_t targetApiVersion,
    const void *buffer, size_t bufferLen, const char *appFileName,
    uint32_t fragmentId, size_t appBinaryLen, bool respondBeforeStart) {
  UNUSED_VAR(appFileName);

  loadNanoappData(hostClientId, transactionId, appId, appVersion, appFlags,
                  targetApiVersion, buffer, bufferLen, fragmentId, appBinaryLen,
                  respondBeforeStart);
}

DRAM_REGION_FUNCTION void HostMessageHandlers::handleUnloadNanoappRequest(
    uint16_t hostClientId, uint32_t transactionId, uint64_t appId,
    bool allowSystemNanoappUnload) {
  LOGD("Unload nanoapp request from client %" PRIu16 " (txnID %" PRIu32
       ") for appId 0x%016" PRIx64 " system %d",
       hostClientId, transactionId, appId, allowSystemNanoappUnload);
  auto *cbData = memoryAlloc<UnloadNanoappCallbackData>();
  if (cbData == nullptr) {
    LOG_OOM();
  } else {
    cbData->appId = appId;
    cbData->transactionId = transactionId;
    cbData->hostClientId = hostClientId;
    cbData->allowSystemNanoappUnload = allowSystemNanoappUnload;

    EventLoopManagerSingleton::get()->deferCallback(
        SystemCallbackType::HandleUnloadNanoapp, cbData,
        handleUnloadNanoappCallback);
  }
}

DRAM_REGION_FUNCTION void HostLinkBase::sendNanoappTokenDatabaseInfo(
    uint64_t appId, uint32_t tokenDatabaseOffset, size_t tokenDatabaseSize) {
  constexpr size_t kInitialBufferSize = 56;
  struct DatabaseInfoArgs {
    uint64_t appId;
    uint32_t tokenDatabaseOffset;
    size_t tokenDatabaseSize;
  } args{appId, tokenDatabaseOffset, tokenDatabaseSize};

  auto msgBuilder = [](ChreFlatBufferBuilder &builder, void *cookie) {
    DatabaseInfoArgs *args = static_cast<DatabaseInfoArgs *>(cookie);
    uint16_t instanceId;
    EventLoopManagerSingleton::get()
        ->getEventLoop()
        .findNanoappInstanceIdByAppId(args->appId, &instanceId);
    HostProtocolChre::encodeNanoappTokenDatabaseInfo(
        builder, instanceId, args->appId, args->tokenDatabaseOffset,
        args->tokenDatabaseSize);
  };

  buildAndEnqueueMessage(PendingMessageType::NanoappTokenDatabaseInfo,
                         kInitialBufferSize, msgBuilder, &args);
}

DRAM_REGION_FUNCTION void HostLink::flushMessagesSentByNanoapp(
    uint64_t /* appId */) {
  // Not implemented
}

DRAM_REGION_FUNCTION void HostMessageHandlers::handleTimeSyncMessage(
    int64_t offset) {
  LOGE("%s is unsupported", __func__);
}

DRAM_REGION_FUNCTION void HostMessageHandlers::handleDebugDumpRequest(
    uint16_t hostClientId) {
  LOGV("%s: host client id %d", __func__, hostClientId);
  if (!EventLoopManagerSingleton::get()
           ->getDebugDumpManager()
           .onDebugDumpRequested(hostClientId)) {
    LOGE("Couldn't trigger debug dump process");
    sendDebugDumpResponse(hostClientId, /* success= */ false,
                          /* dataCount= */ 0);
  }
}

DRAM_REGION_FUNCTION void HostMessageHandlers::handleSettingChangeMessage(
    fbs::Setting setting, fbs::SettingState state) {
  // TODO(b/285219398): Refactor handleSettingChangeMessage to shared code
  Setting chreSetting;
  bool chreSettingEnabled;
  if (HostProtocolChre::getSettingFromFbs(setting, &chreSetting) &&
      HostProtocolChre::getSettingEnabledFromFbs(state, &chreSettingEnabled)) {
    EventLoopManagerSingleton::get()->getSettingManager().postSettingChange(
        chreSetting, chreSettingEnabled);
  }
}

DRAM_REGION_FUNCTION void HostMessageHandlers::handleSelfTestRequest(
    uint16_t hostClientId) {
  LOGV("%s: host client id %d", __func__, hostClientId);
}

DRAM_REGION_FUNCTION void HostMessageHandlers::handleNanConfigurationUpdate(
    bool /* enabled */) {
  LOGE("%s is unsupported", __func__);
}

DRAM_REGION_FUNCTION void HostMessageHandlers::handleBtSocketOpen(
    uint16_t /* hostClientId */, uint64_t /* socketId */,
    const char * /* name */, uint64_t /* endpointId */, uint64_t /* hubId */,
    uint32_t /* aclConnectionHandle */, uint32_t /* localCid */,
    uint32_t /* remoteCid */, uint32_t /* psm */, uint32_t /* localMtu */,
    uint32_t /* remoteMtu */, uint32_t /* localMps */, uint32_t /* remoteMps */,
    uint32_t /* initialRxCredits */, uint32_t /* initialTxCredits */) {
  LOGE("BT Socket offload not supported");
}

DRAM_REGION_FUNCTION void sendAudioRequest() {
  auto msgBuilder = [](ChreFlatBufferBuilder &builder, void * /*cookie*/) {
    HostProtocolChre::encodeLowPowerMicAccessRequest(builder);
  };
  constexpr size_t kInitialSize = 32;
  buildAndEnqueueMessage(PendingMessageType::LowPowerMicAccessRequest,
                         kInitialSize, msgBuilder, /* cookie= */ nullptr);
}

DRAM_REGION_FUNCTION void sendAudioRelease() {
  auto msgBuilder = [](ChreFlatBufferBuilder &builder, void * /*cookie*/) {
    HostProtocolChre::encodeLowPowerMicAccessRelease(builder);
  };
  constexpr size_t kInitialSize = 32;
  buildAndEnqueueMessage(PendingMessageType::LowPowerMicAccessRelease,
                         kInitialSize, msgBuilder, /* cookie= */ nullptr);
}

}  // namespace chre
