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

#include "location/lbs/contexthub/nanoapps/nearby/presence_crypto_mic.h"

#include <cstdint>
#include <cstring>

#include "location/lbs/contexthub/nanoapps/nearby/byte_array.h"
#include "location/lbs/contexthub/nanoapps/nearby/crypto/aes.h"
#include "location/lbs/contexthub/nanoapps/nearby/crypto/hkdf.h"
#include "location/lbs/contexthub/nanoapps/nearby/crypto/hmac.h"
#include "location/lbs/contexthub/nanoapps/nearby/crypto/sha2.h"
#include "third_party/contexthub/chre/util/include/chre/util/macros.h"
#include "third_party/contexthub/chre/util/include/chre/util/nanoapp/log.h"

#define STR_LEN_NO_TERM(str) (ARRAY_SIZE(str) - 1)
#define LOG_TAG "[NEARBY][PRESENCE_CRYPTO_V1]"
namespace nearby {
bool PresenceCryptoMicImpl::decrypt(const ByteArray &input,
                                    const ByteArray &salt, const ByteArray &key,
                                    ByteArray &output) const {
  if (salt.length != kSaltSize && salt.length != kEncryptionInfoSize - 1) {
    LOGE("Invalid salt size");
    return false;
  }
  if (input.length != output.length) {
    LOGE("Output length is not equal to input length.");
    return false;
  }

  // Generate a 32 bytes decryption key from authenticity_key
  uint8_t decryption_key[kAesKeySize] = {0};
  hkdf(reinterpret_cast<const uint8_t *>(kHkdfSalt), STR_LEN_NO_TERM(kHkdfSalt),
       key.data, key.length, reinterpret_cast<const uint8_t *>(kAesKeyInfo),
       STR_LEN_NO_TERM(kAesKeyInfo), decryption_key,
       ARRAY_SIZE(decryption_key));

  // Generate nonce
  uint8_t nonce[kAdvNonceSizeSaltDe] = {0};
  hkdf(reinterpret_cast<const uint8_t *>(kHkdfSalt), STR_LEN_NO_TERM(kHkdfSalt),
       salt.data, salt.length,
       reinterpret_cast<const uint8_t *>(kAdvNonceInfoSaltDe),
       STR_LEN_NO_TERM(kAdvNonceInfoSaltDe), nonce, ARRAY_SIZE(nonce));

  // Decrypt the input cipher text using the decryption key
  struct AesCtrContext ctx;
  if (aesCtrInit(&ctx, decryption_key, nonce, AES_128_KEY_TYPE) < 0) {
    LOGE("aesCtrInit() is failed");
    return false;
  }
  aesCtr(&ctx, input.data, output.data, output.length);
  return true;
}

// Compares the calculated HMAC tag for the metadata encryption key (identity
// value) with provided authenticity key, with the given value.
bool PresenceCryptoMicImpl::verify(const ByteArray &metadataKey,
                                   const ByteArray &authenticityKey,
                                   const ByteArray &tag) const {
  UNUSED_VAR(authenticityKey);
  if (metadataKey.data == nullptr || tag.data == nullptr) {
    LOGE("Null pointer was found in input parameter");
    return false;
  }
  if (tag.length != SHA2_HASH_SIZE) {
    LOGE("Invalid signature size");
    return false;
  }
  // Generates a 32 bytes HMAC tag from the data
  uint8_t hmac_key[kHmacKeySize] = {0};
  hkdf(reinterpret_cast<const uint8_t *>(kHkdfSalt), STR_LEN_NO_TERM(kHkdfSalt),
       authenticityKey.data, authenticityKey.length, kMetadataKeyHmacKeyInfo,
       STR_LEN_NO_TERM(kMetadataKeyHmacKeyInfo), hmac_key,
       ARRAY_SIZE(hmac_key));

  uint8_t hmac_tag[SHA2_HASH_SIZE];
  hmacSha256(hmac_key, ARRAY_SIZE(hmac_key), metadataKey.data,
             metadataKey.length, hmac_tag, sizeof(hmac_tag));

  // Verifies the generated HMAC tag matching the signature
  volatile uint8_t diff = 0;
  for (size_t i = 0; i < SHA2_HASH_SIZE; ++i) diff |= hmac_tag[i] ^ tag.data[i];
  return (diff == 0);
}
}  // namespace nearby
