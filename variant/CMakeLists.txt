include($ENV{PW_ROOT}/pw_build/pigweed.cmake)

pw_add_module_config(chre_variant_CONFIG)
pw_add_library(chre.variant.config INTERFACE
  HEADERS
    include/chre/variant/config.h
  PUBLIC_INCLUDES
    include
  PUBLIC_DEPS
    "${chre_variant_CONFIG}"
  PUBLIC_COMPILE_OPTIONS
    --include=${CMAKE_CURRENT_SOURCE_DIR}/include/chre/variant/config.h
)
