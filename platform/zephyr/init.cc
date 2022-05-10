/*
 * Copyright (C) 2022 The Android Open Source Project
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
#include "chre/core/init.h"

#include <errno.h>
#include <sys/printk.h>
#include <zephyr.h>

#include "chre/core/event_loop_manager.h"
#include "chre/core/static_nanoapps.h"
#include "chre/target_platform/init.h"

namespace chre {
namespace zephyr {
namespace {

K_THREAD_STACK_DEFINE(chre_stack_area, CONFIG_CHRE_TASK_STACK_SIZE);
struct k_thread chre_thread_data;
k_tid_t chre_tid;

void chreThreadEntry(void *, void *, void *) {
  chre::init();
  chre::EventLoopManagerSingleton::get()->lateInit();
  chre::loadStaticNanoapps();

  chre::EventLoopManagerSingleton::get()->getEventLoop().run();

  // we only get here if the CHRE EventLoop exited
  chre::deinit();

  chre_tid = nullptr;
}
}  // namespace

int init() {
  static const char *thread_name = CONFIG_CHRE_TASK_NAME;
  chre_tid = k_thread_create(&chre_thread_data, chre_stack_area,
                             K_THREAD_STACK_SIZEOF(chre_stack_area),
                             chreThreadEntry, nullptr, nullptr, nullptr,
                             CONFIG_CHRE_TASK_PRIORITY, 0, K_NO_WAIT);

  if (chre_tid == nullptr) {
    printk("Failed to create thread\n");
    return -EINVAL;
  }

  if (int rc = k_thread_name_set(chre_tid, thread_name); rc != 0) {
    printk("Failed to set thread name to \"%s\": %d\n", CONFIG_CHRE_TASK_NAME,
           rc);
  }

//  chpp::init();
  return 0;
}

void deinit() {
  if (chre_tid != nullptr) {
    chre::EventLoopManagerSingleton ::get()->getEventLoop().stop();
  }
//  chpp::deinit();
}

k_tid_t getChreTaskId() {
  return chre_tid;
}

}  // namespace chre::zephyr

}  // namespace chre
