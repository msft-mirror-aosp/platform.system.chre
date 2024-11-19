include($ENV{PW_ROOT}/pw_build/pigweed.cmake)
include(backend.cmake)

pw_add_facade(chre.platform.shared.authentication INTERFACE
  BACKEND
    chre.platform.shared.authentication_BACKEND
  HEADERS
    include/chre/platform/shared/authentication.h
  PUBLIC_INCLUDES
    include
)

pw_add_facade(chre.platform.shared.bt_snoop_log INTERFACE
  BACKEND
    chre.platform.shared.bt_snoop_log_BACKEND
  HEADERS
    include/chre/platform/shared/bt_snoop_log.h
  PUBLIC_INCLUDES
    include
)

# Implements chre_api/chre/audio.h's:
# - bool chreAudioGetSource(uint32_t handle,
#                           struct chreAudioSource *audioSource)
# - bool chreAudioConfigureSource(uint32_t handle, bool enable,
#                                 uint64_t bufferDuration,
#                                 uint64_t deliveryInterval)
# - bool chreAudioGetStatus(uint32_t handle,
#                           struct chreAudioSourceStatus *status)
#
pw_add_library(chre.platform.shared.chre_api_audio STATIC
  SOURCES
    chre_api_audio.cc
  PRIVATE_DEPS
    chre.chre_api.facade
    chre.platform.platform_audio
    chre.util
    chre.util.system
)
# Implements chre_api/chre/ble.h's:
# - uint32_t chreBleGetCapabilities()
# - uint32_t chreBleGetFilterCapabilities()
# - bool chreBleFlushAsync(const void *cookie)
# - bool chreBleStartScanAsync(chreBleScanMode mode,
#                              uint32_t reportDelayMs,
#                              const struct chreBleScanFilter *filter)
# - bool chreBleStartScanAsyncV1_9(enum chreBleScanMode mode,
#                                  uint32_t reportDelayMs,
#                                  const struct chreBleScanFilterV1_9 *filter,
#                                  const void *cookie)
# - bool chreBleStopScanAsync()
# - bool chreBleStopScanAsyncV1_9(const void *cookie)
# - bool chreBleReadRssiAsync(uint16_t connectionHandle,
#                             const void *cookie)
# - bool chreBleGetScanStatus(struct chreBleScanStatus *status)
#
pw_add_library(chre.platform.shared.chre_api_ble STATIC
  SOURCES
    chre_api_ble.cc
  PRIVATE_DEPS
    chre.chre_api.facade
    chre.core
    chre.util
    chre.util.system
)

# Implements chre_api/chre/event.h's:
# - bool chreSendEvent(uint16_t eventType, void *eventData,
#                      chreEventCompleteFunction *freeCallback,
#                      uint32_t targetInstanceId)
# - bool chreSendMessageToHost(void *message, uint32_t messageSize,
#                              uint32_t messageType,
#                              chreMessageFreeFunction *freeCallback)
# - bool chreSendMessageToHostEndpoint(
#       void *message, size_t messageSize, uint32_t messageType,
#       uint16_t hostEndpoint, chreMessageFreeFunction *freeCallback)
# - bool chreSendMessageWithPermissions(
#       void *message, size_t messageSize, uint32_t messageType,
#       uint16_t hostEndpoint, uint32_t messagePermissions,
#       chreMessageFreeFunction *freeCallback)
# - bool chreSendReliableMessageAsync(
#       void *message, size_t messageSize, uint32_t messageType,
#       uint16_t hostEndpoint, uint32_t messagePermissions,
#       chreMessageFreeFunction *freeCallback, const void *cookie)
# - bool chreGetNanoappInfoByAppId(uint64_t appId, struct chreNanoappInfo *info)
# - bool chreGetNanoappInfoByInstanceId(uint32_t instanceId,
#                                       struct chreNanoappInfo *info)
# - void chreConfigureNanoappInfoEvents(bool enable)
# - void chreConfigureHostSleepStateEvents(bool enable)
# - bool chreIsHostAwake()
# - void chreConfigureDebugDumpEvent(bool enable)
# - bool chreConfigureHostEndpointNotifications(uint16_t hostEndpointId,
#                                               bool enable)
# - bool chrePublishRpcServices(struct chreNanoappRpcService *services,
#                               size_t numServices)
# - bool chreGetHostEndpointInfo(uint16_t hostEndpointId,
#                                struct chreHostEndpointInfo *info)
#
pw_add_library(chre.platform.shared.chre_api_core STATIC
  SOURCES
    chre_api_core.cc
  PRIVATE_DEPS
    chre.chre_api.facade
    chre.core
    chre.platform.log
    chre.util
    chre.util.system
)

