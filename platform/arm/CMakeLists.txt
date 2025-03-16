include($ENV{PW_ROOT}/pw_build/pigweed.cmake)

pw_add_library(chre.platform.arm.nanoapp_loader STATIC
  SOURCES
    nanoapp_loader.cc
  PRIVATE_DEPS
    chre.platform.shared.nanoapp_loader.facade
)
