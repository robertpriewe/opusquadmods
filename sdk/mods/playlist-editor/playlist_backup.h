/* R80 playlist editing: copy the USB library before the first write of a boot.
 *
 * <usb>/OPUSMOD_BACKUP/exportLibrary.db and export.pdb are byte copies taken
 * on the library thread, inside the claimed onMessage and before BEGIN, so no
 * transaction of ours is open. Each file is written to a .tmp sibling (O_EXCL,
 * fsync), renamed over the previous boot's copy, and the directory is fsynced.
 * A hot journal or WAL beside the DB, too little free space, or any I/O error
 * refuses the backup, and the caller then writes nothing. Pure POSIX; host
 * tests run it on fixture copies. */
#ifndef OQ_PLAYLIST_BACKUP_H
#define OQ_PLAYLIST_BACKUP_H
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

enum plb_result {
    PLB_OK = 0,
    PLB_PATH = -1,    /* not <usb>/PIONEER/rekordbox/exportLibrary.db */
    PLB_JOURNAL = -2, /* -journal or -wal beside the DB: SQLite is mid-write */
    PLB_SPACE = -3,   /* free space below both copies + PLB_MARGIN */
    PLB_IO = -4
};
#define PLB_DIR "OPUSMOD_BACKUP"
#define PLB_MARGIN (1024 * 1024)
#define PLB_DB_TAIL "/PIONEER/rekordbox/exportLibrary.db"

static int plb_fsync_dir(const char *dir) {
    int fd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    int rc = fsync(fd);
    close(fd);
    return rc;
}
/* src -> dst via dst.tmp. 0 or PLB_IO. */
static int plb_copy(const char *src, const char *dst) {
    char tmp[256];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", dst) >= (int) sizeof tmp)
        return PLB_IO;
    unlink(tmp); /* a stale tmp from a crashed copy */
    int in = open(src, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (in < 0)
        return PLB_IO;
    int out = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0644);
    if (out < 0) {
        close(in);
        return PLB_IO;
    }
    static char buf[65536]; /* library thread only */
    int ok = 1;
    for (;;) {
        ssize_t got = read(in, buf, sizeof buf);
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0) {
            ok = got == 0;
            break;
        }
        for (ssize_t at = 0; at < got;) {
            ssize_t sent = write(out, buf + at, (size_t) (got - at));
            if (sent < 0 && errno == EINTR)
                continue;
            if (sent <= 0) {
                ok = 0;
                break;
            }
            at += sent;
        }
        if (!ok)
            break;
    }
    if (ok && fsync(out))
        ok = 0;
    if (close(out))
        ok = 0;
    close(in);
    if (!ok || rename(tmp, dst)) {
        unlink(tmp);
        return PLB_IO;
    }
    return PLB_OK;
}
/* Backs up db (and export.pdb beside it, when present). `made` gets the
 * backup directory on success. */
static int plb_backup(const char *db, char *made, size_t cap) {
    size_t n = db ? strlen(db) : 0, tail = sizeof PLB_DB_TAIL - 1;
    if (n <= tail || n >= 200 || strcmp(db + n - tail, PLB_DB_TAIL))
        return PLB_PATH;
    char root[200], dir[232], side[232], pdb[232], dst[256];
    snprintf(root, sizeof root, "%.*s", (int) (n - tail), db);
    snprintf(dir, sizeof dir, "%s/" PLB_DIR, root);
    snprintf(pdb, sizeof pdb, "%.*s/export.pdb", (int) (n - sizeof "/exportLibrary.db" + 1), db);
    struct stat st, ps;
    for (int i = 0; i < 2; i++) {
        snprintf(side, sizeof side, "%s%s", db, i ? "-wal" : "-journal");
        if (!lstat(side, &st))
            return PLB_JOURNAL;
    }
    if (stat(db, &st) || !S_ISREG(st.st_mode))
        return PLB_PATH;
    int have_pdb = !stat(pdb, &ps) && S_ISREG(ps.st_mode);
    unsigned long long need = (unsigned long long) st.st_size + PLB_MARGIN +
                              (have_pdb ? (unsigned long long) ps.st_size : 0);
    if (mkdir(dir, 0755) && errno != EEXIST)
        return PLB_IO;
    if (lstat(dir, &st) || !S_ISDIR(st.st_mode))
        return PLB_IO;
    struct statvfs vf; /* where the copies go */
    if (statvfs(dir, &vf))
        return PLB_IO;
    if ((unsigned long long) vf.f_bavail * vf.f_frsize < need)
        return PLB_SPACE;
    snprintf(dst, sizeof dst, "%s/exportLibrary.db", dir);
    if (plb_copy(db, dst))
        return PLB_IO;
    snprintf(dst, sizeof dst, "%s/export.pdb", dir);
    if (have_pdb && plb_copy(pdb, dst))
        return PLB_IO;
    if (plb_fsync_dir(dir))
        return PLB_IO;
    plb_fsync_dir(root); /* the directory entry of a new OPUSMOD_BACKUP */
    if (made)
        snprintf(made, cap, "%s", dir);
    return PLB_OK;
}
#endif
