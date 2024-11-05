include_guard(GLOBAL)

include($ENV{PW_ROOT}/pw_build/pigweed.cmake)

# Backend for chre.platform.assert.
pw_add_backend_variable(chre.platform.assert_BACKEND)

# Backend for chre.platform.atomic.
pw_add_backend_variable(chre.platform.atomic_BACKEND)

# Backend for chre.platform.condition_variable.
pw_add_backend_variable(chre.platform.condition_variable_BACKEND)

# Backend for chre.platform.context.
pw_add_backend_variable(chre.platform.context_BACKEND)

# Backend for chre.platform.fatal_error.
pw_add_backend_variable(chre.platform.fatal_error_BACKEND)

# Backend for chre.platform.host_link.
pw_add_backend_variable(chre.platform.host_link_BACKEND)

# Backend for chre.platform.log.
pw_add_backend_variable(chre.platform.log_BACKEND)

# Backend for chre.platform.memory.
pw_add_backend_variable(chre.platform.memory_BACKEND)

# Backend for chre.platform.memory_manager.
pw_add_backend_variable(chre.platform.memory_manager_BACKEND)

# Backend for chre.platform.mutex.
pw_add_backend_variable(chre.platform.mutex_BACKEND)

# Backend for chre.platform.notifier.
pw_add_backend_variable(chre.platform.notifier_BACKEND)

# Backend for chre.platform.platform_audio.
pw_add_backend_variable(chre.platform.platform_audio_BACKEND)

# Backend for chre.platform.platform_ble.
pw_add_backend_variable(chre.platform.platform_ble_BACKEND)

# Backend for chre.platform.platform_debug_dump_manager.
pw_add_backend_variable(chre.platform.platform_debug_dump_manager_BACKEND)

# Backend for chre.platform.platform_gnss.
pw_add_backend_variable(chre.platform.platform_gnss_BACKEND)

# Backend for chre.platform.platform_nanoapp.
pw_add_backend_variable(chre.platform.platform_nanoapp_BACKEND)

# Backend for chre.platform.platform_sensor.
pw_add_backend_variable(chre.platform.platform_sensor_BACKEND)

# Backend for chre.platform.platform_sensor_manager.
pw_add_backend_variable(chre.platform.platform_sensor_manager_BACKEND)

# Backend for chre.platform.platform_sensor_type_helpers.
pw_add_backend_variable(chre.platform.platform_sensor_type_helpers_BACKEND)

# Backend for chre.platform.platform_wifi.
pw_add_backend_variable(chre.platform.platform_wifi_BACKEND)

# Backend for chre.platform.platform_wwan.
pw_add_backend_variable(chre.platform.platform_wwan_BACKEND)

# Backend for chre.platform.power_control_manager.
pw_add_backend_variable(chre.platform.power_control_manager_BACKEND)

# Backend for chre.platform.static_nanoapp_init.
pw_add_backend_variable(chre.platform.static_nanoapp_init_BACKEND)

# Backend for chre.platform.system_time.
pw_add_backend_variable(chre.platform.system_time_BACKEND)

# Backend for chre.platform.system_timer.
pw_add_backend_variable(chre.platform.system_timer_BACKEND)

# Backend for chre.platform.thread_handle.
pw_add_backend_variable(chre.platform.thread_handle_BACKEND)

# Backend for chre.platform.tracing.
pw_add_backend_variable(chre.platform.tracing_BACKEND)

# Backend for chre.platform.version.
pw_add_backend_variable(chre.platform.version_BACKEND)
