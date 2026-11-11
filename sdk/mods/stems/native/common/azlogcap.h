/* SPDX-License-Identifier: GPL-3.0-only */
/* azlogcap.h: size caps for every file azmod and azstem append to under /tmp.
 *
 * /tmp on the deck is RAM. A log that only ever grows (a crash record per restart, one
 * line per start, a per-change diagnostic left on for a week) eventually takes memory
 * from EP147. Every append-mode writer therefore goes through one of these:
 *
 *   azlog_rotate(path, max)       path -> path.1 (replacing the old .1) once path is
 *                                 larger than max, so a log holds at most ~2 x max and
 *                                 the newest lines always survive;
 *   azlog_fopen_capped(path, max) fopen(path, "ae") after azlog_rotate.
 *
 * azlog_rotate uses only stat() and rename() on a stack buffer, both async-signal-safe,
 * so a fault handler may call it before opening its record file.
 *
 * Header-only: azmod and azstem each include it. */
#ifndef AZLOGCAP_H
#define AZLOGCAP_H
#include <stdio.h>
#include <sys/stat.h>

#define AZLOG_CAP_BYTES (256L * 1024)

static int azlog_rotate(const char *path, long max) {
    struct stat st;
    if (!path || stat(path, &st) != 0 || st.st_size <= max)
        return 0;
    char old[320];
    size_t n = 0;
    while (path[n] && n < sizeof old - 3) {
        old[n] = path[n];
        n++;
    }
    if (path[n])
        return 0; /* too long to name the .1: leave it alone rather than truncate it */
    old[n] = '.';
    old[n + 1] = '1';
    old[n + 2] = 0;
    return rename(path, old) == 0;
}

static inline FILE *azlog_fopen_capped(const char *path, long max) {
    azlog_rotate(path, max);
    return fopen(path, "ae"); /* cloexec-ok: "ae"; logcap-ok: rotated above */
}
#endif
