/* SPDX-License-Identifier: GPL-3.0-only */
/* azsha256.h: SHA-256 (FIPS 180-4), small and self-contained, for hashing files that are
 * about to be loaded (community mods). Not constant-time and not meant for secrets: it
 * fingerprints public bytes. Header-only (static functions), one translation unit each. */
#ifndef AZSHA256_H
#define AZSHA256_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    uint32_t h[8];
    uint64_t len;
    unsigned char buf[64];
    size_t n;
} azsha256_ctx;

/* clang-format off */
static const uint32_t AZSHA256_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};
#define AZSHA256_ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void azsha256_block(azsha256_ctx *c, const unsigned char *p){
    uint32_t w[64], a, b, d, e, f, g, h, cc;
    for(int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for(int i = 16; i < 64; i++){
        uint32_t s0 = AZSHA256_ROR(w[i - 15], 7) ^ AZSHA256_ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = AZSHA256_ROR(w[i - 2], 17) ^ AZSHA256_ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3]; e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];
    for(int i = 0; i < 64; i++){
        uint32_t S1 = AZSHA256_ROR(e, 6) ^ AZSHA256_ROR(e, 11) ^ AZSHA256_ROR(e, 25);
        uint32_t t1 = h + S1 + ((e & f) ^ (~e & g)) + AZSHA256_K[i] + w[i];
        uint32_t S0 = AZSHA256_ROR(a, 2) ^ AZSHA256_ROR(a, 13) ^ AZSHA256_ROR(a, 22);
        uint32_t t2 = S0 + ((a & b) ^ (a & cc) ^ (b & cc));
        h = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}
/* clang-format on */
static void azsha256_init(azsha256_ctx *c) {
    static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(c->h, iv, sizeof iv);
    c->len = 0;
    c->n = 0;
}
static void azsha256_update(azsha256_ctx *c, const void *data, size_t len) {
    const unsigned char *p = data;
    c->len += len;
    if (c->n) {
        size_t k = 64 - c->n < len ? 64 - c->n : len;
        memcpy(c->buf + c->n, p, k);
        c->n += k;
        p += k;
        len -= k;
        if (c->n < 64) return;
        azsha256_block(c, c->buf);
        c->n = 0;
    }
    for (; len >= 64; p += 64, len -= 64) azsha256_block(c, p);
    if (len) {
        memcpy(c->buf, p, len);
        c->n = len;
    }
}
/* the digest as 64 lowercase hex characters and a NUL */
static void azsha256_final_hex(azsha256_ctx *c, char out[65]) {
    uint64_t bits = c->len * 8;
    unsigned char pad = 0x80, z = 0, L[8];
    azsha256_update(c, &pad, 1);
    while (c->n != 56) azsha256_update(c, &z, 1);
    for (int i = 0; i < 8; i++) L[i] = (unsigned char) (bits >> (56 - 8 * i));
    azsha256_update(c, L, 8);
    static const char hx[] = "0123456789abcdef";
    for (int i = 0; i < 8; i++)
        for (int k = 0; k < 4; k++) {
            unsigned v = (c->h[i] >> (24 - 8 * k)) & 0xff;
            out[8 * i + 2 * k] = hx[v >> 4];
            out[8 * i + 2 * k + 1] = hx[v & 15];
        }
    out[64] = 0;
}
static void azsha256_hex(const void *data, size_t len, char out[65]) {
    azsha256_ctx c;
    azsha256_init(&c);
    azsha256_update(&c, data, len);
    azsha256_final_hex(&c, out);
}
#endif
