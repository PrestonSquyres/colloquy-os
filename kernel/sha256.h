#pragma once
#include "types.h"
typedef struct { u32 h[8]; u8 buf[64]; u64 len; u32 blen; } sha256_ctx;
void sha256_init(sha256_ctx *c);
void sha256_update(sha256_ctx *c, const void *data, usize n);
void sha256_final(sha256_ctx *c, u8 out[32]);
void sha256(const void *data, usize n, u8 out[32]);
