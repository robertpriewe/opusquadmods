/* Main-Browse playlist track reorder: OPUS 1.32 native commit engine (R80 lab).
 *
 * EP136 has no movePlaylistTrack (the AZ InformationUpdater move and its
 * updater slot +0x50 do not exist here). It does have the composition stock
 * itself uses to rewrite an ordered list, all on the library thread inside
 * MusicLibraryServer::onMessage:
 *   InformationUpdater::deletePlaylist  0xfe9138  updater +0x38/+0x28, then +0x10
 *       per surviving sibling with sequenceNo i+1, then collector drops
 *   Tag List -> playlist                0xfe7eb0  updater +0x10, then
 *       +0x18(playlist_id, content_id, i+1) per track
 * One commit here is: SqlitePlaylist updater (BEGIN), +0x38 DELETE FROM
 * playlist_content WHERE playlist_id, +0x18 per track in the new order, read
 * back on the same connection, destroy the updater (COMMIT, or ROLLBACK when
 * its state is nonzero), then ListCacheCollector 0xfc3ba8(playlist_id) and the
 * handler's release pass 0xfc5e78, as 0xfe9138 / 0x10414b8 do. Every address
 * is for the stock EP136 1.32 (sha256 811a6b74...4e22); see REPORT.md.
 *
 * Nothing here is built unless OQ_PLAYLIST_REORDER is defined; in that build
 * nothing is written unless the Mod Menu's Playlist Editing switch (a
 * diagnostics-only item) is on and a knob gesture queued a request
 * (playlist_reorder_knob.h). The planner and engine are pure and take their native entry
 * points through struct plr_env so they run in host unit tests. No AZ code is
 * copied; the safety rules follow the AZ design (see REPORT.md, GPL notes). */
#ifndef OQ_NATIVE_PLAYLIST_REORDER_H
#define OQ_NATIVE_PLAYLIST_REORDER_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { PLR_MAX = 2048 }; /* tracks in one playlist we will rewrite */

enum plr_result {
    PLR_OK = 0,
    PLR_NOOP = 1,        /* from == to: nothing written */
    PLR_BAD = -1,        /* malformed request */
    PLR_ABI = -2,        /* an object is not the EP136 class we expect */
    PLR_BUSY = -3,       /* a transaction is open on the connection: try a later message */
    PLR_STALE = -4,      /* the DB no longer matches what was on screen */
    PLR_WRITE = -5,      /* a native write failed; the updater rolled back */
    PLR_READBACK = -6,   /* in-transaction read-back differed; forced rollback */
    PLR_VERIFY = -7,     /* committed, but the post-commit read-back differs */
    PLR_LIMIT = -8,
    PLR_SPACE = -9,      /* volume below stock's minimum free space: nothing written */
    PLR_BACKUP = -10     /* the per-boot USB backup failed: nothing written */
};

