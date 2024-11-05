include($ENV{PW_ROOT}/pw_build/pigweed.cmake)

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
    chre.platform.fatal_error
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
    chre.platform.assert
    chre.platform.memory
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
