include($ENV{PW_ROOT}/pw_build/pigweed.cmake)

add_subdirectory(chre_api)
add_subdirectory(core)
add_subdirectory(platform)
add_subdirectory(util)
add_subdirectory(variant)

include($ENV{PW_ROOT}/pw_build/pigweed.cmake)

if(NOT chre_third_party_flatbuffers_LIBRARY)
  pw_add_error_target(chre_third_party.flatbuffers
    MESSAGE
      "Attempted to build chre_third_party.flatbuffers without configuring it "
      "via chre_third_party_flatbuffers_LIBRARY."
  )
else()
  pw_add_library(chre_third_party.flatbuffers INTERFACE
    PUBLIC_DEPS
      "${chre_third_party_flatbuffers_LIBRARY}"
  )
endif()
