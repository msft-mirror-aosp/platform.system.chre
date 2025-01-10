/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "permissions_util.h"

#include <chre_host/log.h>

#include "chre/util/macros.h"
#include "chre/util/system/napp_permissions.h"

namespace android::hardware::contexthub::common::implementation {

std::vector<std::string> chreToAndroidPermissions(uint32_t chrePermissions) {
  std::vector<std::string> androidPermissions;
  if (BITMASK_HAS_VALUE(chrePermissions,
                        ::chre::NanoappPermissions::CHRE_PERMS_AUDIO)) {
    androidPermissions.push_back(kRecordAudioPerm);
  }

  if (BITMASK_HAS_VALUE(chrePermissions,
                        ::chre::NanoappPermissions::CHRE_PERMS_GNSS) ||
      BITMASK_HAS_VALUE(chrePermissions,
                        ::chre::NanoappPermissions::CHRE_PERMS_WIFI) ||
      BITMASK_HAS_VALUE(chrePermissions,
                        ::chre::NanoappPermissions::CHRE_PERMS_WWAN)) {
    androidPermissions.push_back(kFineLocationPerm);
    androidPermissions.push_back(kBackgroundLocationPerm);
  }

  if (BITMASK_HAS_VALUE(chrePermissions,
                        ::chre::NanoappPermissions::CHRE_PERMS_BLE)) {
    androidPermissions.push_back(kBluetoothScanPerm);
  }

  return androidPermissions;
}

uint32_t androidToChrePermissions(
    const std::vector<std::string> &androidPermissions) {
  uint32_t chrePermissions = 0;
  for (const auto &permission : androidPermissions) {
    if (permission == kRecordAudioPerm) {
      chrePermissions |= ::chre::NanoappPermissions::CHRE_PERMS_AUDIO;
    } else if (permission == kFineLocationPerm ||
               permission == kBackgroundLocationPerm) {
      chrePermissions |= ::chre::NanoappPermissions::CHRE_PERMS_GNSS |
                         ::chre::NanoappPermissions::CHRE_PERMS_WIFI |
                         ::chre::NanoappPermissions::CHRE_PERMS_WWAN;
    } else if (permission == kBluetoothScanPerm) {
      chrePermissions |= ::chre::NanoappPermissions::CHRE_PERMS_BLE;
    } else {
      LOGW("Unknown Android permission %s", permission.c_str());
    }
  }
  return chrePermissions;
}

}  // namespace android::hardware::contexthub::common::implementation