# Implements chre_api/chre/gnss.h's:
# - uint32_t chreGnssGetCapabilities()
# - bool chreGnssLocationSessionStartAsync(
#       uint32_t minIntervalMs, uint32_t minTimeToNextFixMs,
#       const void *cookie)
# - bool chreGnssLocationSessionStopAsync(const void *cookie)
# - bool chreGnssMeasurementSessionStartAsync(
#        uint32_t minIntervalMs, const void *cookie)
# - bool chreGnssMeasurementSessionStopAsync(const void *cookie)
# - bool chreGnssConfigurePassiveLocationListener(bool enable)
#
pw_add_library(chre.platform.shared.chre_api_gnss STATIC
  SOURCES
    chre_api_gnss.cc
  PRIVATE_DEPS
    chre.chre_api.facade
    chre.core
    chre.util
    chre.util.system
)

# Implements chre_api/chre/re.h's:
# - uint32_t chreGetCapabilities()
# - uint32_t chreGetMessageToHostMaxSize()
# - uint64_t chreGetAppId(void)
# - uint32_t chreGetInstanceId(void)
# - uint64_t chreGetTime()
# - int64_t chreGetEstimatedHostTimeOffset()
# - uint32_t chreTimerSet(uint64_t duration, const void *cookie, bool oneShot)
# - bool chreTimerCancel(uint32_t timerId)
# - void chreAbort(uint32_t abortCode)
# - void* chreHeapAlloc(uint32_t bytes)
# - void chreHeapFree(void *ptr)
# - chreDebugDumpLog(const char *formatStr, ...)
#
# Also provides:
# - void platform_chreDebugDumpVaLog(const char *formatStr, va_list args)
#
# Does not provide:
# - void chreLog(enum chreLogLevel level, const char *formatStr, ...)
pw_add_library(chre.platform.shared.chre_api_re STATIC
  HEADERS
    include/chre/platform/shared/debug_dump.h
  PUBLIC_INCLUDES
    include
  SOURCES
    chre_api_re.cc
  PRIVATE_DEPS
    chre.chre_api.facade
    chre.core
    chre.platform.fatal_error
    chre.util
)

# Implements chre_api/chre/sensor.h's:
# - bool chreSensorFindDefault(uint8_t sensorType, uint32_t *handle)
# - bool chreSensorFind(uint8_t sensorType, uint8_t sensorIndex,
#                       uint32_t *handle)
# - bool chreGetSensorInfo(uint32_t sensorHandle, struct chreSensorInfo *info)
# - bool chreGetSensorSamplingStatus(uint32_t sensorHandle,
#                                    struct chreSensorSamplingStatus *status)
# - bool chreSensorConfigure(uint32_t sensorHandle,
#                            enum chreSensorConfigureMode mode,
#                            uint64_t interval, uint64_t latency)
# - bool chreSensorConfigureBiasEvents(uint32_t sensorHandle, bool enable)
# - bool chreSensorGetThreeAxisBias(uint32_t sensorHandle,
#                                   struct chreSensorThreeAxisData *bias)
# - bool chreSensorFlushAsync(uint32_t sensorHandle, const void *cookie)
#
pw_add_library(chre.platform.shared.chre_api_sensor STATIC
  SOURCES
    chre_api_sensor.cc
  PRIVATE_DEPS
    chre.chre_api.facade
    chre.core
    chre.util
)

# Implements chre_api/chre/user_settings.h's:
# - int8_t chreUserSettingGetState(uint8_t setting)
# - void chreUserSettingConfigureEvents(uint8_t setting, bool enable)
#
pw_add_library(chre.platform.shared.chre_api_user_settings STATIC
  SOURCES
    chre_api_user_settings.cc
  PRIVATE_DEPS
    chre.chre_api.facade
    chre.core
    chre.util
)

