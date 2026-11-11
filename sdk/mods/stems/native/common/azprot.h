/* SPDX-License-Identifier: GPL-3.0-only */
/* azprot.h: open a page range for writing and put back exactly the protection it had.
 *
 * Every write into EP147's own image (an inline hook, a vtable slot, a patched
 * instruction, replacement art in .rodata) goes through azprot_unlock / azprot_relock.
 * The protection to restore is read from /proc/self/maps at unlock time, never assumed:
 * .rodata may share the r-x segment with the code, a vtable lives in r-- RELRO, and an
 * island or trampoline we mapped ourselves is r-x. Hard-coding one of those for all of
 * them either drops PROT_EXEC from code or leaves a data page executable.
 *
 * Rules:
 *  - unlock refuses (nothing changed, returns -1) when any byte of the range is unmapped
 *    or the range spans mappings with different protections; the caller must then skip
 *    its write;
 *  - unlock and relock are a pair and hold a process-wide lock between them, so two
 *    threads patching the same page cannot read each other's temporary "rw" as the
 *    original and leave it behind;
 *  - relock reports failure (-1) instead of ignoring it: the write happened, but the
 *    page is still writable, and the caller logs that.
 *
 * Header-only: azmod and azstem each include it. Not async-signal-safe. */
#ifndef AZPROT_H
#define AZPROT_H
#include <fcntl.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef AZPROT_MAPS
#define AZPROT_MAPS "/proc/self/maps"
#endif

/* One maps line: "lo-hi rwxp ..." -> 1 with the range and PROT_* bits, 0 if malformed. */
static int azprot_parse_line(const char *s, uintptr_t *lo, uintptr_t *hi, int *prot) {
    uintptr_t v[2] = {0, 0};
    for (int k = 0; k < 2; k++) {
        int n = 0;
        for (;; s++, n++) {
            int c = *s, d;
            if (c >= '0' && c <= '9')
                d = c - '0';
            else if (c >= 'a' && c <= 'f')
                d = c - 'a' + 10;
            else
                break;
            if (n >= 16)
                return 0;
            v[k] = (v[k] << 4) | (uintptr_t) d;
        }
        if (!n || *s != (k ? ' ' : '-'))
            return 0;
        s++;
    }
    if (v[1] <= v[0])
        return 0;
    if ((s[0] != 'r' && s[0] != '-') || (s[1] != 'w' && s[1] != '-') || (s[2] != 'x' && s[2] != '-'))
        return 0;
    *lo = v[0];
    *hi = v[1];
    *prot = (s[0] == 'r' ? PROT_READ : 0) | (s[1] == 'w' ? PROT_WRITE : 0) |
            (s[2] == 'x' ? PROT_EXEC : 0);
    return 1;
}

/* The protection every byte of [a, b) shares, read from a maps stream on fd (lines in
 * ascending address order, as the kernel writes them). Contiguous mappings with the same
 * protection count as one. -1 for a gap, a protection change inside the range, or a
 * malformed or empty file. */
static int azprot_query_fd(int fd, uintptr_t a, uintptr_t b) {
    if (fd < 0 || b <= a)
        return -1;
    char buf[2048], line[256];
    int ln = 0, prot = -1;
    uintptr_t cur = a;
    ssize_t r;
    while ((r = read(fd, buf, sizeof buf)) > 0)
        for (ssize_t k = 0; k < r; k++) {
            char c = buf[k];
            if (c != '\n') {
                if (ln < (int) sizeof line - 1)
                    line[ln++] = c;
                continue;
            }
            line[ln] = 0;
            ln = 0;
            uintptr_t lo, hi;
            int p;
            if (!azprot_parse_line(line, &lo, &hi, &p) || hi <= cur)
                continue;
            if (lo > cur)
                return -1; /* a hole: part of the range is not mapped */
            if (prot >= 0 && p != prot)
                return -1; /* the range spans two protections */
            prot = p;
            cur = hi;
            if (cur >= b)
                return prot;
        }
    return -1;
}

static int azprot_query(uintptr_t a, uintptr_t b) {
    int fd = open(AZPROT_MAPS, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    int p = azprot_query_fd(fd, a, b);
    close(fd);
    return p;
}

typedef struct {
    uintptr_t base;
    size_t len;
    int prot; /* what the pages had before azprot_unlock */
} azprot_t;

static pthread_mutex_t g_azprot_mu = PTHREAD_MUTEX_INITIALIZER;

/* Make [addr, addr + len) writable: whole pages, the original bits plus PROT_WRITE.
 * 0 = unlocked (call azprot_relock), -1 = refused, nothing changed, nothing held. */
static int azprot_unlock(uintptr_t addr, size_t len, azprot_t *t) {
    long pg = sysconf(_SC_PAGESIZE);
    if (!t || !addr || !len || pg <= 0 || addr + len < addr)
        return -1;
    uintptr_t base = addr & ~(uintptr_t) (pg - 1);
    uintptr_t end = (addr + len + (uintptr_t) pg - 1) & ~(uintptr_t) (pg - 1);
    pthread_mutex_lock(&g_azprot_mu);
    int prot = azprot_query(base, end);
    if (prot < 0 || mprotect((void *) base, end - base, prot | PROT_READ | PROT_WRITE) != 0) {
        pthread_mutex_unlock(&g_azprot_mu);
        return -1;
    }
    t->base = base;
    t->len = end - base;
    t->prot = prot;
    return 0;
}

/* Restore what azprot_unlock found. 0 = restored, -1 = mprotect failed (still writable). */
static int azprot_relock(const azprot_t *t) {
    int rc = mprotect((void *) t->base, t->len, t->prot);
    pthread_mutex_unlock(&g_azprot_mu);
    return rc == 0 ? 0 : -1;
}
#endif
