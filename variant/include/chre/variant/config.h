/*
 * Copyright (C) 2024 The Android Open Source Project
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

#pragma once

// TODO: b/376532038 - Refactor the platform layer to provide static nanoapps
// instead of having it conditionally come out of core/static_nanoapps.cc to
// ensure the build graph can properly represent chre.core.
#if defined(CHRE_INCLUDE_DEFAULT_STATIC_NANOAPPS)
// This cannot be supported due to how the build rules are set up. Ideally this
// would be part of a shared platform layer, but it's in fact part of core.
#error "CMake does not permit the built in default static nanoapps"
#endif  // defined(CHRE_INCLUDE_DEFAULT_STATIC_NANOAPPS)

// This should provide all CHRE_* configuration defines.
#include "chre/target_variant/config.h"
