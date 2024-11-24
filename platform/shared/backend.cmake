include_guard(GLOBAL)

include($ENV{PW_ROOT}/pw_build/pigweed.cmake)

# Backend for chre.platform.shared.authentication.
pw_add_backend_variable(chre.platform.shared.authentication_BACKEND)

# Backend for chre.platform.shared.bt_snoop_log.
pw_add_backend_variable(chre.platform.shared.bt_snoop_log_BACKEND)

# Backend for chre.platform.shared.log_buffer_manager.
pw_add_backend_variable(chre.platform.shared.log_buffer_manager_BACKEND)

# Backend for chre.platform.shared.memory.
pw_add_backend_variable(chre.platform.shared.memory_BACKEND)

# Backend for chre.platform.shared.nanoapp_loader.
pw_add_backend_variable(chre.platform.shared.nanoapp_loader_BACKEND)

# Backend for chre.platform.shared.platform_cache_management.
pw_add_backend_variable(chre.platform.shared.platform_cache_management_BACKEND)

# Backend for chre.platform.shared.platform_pal.
pw_add_backend_variable(chre.platform.shared.platform_pal_BACKEND)

# Backend for chre.platform.shared.pal_system_api.
pw_add_backend_variable(chre.platform.shared.pal_system_api_BACKEND)