/* EP136 1.32 layout. Offsets are bytes; *_SLOT are vtable byte offsets. */
#define PLR_MLSERVER_VT         0x2499fe0u /* music_library::MusicLibraryServer */
#define PLR_ONMSG_SLOT          (PLR_MLSERVER_VT + 0x50u) /* -> 0x1042b00 */
#define PLR_ONMSG_FN            0x1042b00u
#define PLR_SERVER_DISPATCH     0x1c8u /* 0x1042b00: ldr x0,[x0,#0x1c8]; b 0x103de80 */
#define PLR_DISPATCH_UPDATE     0x20u  /* 0x103e3d4: msg 0x3104 -> [disp+0x20] -> 0x10414b8 */
#define PLR_HANDLER_NOTIFIER    0x10u  /* 0x104150c: [h+0x10] -> 0xfc5e78 after a write */
#define PLR_HANDLER_INFOUPD     0x30u  /* 0x10414f8: [h+0x30] -> 0xfe9138 */
#define PLR_IU_LIBRARY          0x00u  /* 0xfe9144: ldr x19,[x0] */
#define PLR_IU_COLLECTOR        0x10u  /* 0x1082990: IU+0x10 = bundle+0xb0 collector */
#define PLR_LIBRARY_SQLDB       0x40u  /* 0xfe915c / 0xfe7f1c */
#define PLR_LIBRARY_FILE        0x28u  /* juce::File of the library, 0xfe9168: add x0,x19,#0x28 */
#define PLR_IU_MIN_FREE         0x48u  /* int64 0x5000; 0xfe9170: b.gt fail when > free */
#define PLR_FREE_SPACE_FN       0x1f522b0u /* juce::File::getBytesFreeOnVolume (statfs64, 0 on error) */
#define PLR_SQLDB_VT            0x24975f0u /* music_library::SqliteDataBase */
#define PLR_SQLDB_CONN          0x10u  /* 0x100e558: SqlitePlaylist{vt, [db+0x10]} */
#define PLR_SQLDB_PLAYLIST_SLOT 0x20u  /* 0x100e538: new SqlitePlaylist */
#define PLR_PLAYLIST_VT         0x2496ef0u /* music_library::SqlitePlaylist */
#define PLR_PLAYLIST_UPD_SLOT   0x38u  /* 0x10181e8: new updater, BEGIN TRANSACTION */
#define PLR_UPDATER_VT          0x2496ea0u /* music_library::SqlitePlaylistUpdater */
#define PLR_UPDATER_STATE       0x10u  /* int at updater+8+8: 0 -> COMMIT in 0x1018240 */
#define PLR_UPD_ADD_SLOT        0x18u  /* 0x102a120(upd, playlist_id, content_id, sequenceNo) */
#define PLR_UPD_CLEAR_SLOT      0x38u  /* 0x1018f68(upd, playlist_id): DELETE ... WHERE playlist_id */
#define PLR_DELETE_SLOT         0x08u  /* deleting destructor (both classes) */
#define PLR_COLLECTOR_VT        0x24a17a0u /* music_library::ListCacheCollector */
#define PLR_COLLECTOR_DROP_FN   0xfc3ba8u  /* removePlaylistTrackListCache(collector, u32) */
#define PLR_NOTIFIER_RELEASE_FN 0xfc5e78u  /* handler notifier release pass (stock 0x1041514) */

/* Native entry points and SQL reads, injectable for tests. */
struct plr_env {
    uintptr_t sqldb_vt, playlist_vt, updater_vt, collector_vt;
    int (*autocommit)(void *conn);
    /* Current order: content_id and sequenceNo by (sequenceNo, rowid).
     * Returns 0, or -1 on SQL error, or 1 when there are more than cap rows. */
    int (*read_order)(void *conn, uint32_t playlist_id, uint32_t *ids, uint32_t *seqs,
                      unsigned cap, unsigned *n);
    void (*drop_track_cache)(void *collector, uint32_t playlist_id);
    void (*release)(void *notifier);
    long long (*bytes_free)(const void *file); /* juce::File::getBytesFreeOnVolume */
};

struct plr_request {
    uint32_t playlist_id;
    unsigned count;
    unsigned from, to;       /* the move, for the export.pdb mirror (gate 4) */
    /* gesture requests leave count 0 and give what the screen showed instead;
     * plr_fill_from_db turns that into expect/order on the library thread */
    uint32_t grabbed;        /* content_id of the grabbed row (pm_decode track) */
    unsigned rows;           /* rows the list showed ([TrackListWidget+0x2e8]) */
    uint32_t expect[PLR_MAX]; /* content_id in the order the list showed */
    uint32_t order[PLR_MAX];  /* content_id in the order to write */
};

