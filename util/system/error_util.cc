/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "chre/util/system/error_util.h"

#include "chre/util/system/chre_error_util.h"

namespace chre {

const char *getChreErrorMessage(chreError error) {
  switch (error) {
    case CHRE_ERROR_NONE:
      return "CHRE_ERROR_NONE";
    case CHRE_ERROR:
      return "CHRE_ERROR";
    case CHRE_ERROR_INVALID_ARGUMENT:
      return "CHRE_ERROR_INVALID_ARGUMENT";
    case CHRE_ERROR_BUSY:
      return "CHRE_ERROR_BUSY";
    case CHRE_ERROR_NO_MEMORY:
      return "CHRE_ERROR_NO_MEMORY";
    case CHRE_ERROR_NOT_SUPPORTED:
      return "CHRE_ERROR_NOT_SUPPORTED";
    case CHRE_ERROR_TIMEOUT:
      return "CHRE_ERROR_TIMEOUT";
    case CHRE_ERROR_FUNCTION_DISABLED:
      return "CHRE_ERROR_FUNCTION_DISABLED";
    case CHRE_ERROR_REJECTED_RATE_LIMIT:
      return "CHRE_ERROR_REJECTED_RATE_LIMIT";
    case CHRE_ERROR_FUNCTION_RESTRICTED_TO_OTHER_CLIENT:
      return "CHRE_ERROR_FUNCTION_RESTRICTED";
    case CHRE_ERROR_OBSOLETE_REQUEST:
      return "CHRE_ERROR_OBSOLETE_REQUEST";
    case CHRE_ERROR_TRANSIENT:
      return "CHRE_ERROR_TRANSIENT";
    case CHRE_ERROR_PERMISSION_DENIED:
      return "CHRE_ERROR_PERMISSION_DENIED";
    case CHRE_ERROR_DESTINATION_NOT_FOUND:
      return "CHRE_ERROR_DESTINATION_NOT_FOUND";
      // If this assertion is hit, it is because a new CHRE error code was added
      // and is not yet supported in this function. Add the new error code to
      // this function and update this assertion to use the latest error code.
      static_assert(CHRE_ERROR_SIZE == CHRE_ERROR_DESTINATION_NOT_FOUND + 1);
    default:
      return "CHRE_ERROR_UNKNOWN";
  }
}

}  // namespace chre
