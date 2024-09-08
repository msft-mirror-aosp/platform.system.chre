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

#include "chre_host/config_util.h"
#include "chre_host/log.h"

#include <dirent.h>
#include <json/json.h>
#include <filesystem>
#include <fstream>
#include <regex>

namespace android {
namespace chre {

bool findAllNanoappsInFolder(const std::string &path,
                             std::vector<std::string> &outNanoapps) {
  DIR *dir = opendir(path.c_str());
  if (dir == nullptr) {
    LOGE("Failed to open nanoapp folder %s", path.c_str());
    return false;
  }
  std::regex regex("(\\w+)\\.napp_header");
  std::cmatch match;
  for (struct dirent *entry; (entry = readdir(dir)) != nullptr;) {
    if (!std::regex_match(entry->d_name, match, regex)) {
      continue;
    }
    LOGD("Found nanoapp: %s", match[1]);
    outNanoapps.push_back(match[1]);
  }
  closedir(dir);
  return true;
}

bool getPreloadedNanoappsFromConfigFile(const std::string &configFilePath,
                                        std::string &outDirectory,
                                        std::vector<std::string> &outNanoapps) {
  std::ifstream configFileStream(configFilePath);

  Json::CharReaderBuilder builder;
  Json::Value config;
  if (!configFileStream) {
    // TODO(b/350102369) to deprecate preloaded_nanoapps.json
    // During the transition, fall back to the old behavior if the json
    // file exists. But if the json file does not exist, do the new behavior
    // to load all nanoapps in /vendor/etc/chre or where ever the location.
    LOGI("Failed to open config file '%s' load all nanoapps in folder ",
         configFilePath.c_str());
    std::filesystem::path path(configFilePath);
    outDirectory = path.parent_path().string();
    return findAllNanoappsInFolder(outDirectory, outNanoapps);
  } else if (!Json::parseFromStream(builder, configFileStream, &config,
                                    /* errs = */ nullptr)) {
    LOGE("Failed to parse nanoapp config file");
    return false;
  } else if (!config.isMember("nanoapps") || !config.isMember("source_dir")) {
    LOGE("Malformed preloaded nanoapps config");
    return false;
  }

  outDirectory = config["source_dir"].asString();
  for (Json::ArrayIndex i = 0; i < config["nanoapps"].size(); ++i) {
    const std::string &nanoappName = config["nanoapps"][i].asString();
    outNanoapps.push_back(nanoappName);
  }
  return true;
}

}  // namespace chre
}  // namespace android
