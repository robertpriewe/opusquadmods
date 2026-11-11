/* Gate 4: mirror one committed playlist move into export.pdb (DeviceSQL).
 *
 * Table 8 (playlist_entries) rows are 12 bytes: entry rank (1-based), track ID,
 * playlist ID. A move of the row at rank from+1 to rank to+1 rewrites only the
 * rank field of the rows whose position changes; record slots stay in place.
 * Pages whose rows changed get the dirty-row bookkeeping the R59/R76 adjacent
 * swap uses (playlist_pdb_track_order.c): clear each group's -2 mask, mark the
 * last written slot, set header +0x20 low bits to 1 and +0x22 low bits to that
 * slot. The page walk and bounds checks follow that file; this is project code,
 * not AZ code.
 *
 * pdbm_stage_move() never writes `input`; it writes a new file (O_EXCL, fsync).
 * The whole playlist must still equal `expect` (track IDs in rank order, ranks
 * 1..n) or nothing is written. pdbm_publish() renames the staged sibling over
 * the live file and fsyncs the directory. Library thread, after PLR_OK. */
#ifndef OQ_PLAYLIST_PDB_MOVE_H
#define OQ_PLAYLIST_PDB_MOVE_H
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum pdbm_result {
    PDBM_OK = 0,
    PDBM_NOOP = 1,    /* from == to */
    PDBM_BAD = -1,
    PDBM_FORMAT = -2, /* not a DeviceSQL file this code understands */
    PDBM_STALE = -3,  /* table 8 does not hold exactly `expect` for the playlist */
    PDBM_IO = -4,
    PDBM_LIMIT = -5
};
enum { PDBM_MAX_BYTES = 134217728, PDBM_MAX_TABLES = 64, PDBM_MAX_ROWS = 8192,
       PDBM_ENTRY_TABLE = 8, PDBM_HEADER = 0x28, PDBM_MAX_SLOTS = 8192 };

struct pdbm_row { uint32_t rank, track; size_t offset, page; unsigned slot; };

static uint16_t pdbm_rd16(const unsigned char *p) { return (uint16_t) (p[0] | p[1] << 8); }
static uint32_t pdbm_rd32(const unsigned char *p) {
    return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}
static void pdbm_wr16(unsigned char *p, uint16_t v) { p[0] = (unsigned char) v; p[1] = (unsigned char) (v >> 8); }
static void pdbm_wr32(unsigned char *p, uint32_t v) {
    for (int i = 0; i < 4; i++)
        p[i] = (unsigned char) (v >> 8 * i);
}

/* Where the row at 0-based position i lands when `from` moves to `to`. */
static unsigned pdbm_new_pos(unsigned i, unsigned from, unsigned to) {
    if (i == from)
        return to;
    if (from < to && i > from && i <= to)
        return i - 1;
    if (from > to && i >= to && i < from)
        return i + 1;
    return i;
}

static int pdbm_read(const char *path, unsigned char **out, size_t *len) {
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return PDBM_IO;
    struct stat st;
    int rc = PDBM_IO;
    if (!fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_size >= 512 && st.st_size <= PDBM_MAX_BYTES) {
        size_t n = (size_t) st.st_size, at = 0;
        unsigned char *b = malloc(n);
        while (b && at < n) {
            ssize_t got = read(fd, b + at, n - at);
            if (got <= 0)
                break;
            at += (size_t) got;
        }
        if (b && at == n) {
            *out = b;
            *len = n;
            rc = PDBM_OK;
        } else {
            free(b);
        }
    }
    close(fd);
    return rc;
}
static int pdbm_write_new(const char *path, const unsigned char *b, size_t n, mode_t mode) {
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return PDBM_IO;
    size_t at = 0;
    int ok = 1;
    while (at < n) {
        ssize_t sent = write(fd, b + at, n - at);
        if (sent <= 0) {
            ok = 0;
            break;
        }
        at += (size_t) sent;
    }
    if (ok && (fchmod(fd, mode & 0777) || fsync(fd))) /* keep the live file's mode */
        ok = 0;
    if (close(fd))
        ok = 0;
    if (!ok) {
        unlink(path);
        return PDBM_IO;
    }
    return PDBM_OK;
}
static int pdbm_mark_page(unsigned char *page, size_t page_len, unsigned last_slot) {
    unsigned slots = page[0x18] | (unsigned) (page[0x19] & 0x1f) << 8;
    if (last_slot >= slots)
        return 0;
    for (unsigned g = 0; g < (slots + 15) / 16; g++) {
        size_t pos = page_len - (size_t) g * 0x24 - 2;
        if (pos < PDBM_HEADER || pos + 2 > page_len)
            return 0;
        pdbm_wr16(page + pos, 0);
    }
    pdbm_wr16(page + page_len - (size_t) (last_slot / 16) * 0x24 - 2, (uint16_t) (1u << last_slot % 16));
    pdbm_wr16(page + 0x20, (uint16_t) ((pdbm_rd16(page + 0x20) & 0xc000u) | 1u));
    pdbm_wr16(page + 0x22, (uint16_t) ((pdbm_rd16(page + 0x22) & 0xc000u) | last_slot));
    return 1;
}

