include($ENV{PW_ROOT}/pw_build/pigweed.cmake)
include(backend.cmake)

pw_add_facade(chre.platform.assert INTERFACE
  BACKEND
    chre.platform.assert_BACKEND
  HEADERS
    include/chre/platform/assert.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.platform.log
    chre.variant.config
)

pw_add_facade(chre.platform.atomic INTERFACE
  BACKEND
    chre.platform.atomic_BACKEND
  HEADERS
    include/chre/platform/atomic.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.util
    chre.variant.config
)

pw_add_facade(chre.platform.condition_variable INTERFACE
  BACKEND
    chre.platform.condition_variable_BACKEND
  HEADERS
    include/chre/platform/condition_variable.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.platform.mutex
    chre.util
    chre.variant.config
)

pw_add_facade(chre.platform.context INTERFACE
  BACKEND
    chre.platform.context_BACKEND
  HEADERS
    include/chre/platform/context.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.variant.config
)

pw_add_facade(chre.platform.fatal_error INTERFACE
  BACKEND
    chre.platform.fatal_error_BACKEND
  HEADERS
    include/chre/platform/fatal_error.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.platform.log
    chre.variant.config
)

pw_add_facade(chre.platform.host_link INTERFACE
  BACKEND
    chre.platform.host_link_BACKEND
  HEADERS
    include/chre/platform/host_link.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.util
    chre.variant.config
)

pw_add_facade(chre.platform.log INTERFACE
  BACKEND
    chre.platform.log_BACKEND
  HEADERS
    include/chre/platform/log.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.util
    chre.variant.config
)

pw_add_facade(chre.platform.memory INTERFACE
  BACKEND
    chre.platform.memory_BACKEND
  HEADERS
    include/chre/platform/memory.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.variant.config
)

pw_add_facade(chre.platform.memory_manager INTERFACE
  BACKEND
    chre.platform.memory_manager_BACKEND
  HEADERS
    include/chre/platform/heap_block_header.h
    include/chre/platform/memory_manager.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.util
    chre.core
    chre.variant.config
)

pw_add_facade(chre.platform.mutex INTERFACE
  BACKEND
    chre.platform.mutex_BACKEND
  HEADERS
    include/chre/platform/mutex.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.util
    chre.variant.config
)

pw_add_facade(chre.platform.notifier INTERFACE
  BACKEND
    chre.platform.notifier_BACKEND
  HEADERS
    include/chre/platform/notifier.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.util
    chre.variant.config
)

pw_add_facade(chre.platform.platform_audio INTERFACE
  BACKEND
    chre.platform.platform_audio_BACKEND
  HEADERS
    include/chre/platform/platform_audio.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.util
    chre.chre_api
    chre.variant.config
)

pw_add_facade(chre.platform.platform_ble INTERFACE
  BACKEND
    chre.platform.platform_ble_BACKEND
  HEADERS
    include/chre/platform/platform_ble.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.util
    chre.variant.config
)

pw_add_facade(chre.platform.platform_debug_dump_manager INTERFACE
  BACKEND
    chre.platform.platform_debug_dump_manager_BACKEND
  HEADERS
    include/chre/platform/platform_debug_dump_manager.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.util
    chre.chre_api
    chre.variant.config
)

pw_add_facade(chre.platform.platform_gnss INTERFACE
  BACKEND
    chre.platform.platform_gnss_BACKEND
  HEADERS
    include/chre/platform/platform_gnss.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.util
    chre.variant.config
)

pw_add_facade(chre.platform.platform_nanoapp INTERFACE
  BACKEND
    chre.platform.platform_nanoapp_BACKEND
  HEADERS
    include/chre/platform/platform_nanoapp.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.util
    chre.variant.config
)

pw_add_facade(chre.platform.platform_sensor INTERFACE
  BACKEND
    chre.platform.platform_sensor_BACKEND
  HEADERS
    include/chre/platform/platform_sensor.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.core
    chre.platform.fatal_error
    chre.util
    chre.variant.config
)

pw_add_facade(chre.platform.platform_sensor_manager INTERFACE
  BACKEND
    chre.platform.platform_sensor_manager_BACKEND
  HEADERS
    include/chre/platform/platform_sensor_manager.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.core
    chre.util
    chre.variant.config
)

pw_add_facade(chre.platform.platform_sensor_type_helpers INTERFACE
  BACKEND
    chre.platform.platform_sensor_type_helpers_BACKEND
  HEADERS
    include/chre/platform/platform_sensor_type_helpers.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.core
    chre.variant.config
)

pw_add_facade(chre.platform.platform_wifi INTERFACE
  BACKEND
    chre.platform.platform_wifi_BACKEND
  HEADERS
    include/chre/platform/platform_wifi.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.variant.config
)

pw_add_facade(chre.platform.platform_wwan INTERFACE
  BACKEND
    chre.platform.platform_wwan_BACKEND
  HEADERS
    include/chre/platform/platform_wwan.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.variant.config
)

pw_add_facade(chre.platform.power_control_manager INTERFACE
  BACKEND
    chre.platform.power_control_manager_BACKEND
  HEADERS
    include/chre/platform/power_control_manager.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.util
    chre.variant.config
)

pw_add_facade(chre.platform.static_nanoapp_init INTERFACE
  BACKEND
    chre.platform.static_nanoapp_init_BACKEND
  HEADERS
    include/chre/platform/static_nanoapp_init.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.variant.config
)

pw_add_facade(chre.platform.system_time INTERFACE
  BACKEND
    chre.platform.system_time_BACKEND
  HEADERS
    include/chre/platform/system_time.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.util
    chre.variant.config
)

pw_add_facade(chre.platform.system_timer INTERFACE
  BACKEND
    chre.platform.system_timer_BACKEND
  HEADERS
    include/chre/platform/system_timer.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.util
    chre.variant.config
)

pw_add_facade(chre.platform.thread_handle INTERFACE
  BACKEND
    chre.platform.thread_handle_BACKEND
  HEADERS
    include/chre/platform/thread_handle.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.variant.config
)

pw_add_facade(chre.platform.tracing INTERFACE
  BACKEND
    chre.platform.tracing_BACKEND
  HEADERS
    include/chre/platform/tracing.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.variant.config
)

pw_add_facade(chre.platform.version INTERFACE
  BACKEND
    chre.platform.version_BACKEND
  HEADERS
    include/chre/platform/version.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.variant.config
)
