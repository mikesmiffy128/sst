/* This file is dedicated to the public domain. */

#include "3p/monocypher/monocypher.c"
#include "3p/monocypher/monocypher-rng.c"

// -- SST-specific extensions to 4.0.1 API below --
void crypto_aead_lock_djb(u8 *cipher_text, u8 mac[16], const u8 key[32],
                          const u8  nonce[8], const u8 *ad, size_t ad_size,
                          const u8 *plain_text, size_t text_size)
{
	crypto_aead_ctx ctx;
	crypto_aead_init_djb(&ctx, key, nonce);
	crypto_aead_write(&ctx, cipher_text, mac, ad, ad_size,
	                  plain_text, text_size);
	crypto_wipe(&ctx, sizeof(ctx));
}

int crypto_aead_unlock_djb(u8 *plain_text, const u8  mac[16], const u8 key[32],
                           const u8 nonce[8], const u8 *ad, size_t ad_size,
                           const u8 *cipher_text, size_t text_size)
{
	crypto_aead_ctx ctx;
	crypto_aead_init_djb(&ctx, key, nonce);
	int mismatch = crypto_aead_read(&ctx, plain_text, mac, ad, ad_size,
	                                cipher_text, text_size);
	crypto_wipe(&ctx, sizeof(ctx));
	return mismatch;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