/* Display rows are 0-based and equal stored positions ("#" ascending only). */
static int plr_plan_move(const uint32_t *ids, unsigned n, unsigned from, unsigned to,
                        uint32_t grabbed, uint32_t *out) {
    if (!ids || !out || !n || n > PLR_MAX || from >= n || to >= n || ids[from] != grabbed)
        return PLR_BAD;
    memcpy(out, ids, n * sizeof *ids);
    if (from == to)
        return PLR_NOOP;
    /* the grabbed row lands on display row `to`; the rows between shift by one */
    if (from < to)
        memmove(out + from, ids + from + 1, (size_t) (to - from) * sizeof *ids);
    else
        memmove(out + to + 1, ids + to, (size_t) (from - to) * sizeof *ids);
    out[to] = grabbed;
    return PLR_OK;
}

static int plr_same_multiset(const uint32_t *a, const uint32_t *b, unsigned n) {
    /* O(n^2) is fine at PLR_MAX and needs no allocation on the library thread */
    for (unsigned i = 0; i < n; i++) {
        unsigned ca = 0, cb = 0;
        for (unsigned j = 0; j < n; j++) {
            ca += a[j] == a[i];
            cb += b[j] == a[i];
        }
        if (ca != cb)
            return 0;
    }
    return 1;
}

static uintptr_t plr_q(uintptr_t p, uintptr_t off) {
    return p && !(p & 7) ? *(const uintptr_t *) (p + off) : 0;
}
static uintptr_t plr_slot(uintptr_t obj, uintptr_t off) {
    uintptr_t vt = plr_q(obj, 0);
    return vt ? *(const uintptr_t *) (vt + off) : 0;
}

/* Is the stored order exactly `want`, with sequenceNo 1..n and no extra rows? */
static int plr_matches(const struct plr_env *e, void *conn, uint32_t pid, const uint32_t *want,
                      unsigned n, uint32_t *ids, uint32_t *seqs) {
    unsigned got = 0;
    int rc = e->read_order(conn, pid, ids, seqs, PLR_MAX, &got);
    if (rc < 0)
        return -1;
    if (rc > 0 || got != n)
        return 0;
    for (unsigned i = 0; i < n; i++)
        if (seqs[i] != i + 1 || ids[i] != want[i])
            return 0;
    return 1;
}

/* Library thread, inside MusicLibraryServer::onMessage, before the message is
 * dispatched. `infoupd` and `notifier` come from that server's own dispatcher
 * chain (plr_chain). Scratch must hold 2*PLR_MAX words. */
