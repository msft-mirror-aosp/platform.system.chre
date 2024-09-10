/*
 * Copyright (C) 2023 The Android Open Source Project
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
#ifndef LOCATION_LBS_CONTEXTHUB_NANOAPPS_NEARBY_PRESENCE_CRYPTO_MIC_H_
#define LOCATION_LBS_CONTEXTHUB_NANOAPPS_NEARBY_PRESENCE_CRYPTO_MIC_H_
#include "location/lbs/contexthub/nanoapps/nearby/crypto.h"

namespace nearby {
static constexpr size_t kSaltSize = 2;
// Implements Crypto interface using MIC authentication type for Presence v1
// specification. Crypto algorithms: AES/CTR, HMAC, HKDF, SHA256.
class PresenceCryptoMicImpl : public Crypto {
 public:
  // Decrypts input with salt and key. Places the decrypted result in output.
  bool decrypt(const ByteArray &input, const ByteArray &salt,
               const ByteArray &key, ByteArray &output) const override;
  // Verifies the computed HMAC tag for metadata encryptionKey equal to the one
  // read from credential. Note currently we are not verifying the 16-bytes MIC
  // at the end of the advertisement.
  bool verify(const ByteArray &input, const ByteArray &key,
              const ByteArray &signature) const override;

 private:
  static constexpr char kAesKeyInfo[] = "Unsigned Section AES key";
  static constexpr size_t kAesKeySize = 16;
  static constexpr size_t kEncryptionInfoSize = 17;
  static constexpr size_t kAdvNonceSizeSaltDe = 16;
  static constexpr char kHkdfSalt[] = "Google Nearby";
  static constexpr char kAdvNonceInfoSaltDe[] = "Unsigned Section IV";
  static constexpr size_t kHmacKeySize = 32;
  // This name will be out of date as soon as the section header spec changes
  // are merged.
  static constexpr char kMetadataKeyHmacKeyInfo[] =
      "Unsigned Section metadata key HMAC key";
};
}  // namespace nearby
#endif  // LOCATION_LBS_CONTEXTHUB_NANOAPPS_NEARBY_PRESENCE_CRYPTO_MIC_H_