# Implements chre_api/chre/version.h's:
# - uint32_t chreGetApiVersion(void)
# - uint32_t chreGetVersion(void)
# - uint64_t chreGetPlatformId(void)
#
pw_add_library(chre.platform.shared.chre_api_version STATIC
  SOURCES
    chre_api_version.cc
  PRIVATE_DEPS
    chre.chre_api.facade
    chre.core
    chre.util
)

# Implements chre_api/chre/wifi.h's:
# - uint32_t chreWifiGetCapabilities()
# - bool chreWifiNanGetCapabilities(
#       struct chreWifiNanCapabilities *capabilities)
# - bool chreWifiConfigureScanMonitorAsync(bool enable, const void *cookie)
# - bool chreWifiRequestScanAsync(const struct chreWifiScanParams *params,
#                                 const void *cookie)
# - bool chreWifiRequestRangingAsync(
#       const struct chreWifiRangingParams *params, const void *cookie)
# - bool chreWifiNanSubscribe(struct chreWifiNanSubscribeConfig *config,
#                             const void *cookie)
# - bool chreWifiNanSubscribeCancel(uint32_t subscriptionId)
# - bool chreWifiNanRequestRangingAsync(
#       const struct chreWifiNanRangingParams *params, const void *cookie)
#
pw_add_library(chre.platform.shared.chre_api_wifi STATIC
  SOURCES
    chre_api_wifi.cc
  PRIVATE_DEPS
    chre.chre_api.facade
    chre.core
    chre.util
    chre.util.system
)

# Implements chre_api/chre/wwan.h's:
# - uint32_t chreWwanGetCapabilities()
# - bool chreWwanGetCellInfoAsync(const void *cookie)
#
pw_add_library(chre.platform.shared.chre_api_wwan STATIC
  SOURCES
    chre_api_wwan.cc
  PRIVATE_DEPS
    chre.chre_api.facade
    chre.core
    chre.util
    chre.util.system
)

pw_add_library(chre.platform.shared.dlfcn STATIC
  HEADERS
    include/chre/platform/shared/libc/dlfcn.h
  PUBLIC_INCLUDES
    include/chre/platform/shared/libc
  SOURCES
    dlfcn.cc
  PRIVATE_DEPS
    chre.platform.assert
    chre.platform.log
    chre.platform.shared.nanoapp_loader
    chre.util
)

pw_add_library(chre.platform.shared.generated.host_messages_generated INTERFACE
  HEADERS
    include/chre/platform/shared/generated/host_messages_generated.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre_third_party.flatbuffers
)

pw_add_library(chre.platform.shared.host_protocol_chre STATIC
  HEADERS
    include/chre/platform/shared/host_protocol_chre.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.core
    chre.platform.shared.generated.host_messages_generated
    chre.platform.shared.host_protocol_common
    chre.util
    chre.util.flatbuffers
    chre.chre_api
    chre_third_party.flatbuffers
  SOURCES
    host_protocol_chre.cc
    host_link.cc
  PRIVATE_DEPS
    chre.core
    chre.platform.log
    chre.platform.shared.host_protocol_chre
    chre.platform.shared.nanoapp_load_manager
)

pw_add_library(chre.platform.shared.host_protocol_common STATIC
  HEADERS
    include/chre/platform/shared/host_protocol_common.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.util.system
    chre_third_party.flatbuffers
  SOURCES
    host_protocol_common.cc
  PRIVATE_DEPS
    chre.platform.shared.generated.host_messages_generated
)

pw_add_library(chre.platform.shared.loader_util INTERFACE
  HEADERS
    include/chre/platform/shared/loader_util.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.variant.config
)

pw_add_library(chre.platform.shared.log_buffer STATIC
  HEADERS
    include/chre/platform/shared/log_buffer.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.core
    chre.platform.mutex
    chre.platform.shared.bt_snoop_log
    chre.platform.shared.generated.host_messages_generated
  SOURCES
    log_buffer.cc
  PRIVATE_DEPS
    chre.platform.assert
    chre.util
)

pw_add_library(chre.platform.shared.log_buffer_manager STATIC
  HEADERS
    include/chre/platform/shared/log_buffer_manager.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.chre_api
    chre.platform.assert
    chre.platform.condition_variable
    chre.platform.mutex
    chre.platform.shared.bt_snoop_log.facade
    chre.platform.shared.generated.host_messages_generated
    chre.platform.shared.log_buffer
    chre.util
  SOURCES
    log_buffer_manager.cc
  PRIVATE_DEPS
    chre.core
    chre.platform.log
    chre.util
    pw_log_tokenized.config
    pw_tokenizer
)