static int plr_commit(const struct plr_env *e, uintptr_t infoupd, uintptr_t notifier,
                     const struct plr_request *rq, uint32_t *scratch) {
    if (!e || !rq || !scratch || !rq->playlist_id || !rq->count)
        return PLR_BAD;
    if (rq->count > PLR_MAX)
        return PLR_LIMIT;
    if (!plr_same_multiset(rq->expect, rq->order, rq->count))
        return PLR_BAD; /* a reorder never adds or drops a row */
    uintptr_t lib = plr_q(infoupd, PLR_IU_LIBRARY), db = plr_q(lib, PLR_LIBRARY_SQLDB);
    uintptr_t collector = plr_q(infoupd, PLR_IU_COLLECTOR);
    if (!db || plr_q(db, 0) != e->sqldb_vt || !collector || plr_q(collector, 0) != e->collector_vt)
        return PLR_ABI;
    void *conn = (void *) plr_q(db, PLR_SQLDB_CONN);
    if (!conn || !e->bytes_free)
        return PLR_ABI;
    /* the stock writers' gate (0xfe916c, 0xfe7f08, 0xfe383c): refuse when
     * [IU+0x48] > getBytesFreeOnVolume(library file), signed compare */
    if ((long long) plr_q(infoupd, PLR_IU_MIN_FREE) > e->bytes_free((const void *) (lib + PLR_LIBRARY_FILE)))
        return PLR_SPACE;
    if (e->autocommit(conn) != 1)
        return PLR_BUSY;
    uint32_t *ids = scratch, *seqs = scratch + PLR_MAX;
    int m = plr_matches(e, conn, rq->playlist_id, rq->expect, rq->count, ids, seqs);
    if (m < 0)
        return PLR_WRITE;
    if (!m)
        return PLR_STALE;

    uintptr_t (*make_playlist)(uintptr_t) =
        (uintptr_t (*)(uintptr_t)) plr_slot(db, PLR_SQLDB_PLAYLIST_SLOT);
    uintptr_t pl = make_playlist ? make_playlist(db) : 0;
    if (!pl)
        return PLR_ABI;
    void (*del_pl)(uintptr_t) = (void (*)(uintptr_t)) plr_slot(pl, PLR_DELETE_SLOT);
    if (plr_q(pl, 0) != e->playlist_vt || !del_pl) {
        if (del_pl)
            del_pl(pl);
        return PLR_ABI;
    }
    uintptr_t (*make_updater)(uintptr_t) =
        (uintptr_t (*)(uintptr_t)) plr_slot(pl, PLR_PLAYLIST_UPD_SLOT);
    uintptr_t upd = make_updater(pl); /* BEGIN TRANSACTION */
    int rc = PLR_OK;
    if (!upd) {
        del_pl(pl);
        return PLR_ABI;
    }
    void (*del_upd)(uintptr_t) = (void (*)(uintptr_t)) plr_slot(upd, PLR_DELETE_SLOT);
    int *state = (int *) (upd + PLR_UPDATER_STATE);
    if (plr_q(upd, 0) != e->updater_vt) {
        rc = PLR_ABI; /* unknown object: do not call it or poke its state */
    } else if (*state) {
        rc = PLR_BUSY; /* BEGIN failed; its destructor will ROLLBACK nothing of ours */
    } else {
        int (*clear)(uintptr_t, uint32_t) =
            (int (*)(uintptr_t, uint32_t)) plr_slot(upd, PLR_UPD_CLEAR_SLOT);
        int (*add)(uintptr_t, uint32_t, uint32_t, uint32_t) =
            (int (*)(uintptr_t, uint32_t, uint32_t, uint32_t)) plr_slot(upd, PLR_UPD_ADD_SLOT);
        if (clear(upd, rq->playlist_id))
            rc = PLR_WRITE;
        for (unsigned i = 0; rc == PLR_OK && i < rq->count; i++)
            if (add(upd, rq->playlist_id, rq->order[i], i + 1))
                rc = PLR_WRITE;
        if (rc == PLR_OK && plr_matches(e, conn, rq->playlist_id, rq->order, rq->count, ids, seqs) != 1)
            rc = PLR_READBACK;
        if (rc != PLR_OK && !*state)
            *state = 1; /* make the destructor ROLLBACK */
    }
    /* COMMIT when state == 0, else ROLLBACK. Even an unexpected object is ended
     * through its own deleter: plr_install proved the playlist slot builds an
     * updater, and leaving a BEGIN open would wedge the library. */
    if (del_upd)
        del_upd(upd);
    del_pl(pl);
    if (rc != PLR_OK)
        return rc;
    if (e->autocommit(conn) != 1 ||
        plr_matches(e, conn, rq->playlist_id, rq->order, rq->count, ids, seqs) != 1)
        return PLR_VERIFY;
    e->drop_track_cache((void *) collector, rq->playlist_id);
    if (notifier && e->release)
        e->release((void *) notifier);
    return PLR_OK;
}

/* Library thread, before plr_commit, for a gesture request (count == 0): the
 * message thread cannot read SQLite, so read the order here once. It must
 * still have exactly the rows the list showed, numbered 1..n, with the
 * grabbed track at `from`; otherwise STALE and nothing is written. */
