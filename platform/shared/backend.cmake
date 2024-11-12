include_guard(GLOBAL)

include($ENV{PW_ROOT}/pw_build/pigweed.cmake)

# Backend for chre.platform.shared.bt_shoop_log.
pw_add_backend_variable(chre.platform.shared.bt_snoop_log_BACKEND)

# Backend for chre.platform.shared.platform_pal.
pw_add_backend_variable(chre.platform.shared.platform_pal_BACKEND)

# Backend for chre.platform.shared.pal_system_api.
pw_add_backend_variable(chre.platform.shared.pal_system_api_BACKEND)
