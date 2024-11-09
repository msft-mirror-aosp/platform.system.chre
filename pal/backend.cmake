include_guard(GLOBAL)

include($ENV{PW_ROOT}/pw_build/pigweed.cmake)

# Backend for chre.pal.audio.
pw_add_backend_variable(chre.pal.audio_BACKEND)

# Backend for chre.pal.ble.
pw_add_backend_variable(chre.pal.ble_BACKEND)

# Backend for chre.pal.gnss.
pw_add_backend_variable(chre.pal.gnss_BACKEND)

# Backend for chre.pal.sensor.
pw_add_backend_variable(chre.pal.sensor_BACKEND)

# Backend for chre.pal.wifi.
pw_add_backend_variable(chre.pal.wifi_BACKEND)

# Backend for chre.pal.wwan.
pw_add_backend_variable(chre.pal.wwan_BACKEND)