static int plr_fill_from_db(const struct plr_env *e, void *conn, struct plr_request *rq,
                            uint32_t *scratch) {
    if (!e || !rq || !scratch || !rq->playlist_id || !rq->grabbed || rq->rows < 2 ||
        rq->rows > PLR_MAX || rq->from >= rq->rows || rq->to >= rq->rows)
        return PLR_BAD;
    unsigned n = 0;
    int rc = e->read_order(conn, rq->playlist_id, rq->expect, scratch, PLR_MAX, &n);
    if (rc < 0)
        return PLR_WRITE;
    if (rc > 0)
        return PLR_LIMIT;
    if (n != rq->rows || rq->expect[rq->from] != rq->grabbed)
        return PLR_STALE;
    for (unsigned i = 0; i < n; i++)
        if (scratch[i] != i + 1)
            return PLR_STALE;
    rq->count = n;
    return plr_plan_move(rq->expect, n, rq->from, rq->to, rq->grabbed, rq->order);
}

/* export.pdb and its staging sibling next to the committed exportLibrary.db.
 * 1 when both fit; the stage name is fixed so a crashed stage is found again. */
static int plr_pdb_paths(const char *db, char *live, char *staged, size_t cap) {
    const char *slash = db ? strrchr(db, '/') : 0;
    if (!slash || strcmp(slash + 1, "exportLibrary.db"))
        return 0;
    int k = (int) (slash - db);
    int a = snprintf(live, cap, "%.*s/export.pdb", k, db);
    int b = snprintf(staged, cap, "%.*s/export.pdb.oq-r80-stage", k, db);
    return a > 0 && b > 0 && (size_t) a < cap && (size_t) b < cap;
}

/* server -> dispatcher -> update handler -> InformationUpdater, as 0x103e3d4. */
static int plr_chain(uintptr_t server, uintptr_t *infoupd, uintptr_t *notifier) {
    uintptr_t disp = plr_q(server, PLR_SERVER_DISPATCH);
    uintptr_t handler = plr_q(disp, PLR_DISPATCH_UPDATE);
    *infoupd = plr_q(handler, PLR_HANDLER_INFOUPD);
    *notifier = plr_q(handler, PLR_HANDLER_NOTIFIER);
    return *infoupd != 0;
}

#ifdef OQ_PLAYLIST_REORDER
/* ---- live wiring (disabled by default; never built into a release yet) ----
 * The message thread fills g_plr_req and moves g_plr_state 0 -> PENDING; the
 * first onMessage of the server whose connection is that USB's
 * exportLibrary.db claims it, commits, and publishes the result. */
#include "playlist_pdb_move.h"
#include "playlist_backup.h"
#include <dlfcn.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
enum { PLR_IDLE, PLR_PENDING, PLR_RUNNING, PLR_DONE };
static struct plr_request g_plr_req;
static char g_plr_db[160];
static int g_plr_state, g_plr_result; /* __atomic_* only */
static int g_plr_pdb_result;          /* PDBM_*, or 99 when not attempted */
static int g_plr_backup_result = 99;  /* PLB_*, or 99 when not attempted this request */
static unsigned g_plr_ms;             /* library thread time of the last claimed request */
/* (st_dev, st_ino) of each library backed up this boot; library thread only */
static struct { dev_t dev; ino_t ino; } g_plr_backed[4];
static unsigned g_plr_backed_n;
static uint32_t g_plr_scratch[2 * PLR_MAX]; /* library threads take turns via g_plr_state */
static void (*plr_onmsg_original)(void *, void *, unsigned);
static struct {
    int (*autocommit)(void *);
    int (*prepare)(void *, const char *, int, void **, const char **);
    int (*bind)(void *, int, long long);
    int (*step)(void *);
    long long (*column)(void *, int);
    int (*finalize)(void *);
    const char *(*filename)(void *, const char *);
} plr_sq;
static int plr_sq_autocommit(void *c) { return plr_sq.autocommit(c); }
static int plr_sq_read(void *c, uint32_t pid, uint32_t *ids, uint32_t *seqs, unsigned cap,
                      unsigned *n) {
    void *st = 0;
    *n = 0;
    if (plr_sq.prepare(c, "SELECT content_id, sequenceNo FROM playlist_content "
                         "WHERE playlist_id = ?1 ORDER BY sequenceNo, rowid", -1, &st, 0) || !st)
        return -1;
    int rc = 0, s;
    plr_sq.bind(st, 1, pid);
    while ((s = plr_sq.step(st)) == 100 /* SQLITE_ROW */) {
        if (*n == cap) {
            rc = 1;
            break;
        }
        ids[*n] = (uint32_t) plr_sq.column(st, 0);
        seqs[*n] = (uint32_t) plr_sq.column(st, 1);
        ++*n;
    }
    if (!rc && s != 101 /* SQLITE_DONE */)
        rc = -1;
    plr_sq.finalize(st);
    return rc;
}
static void plr_drop(void *collector, uint32_t pid) {
    ((void (*)(void *, uint32_t)) PLR_COLLECTOR_DROP_FN)(collector, pid);
}
static void plr_release(void *notifier) { ((void (*)(void *)) PLR_NOTIFIER_RELEASE_FN)(notifier); }
static long long plr_bytes_free(const void *file) {
    return ((long long (*)(const void *)) PLR_FREE_SPACE_FN)(file);
}
static const struct plr_env plr_live_env = {PLR_SQLDB_VT, PLR_PLAYLIST_VT, PLR_UPDATER_VT,
    PLR_COLLECTOR_VT, plr_sq_autocommit, plr_sq_read, plr_drop, plr_release, plr_bytes_free};

