include($ENV{PW_ROOT}/pw_build/pigweed.cmake)
include(backend.cmake)

pw_add_facade(chre.pal.audio INTERFACE
  BACKEND
    chre.pal.audio_BACKEND
  HEADERS
    include/chre/pal/audio.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.pal.system
    chre.pal.version
    chre.chre_api
)

pw_add_facade(chre.pal.ble INTERFACE
  BACKEND
    chre.pal.ble_BACKEND
  HEADERS
    include/chre/pal/ble.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.pal.system
    chre.pal.version
    chre.chre_api
)

pw_add_facade(chre.pal.gnss INTERFACE
  BACKEND
    chre.pal.gnss_BACKEND
  HEADERS
    include/chre/pal/gnss.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.pal.system
    chre.pal.version
    chre.chre_api
)

pw_add_facade(chre.pal.sensor INTERFACE
  BACKEND
    chre.pal.sensor_BACKEND
  HEADERS
    include/chre/pal/sensor.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.pal.system
    chre.pal.version
    chre.chre_api
)

pw_add_library(chre.pal.system INTERFACE
  HEADERS
    include/chre/pal/system.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.chre_api
)

pw_add_library(chre.pal.version INTERFACE
  HEADERS
    include/chre/pal/version.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.chre_api
    chre.pal.version
)

pw_add_facade(chre.pal.wifi INTERFACE
  BACKEND
    chre.pal.wifi_BACKEND
  HEADERS
    include/chre/pal/wifi.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.pal.system
    chre.pal.version
    chre.chre_api
)

pw_add_facade(chre.pal.wwan INTERFACE
  BACKEND
    chre.pal.wwan_BACKEND
  HEADERS
    include/chre/pal/wwan.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    chre.pal.system
    chre.pal.version
    chre.chre_api
)
