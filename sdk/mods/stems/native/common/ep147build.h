/* SPDX-License-Identifier: GPL-3.0-only */
/* ep147build.h: which EP147 is this process running? Exact identity, no heuristics.
 *
 * azmod and azstem carry absolute EP147 addresses, and one .so must run on every
 * firmware a user may have. So the running EP147 is identified by the md5 of
 * /proc/self/exe (the same number `md5sum /home/root/pdj/EP147` prints) and
 * matched against the builds that have address tables. Anything else is
 * EPB_UNKNOWN and the mods stay inert on it. There is no fallback to the 1.30
 * table, ever: a wrong address is a crash in someone's set.
 *
 * Known builds (md5 of the EP147 inside the firmware's initramfs):
 *   1.30  stock XDJAZv130.UPD, and modified rebuilds that leave EP147 untouched
 *   2.00  stock XDJAZv200.UPD
 *
 * Header-only: azmod and azstem each include it and each hash once at load
 * (~60 MB, before EP147's main; a fraction of a second on the deck). */
#ifndef EP147BUILD_H
#define EP147BUILD_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

enum {
    EPB_UNKNOWN = 0,
    EPB_V130 = 1,
    EPB_V200 = 2,
    EPB_OPUS132 = 3 /* oqstem: OPUS-QUAD 1.32 EP136 */
};

static const struct {
    const char *md5;
    long size;
    int id;
    const char *name;
} EPB_KNOWN[] = {
    {"3518275e8b4d5d63636d8d7b4468d7ba", 57874328L, EPB_V130, "1.30"},
    {"8daa1de55d7328f458f6d855cdc3cdde", 62461736L, EPB_V200, "2.00"},
    {"a149803bb8008f51583a6a31923175e6", 51194448L, EPB_OPUS132, "OPUS 1.32 EP136"},
};

#define EPB_ROTL(x, c) (((x) << (c)) | ((x) >> (32 - (c))))
/* clang-format off */
static void epb_md5_block(uint32_t h[4], const uint8_t *p){
    static const uint32_t K[64] = {
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391 };
    static const uint8_t R[64] = { 7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
                                   5, 9,14,20,5, 9,14,20,5, 9,14,20,5, 9,14,20,
                                   4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
                                   6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21 };
    uint32_t w[16], a = h[0], b = h[1], c = h[2], d = h[3];
    for(int i = 0; i < 16; i++)
        w[i] = p[4*i] | p[4*i+1] << 8 | p[4*i+2] << 16 | (uint32_t)p[4*i+3] << 24;
    for(int i = 0; i < 64; i++){
        uint32_t f; int g;
        if(i < 16){      f = (b & c) | (~b & d); g = i; }
        else if(i < 32){ f = (d & b) | (~d & c); g = (5*i + 1) & 15; }
        else if(i < 48){ f = b ^ c ^ d;          g = (3*i + 5) & 15; }
        else {           f = c ^ (b | ~d);       g = (7*i) & 15; }
        uint32_t t = d; d = c; c = b; b += EPB_ROTL(a + f + K[i] + w[g], R[i]); a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
}
/* clang-format on */

/* md5 of len bytes as 32 lowercase hex chars + NUL */
static void epb_md5_hex(const uint8_t *m, size_t len, char out[33]) {
    uint32_t h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    size_t i = 0;
    for (; i + 64 <= len; i += 64) epb_md5_block(h, m + i);
    uint8_t t[128] = {0};
    size_t r = len - i;
    memcpy(t, m + i, r);
    t[r] = 0x80;
    size_t n = r < 56 ? 64 : 128;
    uint64_t bits = (uint64_t) len * 8;
    for (int k = 0; k < 8; k++) t[n - 8 + k] = (uint8_t) (bits >> (8 * k));
    epb_md5_block(h, t);
    if (n == 128) epb_md5_block(h, t + 64);
    for (int k = 0; k < 16; k++) snprintf(out + 2 * k, 3, "%02x", (h[k / 4] >> (8 * (k % 4))) & 0xff);
}

/* Identify the EP147 at path (normally "/proc/self/exe"). md5_out may be NULL. */
static int ep147_identify(const char *path, char md5_out[33], long *size_out) {
    if (md5_out) md5_out[0] = 0;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return EPB_UNKNOWN;
    struct stat st;
    int id = EPB_UNKNOWN;
    if (fstat(fd, &st) == 0 && st.st_size > 0) {
        if (size_out) *size_out = (long) st.st_size;
        int sized = 0;      /* only hash a file whose size matches a known build */
        for (size_t k = 0; k < sizeof EPB_KNOWN / sizeof *EPB_KNOWN; k++) sized |= EPB_KNOWN[k].size == st.st_size;
        void *m = sized ? mmap(NULL, (size_t) st.st_size, PROT_READ, MAP_PRIVATE, fd, 0) : MAP_FAILED;
        if (m != MAP_FAILED) {
            char hex[33];
            epb_md5_hex((const uint8_t *) m, (size_t) st.st_size, hex);
            munmap(m, (size_t) st.st_size);
            if (md5_out) memcpy(md5_out, hex, 33);
            for (size_t k = 0; k < sizeof EPB_KNOWN / sizeof *EPB_KNOWN; k++)
                if (EPB_KNOWN[k].size == st.st_size && !strcmp(EPB_KNOWN[k].md5, hex)) id = EPB_KNOWN[k].id;
        }
    }
    close(fd);
    return id;
}

static const char *ep147_build_name(int id) {
    for (size_t k = 0; k < sizeof EPB_KNOWN / sizeof *EPB_KNOWN; k++)
        if (EPB_KNOWN[k].id == id) return EPB_KNOWN[k].name;
    return "UNKNOWN";
}
#endif
