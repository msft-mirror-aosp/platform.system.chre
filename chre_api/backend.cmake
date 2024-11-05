include_guard(GLOBAL)

include($ENV{PW_ROOT}/pw_build/pigweed.cmake)

# Backend for chre.chre_api.
pw_add_backend_variable(chre.chre_api_BACKEND)
