// This file is dual-licensed.  Choose whichever licence you want from
// the two licences listed below.
//
// The first licence is a regular 2-clause BSD licence.  The second licence
// is the CC-0 from Creative Commons. It is intended to release Monocypher
// to the public domain.  The BSD licence serves as a fallback option.
//
// SPDX-License-Identifier: BSD-2-Clause OR CC0-1.0
//
// ------------------------------------------------------------------------
//
// Copyright (c) 2019-2021, Loup Vaillant
// All rights reserved.
//
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the
//    distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// ------------------------------------------------------------------------
//
// Written in 2019-2021 by Loup Vaillant
//
// To the extent possible under law, the author(s) have dedicated all copyright
// and related neighboring rights to this software to the public domain
// worldwide.  This software is distributed without any warranty.
//
// You should have received a copy of the CC0 Public Domain Dedication along
// with this software.  If not, see
// <https://creativecommons.org/publicdomain/zero/1.0/>

#include "monocypher-rng.h"
#include "monocypher.h"

// avoid memcpy dependency (the compiler will likely use memcpy anyway)
static void copy(uint8_t *out, uint8_t *in, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        out[i] = in[i];
    }
}

// mike: HACK: for single translation unit crypto.c, there's already zero[128]
// in monocypher.c, so leave this out!
//static const uint8_t zero[8] = {0};

void crypto_rng_init(crypto_rng_ctx *ctx, uint8_t random_seed[32])
{
    copy(ctx->pool, random_seed, 32);
    ctx->idx = 512;
    crypto_wipe(random_seed, 32);
}

void crypto_rng_read(crypto_rng_ctx *ctx, uint8_t *buf, size_t size)
{
    size_t pool_size = 512 - ctx->idx;
    while (size > pool_size) {
        copy(buf, ctx->pool + ctx->idx, pool_size);
        crypto_chacha20_djb(ctx->pool, 0, 512, ctx->pool, zero, 0);
        size     -= pool_size;
        buf      += pool_size;
        ctx->idx  = 32;
        pool_size = 512 - 32;
    }
    copy(buf, ctx->pool + ctx->idx, size);
    ctx->idx += size;

    // Wipe used bytes ASAP (even if they'll be erased later)
    crypto_wipe(ctx->pool + 32, ctx->idx - 32);
}

void crypto_rng_fork(crypto_rng_ctx *ctx, crypto_rng_ctx *child_ctx)
{
    uint8_t child_seed[32]; // wiped by crypto_rng_init;
    crypto_rng_read(ctx, child_seed, 32);
    crypto_rng_init(child_ctx, child_seed);
}
