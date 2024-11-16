include($ENV{PW_ROOT}/pw_build/pigweed.cmake)

pw_add_library(chre.platform.freertos.platform_nanoapp STATIC
  HEADERS
    public_platform_nanoapp/chre/target_platform/platform_nanoapp_base.h
  PUBLIC_INCLUDES
    public_platform_nanoapp
  PUBLIC_DEPS
    chre.platform.shared.memory
    chre.platform.shared.nanoapp_support_lib_dso
  SOURCES
    platform_nanoapp.cc
  PRIVATE_DEPS
    chre.chre_api
    chre.platform.assert
    chre.platform.host_link
    chre.platform.log
    chre.platform.platform_nanoapp.facade
    chre.platform.shared.authentication
    chre.platform.shared.dlfcn
    chre.platform.shared.nanoapp_dso_util
    chre.platform.shared.nanoapp_loader
    chre.util
    chre.util.system
)