static void plr_onmsg_hook(void *server, void *msg, unsigned arg) {
    int want = PLR_PENDING;
    if (__atomic_load_n(&editor_authorized, __ATOMIC_ACQUIRE) && __atomic_load_n(&g_plr_state, __ATOMIC_ACQUIRE) == PLR_PENDING) {
        uintptr_t iu = 0, nt = 0;
        uintptr_t db = plr_chain((uintptr_t) server, &iu, &nt) ?
            plr_q(plr_q(iu, PLR_IU_LIBRARY), PLR_LIBRARY_SQLDB) : 0;
        void *conn = db && plr_q(db, 0) == PLR_SQLDB_VT ? (void *) plr_q(db, PLR_SQLDB_CONN) : 0;
        const char *path = conn ? plr_sq.filename(conn, "main") : 0;
        if (path && !strcmp(path, g_plr_db) &&
            __atomic_compare_exchange_n(&g_plr_state, &want, PLR_RUNNING, 0, __ATOMIC_ACQ_REL,
                                        __ATOMIC_RELAXED)) {
            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            int r = g_plr_req.count ? PLR_OK : plr_fill_from_db(&plr_live_env, conn, &g_plr_req, g_plr_scratch);
            /* first write of this boot to this library: back it up first */
            int backup = 99;
            struct stat dbst;
            if (r == PLR_OK && !stat(g_plr_db, &dbst)) {
                unsigned i = 0;
                while (i < g_plr_backed_n && (g_plr_backed[i].dev != dbst.st_dev || g_plr_backed[i].ino != dbst.st_ino))
                    i++;
                if (i == g_plr_backed_n) {
                    backup = plb_backup(g_plr_db, 0, 0);
                    if (backup == PLB_OK && g_plr_backed_n < 4) {
                        g_plr_backed[g_plr_backed_n].dev = dbst.st_dev;
                        g_plr_backed[g_plr_backed_n++].ino = dbst.st_ino;
                    }
                }
                if (backup != 99 && backup != PLB_OK)
                    r = PLR_BACKUP;
            } else if (r == PLR_OK) {
                r = PLR_BACKUP;
            }
            if (r == PLR_OK)
                r = plr_commit(&plr_live_env, iu, nt, &g_plr_req, g_plr_scratch);
            /* gate 4: OneLibrary is committed; mirror the same move into
             * export.pdb (staged, checked against the pre-move order, renamed
             * over). A failure leaves export.pdb as it was. */
            int pdb = 99;
            char live[192], staged[192];
            if (r == PLR_OK && plr_pdb_paths(g_plr_db, live, staged, sizeof live)) {
                unlink(staged);
                pdb = pdbm_stage_move(live, staged, g_plr_req.playlist_id, g_plr_req.expect,
                                      g_plr_req.count, g_plr_req.from, g_plr_req.to);
                if (pdb == PDBM_OK)
                    pdb = pdbm_publish(staged, live);
                else
                    unlink(staged);
            }
            clock_gettime(CLOCK_MONOTONIC, &t1);
            g_plr_ms = (unsigned) ((t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000);
            __atomic_store_n(&g_plr_backup_result, backup, __ATOMIC_RELAXED);
            __atomic_store_n(&g_plr_pdb_result, pdb, __ATOMIC_RELAXED);
            __atomic_store_n(&g_plr_result, r, __ATOMIC_RELAXED);
            /* BUSY: leave it for the next message on this server */
            __atomic_store_n(&g_plr_state, r == PLR_BUSY ? PLR_PENDING : PLR_DONE, __ATOMIC_RELEASE);
        }
    }
    plr_onmsg_original(server, msg, arg);
}

/* Constructor time. Checks every slot and entry it will call against stock
 * 1.32 and returns 1 when the onMessage wrapper is installed. The wrapper only
 * acts on a request the knob gesture queued. */
static int plr_install(int (*patch)(uintptr_t, void *, void **)) {
    /* every slot plr_commit calls, as read from the stock vtables */
    static const uintptr_t want[][2] = {
        {PLR_ONMSG_SLOT, PLR_ONMSG_FN},
        {PLR_SQLDB_VT + PLR_SQLDB_PLAYLIST_SLOT, 0x100e538u},
        {PLR_PLAYLIST_VT + PLR_DELETE_SLOT, 0x100e288u},
        {PLR_PLAYLIST_VT + PLR_PLAYLIST_UPD_SLOT, 0x10181e8u},
        {PLR_UPDATER_VT + PLR_DELETE_SLOT, 0x10184e8u},
        {PLR_UPDATER_VT + PLR_UPD_ADD_SLOT, 0x102a120u},
        {PLR_UPDATER_VT + PLR_UPD_CLEAR_SLOT, 0x1018f68u},
    };
    for (unsigned i = 0; i < sizeof want / sizeof want[0]; i++)
        if (*(const uintptr_t *) want[i][0] != want[i][1])
            return -1;
    /* first instructions of the three direct calls (stp x29,x30 / ldp x4,x3 / stp x29,x30) */
    if (*(const uint32_t *) PLR_COLLECTOR_DROP_FN != 0xa9bc7bfdu ||
        *(const uint32_t *) PLR_FREE_SPACE_FN != 0xa9b67bfdu ||
        *(const uint32_t *) PLR_NOTIFIER_RELEASE_FN != 0xa9450c04u)
        return -1;
    *(void **) &plr_sq.autocommit = dlsym(RTLD_DEFAULT, "sqlite3_get_autocommit");
    *(void **) &plr_sq.prepare = dlsym(RTLD_DEFAULT, "sqlite3_prepare_v2");
    *(void **) &plr_sq.bind = dlsym(RTLD_DEFAULT, "sqlite3_bind_int64");
    *(void **) &plr_sq.step = dlsym(RTLD_DEFAULT, "sqlite3_step");
    *(void **) &plr_sq.column = dlsym(RTLD_DEFAULT, "sqlite3_column_int64");
    *(void **) &plr_sq.finalize = dlsym(RTLD_DEFAULT, "sqlite3_finalize");
    *(void **) &plr_sq.filename = dlsym(RTLD_DEFAULT, "sqlite3_db_filename");
    if (!plr_sq.autocommit || !plr_sq.prepare || !plr_sq.bind || !plr_sq.step || !plr_sq.column ||
        !plr_sq.finalize || !plr_sq.filename)
        return -2;
    return patch(PLR_ONMSG_SLOT, (void *) plr_onmsg_hook, (void **) &plr_onmsg_original) == 1 ? 1 : -3;
}
#endif
#endif
