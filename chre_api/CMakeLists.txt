include($ENV{PW_ROOT}/pw_build/pigweed.cmake)
include(backend.cmake)

# The interface which provides the base CHRE API for nanoapps including
# "chre.h", "chre/version.h", etc.
#
# Note that this does not implement the interface, this is done either by the
# runtime or the nanoapp support lib DSO for dynamic nanoapp builds -- neither
# of which nanoapps are permitted to directly depend on.
pw_add_facade(chre.chre_api INTERFACE
  BACKEND
    chre.chre_api_BACKEND
  HEADERS
    include/chre_api/chre.h
    include/chre_api/chre/audio.h
    include/chre_api/chre/ble.h
    include/chre_api/chre/common.h
    include/chre_api/chre/event.h
    include/chre_api/chre/gnss.h
    include/chre_api/chre/msg.h
    include/chre_api/chre/nanoapp.h
    include/chre_api/chre/re.h
    include/chre_api/chre/sensor.h
    include/chre_api/chre/sensor_types.h
    include/chre_api/chre/toolchain.h
    include/chre_api/chre/user_settings.h
    include/chre_api/chre/version.h
    include/chre_api/chre/wifi.h
    include/chre_api/chre/wwan.h
  PUBLIC_INCLUDES
    include
    include/chre_api
  PUBLIC_DEPS
    chre.variant.config
)