pw_add_facade(chre.platform.shared.memory INTERFACE
  BACKEND
    chre.platform.shared.memory_BACKEND
  HEADERS
    include/chre/platform/shared/memory.h
  PUBLIC_INCLUDES
    include
)

# Partial backend for chre.platform.memory_manger which implements:
# 1) void *MemoryManager::nanoappAlloc(Nanoapp *app, uint32_t bytes)
# 2) void MemoryManager::nanoappFree(Nanoapp *app, void *ptr)
# 3) uint32_t MemoryManager::nanoappFreeAll(Nanoapp *app)
# 4) void MemoryManager::logStateToBuffer(DebugDumpWrapper &debugDump) const
pw_add_library(chre.platform.shared.memory_manager STATIC
  SOURCES
    memory_manager.cc
  PRIVATE_DEPS
    chre.platform.assert
    chre.platform.memory_manager.facade
    chre.util.system
)

pw_add_library(chre.platform.shared.nanoapp_dso_util STATIC
  HEADERS
    include/chre/platform/shared/nanoapp_dso_util.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.platform.shared.nanoapp_support_lib_dso
  SOURCES
    nanoapp/nanoapp_dso_util.cc
  PRIVATE_DEPS
    chre.platform.log
)

# This is itself a facade for:
# 1) bool NanoappLoader::relocateTable(DynamicHeader *dyn, int tag)
# 2) bool NanoappLoader::resolveGot()
# 3) if CHREX_SYMBOL_EXTENSIONS is set, providing
#    `const ExportedData chre::kVendorExportedData[]` via
#    `chre/extensions/platform/symbol_list.h`.
pw_add_facade(chre.platform.shared.nanoapp_loader STATIC
  BACKEND
    chre.platform.shared.nanoapp_loader_BACKEND
  HEADERS
    include/chre/platform/shared/nanoapp_loader.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.util
    chre.platform.shared.loader_util
  SOURCES
    nanoapp_loader.cc
  PRIVATE_DEPS
    chre.chre_api
    chre.platform.assert
    chre.platform.fatal_error
    chre.platform.shared.chre_api_re
    chre.platform.shared.memory
    chre.platform.shared.nanoapp_tokenized_log
    chre.platform.shared.platform_cache_management
    chre.variant.config
)

pw_add_library(chre.platform.shared.nanoapp_load_manager STATIC
  HEADERS
    include/chre/platform/shared/nanoapp_load_manager.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.core
    chre.util
  SOURCES
    nanoapp_load_manager.cc
)

# Note that this does NOT actually implement, which are only required for
# nanoapp builds:
# - const struct chreNslNanoappInfo *getChreNslDsoNanoappInfo()
# - bool chreNslDsoGetApi(uint32_t apiId, void **apiHandle)
pw_add_library(chre.platform.shared.nanoapp_support_lib_dso INTERFACE
  HEADERS
    include/chre/platform/shared/nanoapp_support_lib_dso.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.chre_api
)

pw_add_library(chre.platform.shared.nanoapp_tokenized_log STATIC
  HEADERS
    nanoapp/include/chre/platform/shared/nanoapp/tokenized_log.h
  PUBLIC_INCLUDES
    nanoapp/include
  PUBLIC_DEPS
    chre.chre_api
  SOURCES
    tokenized_log.cc
  PRIVATE_DEPS
    chre.core
    chre.platform.log
    chre.platform.shared.log_buffer_manager
    pw_log_tokenized.config
    pw_tokenizer
)

pw_add_library(chre.platform.shared.pal_audio_stub STATIC
  SOURCES
    pal_audio_stub.cc
  PRIVATE_DEPS
    chre.pal.audio.facade
)

pw_add_library(chre.platform.shared.pal_ble_stub STATIC
  SOURCES
    pal_ble_stub.cc
  PRIVATE_DEPS
    chre.pal.ble.facade
)

pw_add_library(chre.platform.shared.pal_gnss_stub STATIC
  SOURCES
    pal_gnss_stub.cc
  PRIVATE_DEPS
    chre.pal.gnss.facade
)

pw_add_library(chre.platform.shared.pal_sensor_stub STATIC
  SOURCES
    pal_sensor_stub.cc
  PRIVATE_DEPS
    chre.pal.sensor.facade
)

