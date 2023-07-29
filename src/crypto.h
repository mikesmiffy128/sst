/* This file is dedicated to the public domain. */

#ifndef INC_CRYPTO_H
#define INC_CRYPTO_H

#include "3p/monocypher/monocypher.h"
#include "3p/monocypher/monocypher-rng.h"

// -- SST-specific extensions to 4.0.1 API below --
void crypto_aead_lock_djb(uint8_t       *cipher_text,
                          uint8_t        mac  [16],
                          const uint8_t  key  [32],
                          const uint8_t  nonce[8],
                          const uint8_t *ad,         size_t ad_size,
                          const uint8_t *plain_text, size_t text_size);
int crypto_aead_unlock_djb(uint8_t       *plain_text,
                           const uint8_t  mac  [16],
                           const uint8_t  key  [32],
                           const uint8_t  nonce[8],
                           const uint8_t *ad,          size_t ad_size,
                           const uint8_t *cipher_text, size_t text_size);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