/* Collect the playlist's table-8 rows from `data`, sorted by rank. */
static int pdbm_rows(const unsigned char *data, size_t length, uint32_t playlist_id,
                     struct pdbm_row *rows, size_t *found, size_t *page_len_out) {
    uint32_t page_len = pdbm_rd32(data + 4), tables = pdbm_rd32(data + 8);
    if (page_len < PDBM_HEADER || page_len > 65536 || length % page_len || tables > PDBM_MAX_TABLES ||
        0x1cu + (size_t) tables * 16 > page_len)
        return PDBM_FORMAT;
    uint32_t first = UINT32_MAX, last = UINT32_MAX;
    for (uint32_t i = 0; i < tables; i++) {
        size_t at = 0x1c + (size_t) i * 16;
        if (pdbm_rd32(data + at) == PDBM_ENTRY_TABLE) {
            first = pdbm_rd32(data + at + 8);
            last = pdbm_rd32(data + at + 12);
            break;
        }
    }
    size_t pages = length / page_len;
    if (first >= pages || last >= pages)
        return PDBM_FORMAT;
    unsigned char *visited = calloc(pages, 1);
    if (!visited)
        return PDBM_IO;
    int rc = PDBM_FORMAT;
    size_t n = 0;
    uint32_t page = first;
    for (size_t step = 0; step < pages; step++) {
        if (page >= pages || visited[page])
            goto out;
        visited[page] = 1;
        size_t base = (size_t) page * page_len;
        const unsigned char *p = data + base;
        if (pdbm_rd32(p + 4) != page || pdbm_rd32(p + 8) != PDBM_ENTRY_TABLE)
            goto out;
        if (!(p[0x1b] & 0x40)) {
            unsigned slots = p[0x18] | (unsigned) (p[0x19] & 0x1f) << 8;
            size_t used = pdbm_rd16(p + 0x1e);
            if (!slots || slots > PDBM_MAX_SLOTS || used > page_len - PDBM_HEADER)
                goto out;
            for (unsigned slot = 0; slot < slots; slot++) {
                size_t group = base + page_len - (size_t) (slot / 16) * 0x24;
                size_t mask = group - 4, off = group - 6 - 2 * (slot % 16);
                if (mask < base + PDBM_HEADER || off < base + PDBM_HEADER || off + 2 > base + page_len)
                    goto out;
                if (!(pdbm_rd16(data + mask) & (uint16_t) (1u << slot % 16)))
                    continue;
                size_t rel = pdbm_rd16(data + off), row = base + PDBM_HEADER + rel;
                if (rel > used || 12 > used - rel)
                    goto out;
                if (pdbm_rd32(data + row + 8) != playlist_id)
                    continue;
                if (n >= PDBM_MAX_ROWS) {
                    rc = PDBM_LIMIT;
                    goto out;
                }
                rows[n++] = (struct pdbm_row){pdbm_rd32(data + row), pdbm_rd32(data + row + 4), row, base, slot};
            }
        }
        if (page == last) {
            for (size_t i = 1; i < n; i++) /* by rank */
                for (size_t j = i; j && rows[j - 1].rank > rows[j].rank; j--) {
                    struct pdbm_row t = rows[j];
                    rows[j] = rows[j - 1];
                    rows[j - 1] = t;
                }
            *found = n;
            *page_len_out = page_len;
            rc = PDBM_OK;
            goto out;
        }
        page = pdbm_rd32(p + 0x0c);
    }
out:
    free(visited);
    return rc;
}

static int pdbm_stage_move(const char *input, const char *output, uint32_t playlist_id,
                           const uint32_t *expect, unsigned n, unsigned from, unsigned to) {
    if (!input || !output || !expect || !playlist_id || !n || n > PDBM_MAX_ROWS || from >= n || to >= n ||
        !strcmp(input, output))
        return PDBM_BAD;
    if (from == to)
        return PDBM_NOOP;
    unsigned char *data = 0;
    size_t length = 0, found = 0, page_len = 0;
    int rc = pdbm_read(input, &data, &length);
    if (rc)
        return rc;
    struct pdbm_row *rows = malloc(PDBM_MAX_ROWS * sizeof *rows);
    if (!rows) {
        free(data);
        return PDBM_IO;
    }
    rc = pdbm_rows(data, length, playlist_id, rows, &found, &page_len);
    if (!rc && found != n)
        rc = PDBM_STALE;
    for (unsigned i = 0; !rc && i < n; i++)
        if (rows[i].rank != i + 1 || rows[i].track != expect[i])
            rc = PDBM_STALE;
    if (!rc) {
        /* write ranks in row order; the last write per page is its dirty slot */
        size_t last_page = SIZE_MAX;
        unsigned last_slot = 0;
        for (unsigned i = 0; i < n && !rc; i++) {
            unsigned np = pdbm_new_pos(i, from, to);
            if (np == i)
                continue;
            if (last_page != SIZE_MAX && rows[i].page != last_page &&
                !pdbm_mark_page(data + last_page, page_len, last_slot))
                rc = PDBM_FORMAT;
            pdbm_wr32(data + rows[i].offset, np + 1);
            last_page = rows[i].page;
            last_slot = rows[i].slot;
        }
        if (!rc && last_page != SIZE_MAX && !pdbm_mark_page(data + last_page, page_len, last_slot))
            rc = PDBM_FORMAT;
    }
    struct stat st;
    if (!rc && stat(input, &st))
        rc = PDBM_IO;
    if (!rc)
        rc = pdbm_write_new(output, data, length, st.st_mode);
    free(rows);
    free(data);
    return rc;
}

/* Rename the staged sibling over the live file, then make the rename durable. */
static int pdbm_publish(const char *staged, const char *live) {
    if (!staged || !live || rename(staged, live))
        return PDBM_IO;
    char dir[512];
    const char *slash = strrchr(live, '/');
    if (!slash || (size_t) (slash - live) >= sizeof dir)
        return PDBM_OK; /* renamed; nothing to fsync relative to */
    memcpy(dir, live, (size_t) (slash - live));
    dir[slash - live] = 0;
    int fd = open(dir[0] ? dir : "/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }
    return PDBM_OK;
}
#endif