pw_add_facade(chre.platform.shared.pal_system_api STATIC
  BACKEND
    chre.platform.shared.pal_system_api_BACKEND
  HEADERS
    include/chre/platform/shared/pal_system_api.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.pal.system
  SOURCES
    pal_system_api.cc
  PRIVATE_DEPS
   chre.platform.log
   chre.platform.memory
   chre.platform.system_time
)

pw_add_library(chre.platform.shared.pal_wifi_stub STATIC
  SOURCES
    pal_wifi_stub.cc
  PRIVATE_DEPS
    chre.pal.wifi.facade
)

pw_add_library(chre.platform.shared.pal_wwan_stub STATIC
  SOURCES
    pal_wwan_stub.cc
  PRIVATE_DEPS
    chre.pal.wwan.facade
)

pw_add_library(chre.platform.shared.platform_ble_pal STATIC
  HEADERS
    public_platform_ble_pal/chre/target_platform/platform_ble_base.h
  PUBLIC_INCLUDES
    public_platform_ble_pal
  PUBLIC_DEPS
    chre.pal.ble
    chre.platform.shared.platform_pal
  SOURCES
    platform_ble.cc
  PRIVATE_DEPS
    chre.chre_api
    chre.core
    chre.platform.log
    chre.platform.platform_ble.facade
    chre.platform.shared.bt_snoop_log
    chre.platform.shared.pal_system_api
    chre.variant.config
)

pw_add_facade(chre.platform.shared.platform_cache_management INTERFACE
  BACKEND
    chre.platform.shared.platform_cache_management_BACKEND
  HEADERS
    include/chre/platform/shared/platform_cache_management.h
  PUBLIC_INCLUDES
    include
)

# The optional CHPP and ASH integration is not supported.
pw_add_library(chre.platform.shared.platform_debug_dump_manager STATIC
  HEADERS
    public_platform_debug_dump_manager/chre/target_platform/platform_debug_dump_manager_base.h
  PUBLIC_INCLUDES
    public_platform_debug_dump_manager
  PUBLIC_DEPS
    chre.variant.config
  SOURCES
    platform_debug_dump_manager.cc
  PRIVATE_DEPS
    chre.core
    chre.platform.host_link
    chre.platform.log
    chre.platform.platform_debug_dump_manager.facade
    chre.util
)

pw_add_library(chre.platform.shared.platform_gnss_pal STATIC
  HEADERS
    public_platform_gnss_pal/chre/target_platform/platform_gnss_base.h
  PUBLIC_INCLUDES
    public_platform_gnss_pal
  PUBLIC_DEPS
    chre.pal.gnss
    chre.platform.shared.platform_pal
  SOURCES
    platform_gnss.cc
  PRIVATE_DEPS
    chre.core
    chre.platform.log
    chre.platform.platform_gnss.facade
    chre.platform.shared.pal_system_api
    chre.variant.config
)

pw_add_facade(chre.platform.shared.platform_pal INTERFACE
  BACKEND
    chre.platform.shared.platform_pal_BACKEND
  HEADERS
    include/chre/platform/shared/platform_pal.h
  PUBLIC_INCLUDES
    include
)

pw_add_library(chre.platform.shared.platform_wifi_pal STATIC
  HEADERS
    public_platform_wifi_pal/chre/target_platform/platform_wifi_base.h
  PUBLIC_INCLUDES
    public_platform_wifi_pal
  PUBLIC_DEPS
    chre.pal.wifi
    chre.platform.shared.platform_pal
  SOURCES
    platform_wifi.cc
  PRIVATE_DEPS
    chre.core
    chre.platform.log
    chre.platform.platform_wifi.facade
    chre.platform.shared.pal_system_api
    chre.util.system
    chre.variant.config
)

pw_add_library(chre.platform.shared.platform_wwan_pal STATIC
  HEADERS
    public_platform_wwan_pal/chre/target_platform/platform_wwan_base.h
  PUBLIC_INCLUDES
    public_platform_wwan_pal
  PUBLIC_DEPS
    chre.pal.wwan
    chre.platform.shared.platform_pal
  SOURCES
    platform_wwan.cc
  PRIVATE_DEPS
    chre.core
    chre.platform.log
    chre.platform.platform_wwan.facade
    chre.platform.shared.pal_system_api
    chre.variant.config
)
