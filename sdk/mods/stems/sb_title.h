/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OQ_STEMS_SB_TITLE_H
#define OQ_STEMS_SB_TITLE_H
/* Stems 3.1: the title tag of the track a STEMS folder was made from.
 *
 * The browser row only knows the track's title, while a STEMS folder is named after the
 * file (cut short, "_" for a trailing space, "-1" for a duplicate, ...). Every folder made
 * by the AZ-MODS app holds source.json with the file's path on the stick, so the title is
 * read from that file's own tag: ID3v2 (MP3, and the "id3 " chunk of WAV / AIFF) or a WAV
 * LIST/INFO INAM. Settings thread only; plain stdio, a few small reads per file. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
/* Paths are bounded by the buffers below; a cut-short path just fails to open. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"

/* UTF-8 of one code point; 0 when it does not fit. */
static size_t st_put(char *o, size_t at, size_t on, uint32_t c) {
    unsigned char b[4];
    size_t k;
    if (c < 0x80) { b[0] = (unsigned char) c; k = 1; }
    else if (c < 0x800) { b[0] = (unsigned char) (0xc0 | c >> 6); b[1] = (unsigned char) (0x80 | (c & 0x3f)); k = 2; }
    else if (c < 0x10000) { b[0] = (unsigned char) (0xe0 | c >> 12); b[1] = (unsigned char) (0x80 | (c >> 6 & 0x3f)); b[2] = (unsigned char) (0x80 | (c & 0x3f)); k = 3; }
    else { b[0] = (unsigned char) (0xf0 | c >> 18); b[1] = (unsigned char) (0x80 | (c >> 12 & 0x3f)); b[2] = (unsigned char) (0x80 | (c >> 6 & 0x3f)); b[3] = (unsigned char) (0x80 | (c & 0x3f)); k = 4; }
    if (at + k + 1 > on)
        return 0;
    memcpy(o + at, b, k);
    return k;
}
/* An ID3 text frame body (encoding byte first) into UTF-8, first value only. */
static int st_text(const unsigned char *s, size_t n, char *o, size_t on) {
    size_t at = 0;
    if (!n || !on)
        return 0;
    unsigned enc = s[0];
    s++, n--;
    if (enc == 0) {                                   /* ISO-8859-1 */
        for (size_t i = 0; i < n && s[i]; i++) { size_t k = st_put(o, at, on, s[i]); if (!k) break; at += k; }
    } else if (enc == 3) {                            /* UTF-8 */
        for (size_t i = 0; i < n && s[i] && at + 1 < on; i++) o[at++] = (char) s[i];
    } else if (enc == 1 || enc == 2) {                /* UTF-16 with BOM / UTF-16BE */
        int be = enc == 2;
        size_t i = 0;
        if (enc == 1 && n >= 2) {
            if (s[0] == 0xfe && s[1] == 0xff) { be = 1; i = 2; }
            else if (s[0] == 0xff && s[1] == 0xfe) { be = 0; i = 2; }
        }
        for (; i + 1 < n; i += 2) {
            uint32_t c = be ? (uint32_t) (s[i] << 8 | s[i + 1]) : (uint32_t) (s[i + 1] << 8 | s[i]);
            if (!c)
                break;
            if (c >= 0xd800 && c < 0xdc00 && i + 3 < n) {
                uint32_t d = be ? (uint32_t) (s[i + 2] << 8 | s[i + 3]) : (uint32_t) (s[i + 3] << 8 | s[i + 2]);
                if (d >= 0xdc00 && d < 0xe000) { c = 0x10000 + ((c - 0xd800) << 10) + (d - 0xdc00); i += 2; }
            }
            size_t k = st_put(o, at, on, c);
            if (!k)
                break;
            at += k;
        }
    } else
        return 0;
    o[at] = 0;
    return at > 0;
}
static uint32_t st_be32(const unsigned char *b) { return (uint32_t) b[0] << 24 | (uint32_t) b[1] << 16 | (uint32_t) b[2] << 8 | b[3]; }
static uint32_t st_le32(const unsigned char *b) { return (uint32_t) b[3] << 24 | (uint32_t) b[2] << 16 | (uint32_t) b[1] << 8 | b[0]; }
static uint32_t st_sync(const unsigned char *b) { return (uint32_t) (b[0] & 0x7f) << 21 | (uint32_t) (b[1] & 0x7f) << 14 | (uint32_t) (b[2] & 0x7f) << 7 | (b[3] & 0x7f); }

/* The ID3v2 tag at the current position of f: its TIT2 (TT2 in v2.2). */
static int st_id3(FILE *f, char *o, size_t on) {
    unsigned char h[10];
    if (fread(h, 1, 10, f) != 10 || memcmp(h, "ID3", 3) || h[3] < 2 || h[3] > 4)
        return 0;
    unsigned ver = h[3];
    long end = ftell(f) + (long) st_sync(h + 6);
    if ((h[5] & 0x40) && ver >= 3) {                  /* extended header */
        unsigned char x[4];
        if (fread(x, 1, 4, f) != 4)
            return 0;
        long skip = ver == 4 ? (long) st_sync(x) - 4 : (long) st_be32(x);
        if (skip < 0 || fseek(f, skip, SEEK_CUR))
            return 0;
    }
    unsigned hl = ver == 2 ? 6 : 10;
    for (int frames = 0; frames < 200 && ftell(f) + (long) hl <= end; frames++) {
        unsigned char fh[10];
        if (fread(fh, 1, hl, f) != hl || !fh[0])
            return 0;
        uint32_t sz = ver == 2 ? (uint32_t) (fh[3] << 16 | fh[4] << 8 | fh[5]) : ver == 4 ? st_sync(fh + 4) : st_be32(fh + 4);
        if (!sz || (long) sz > end - ftell(f))
            return 0;
        int title = ver == 2 ? !memcmp(fh, "TT2", 3) : !memcmp(fh, "TIT2", 4);
        if (title) {
            unsigned char buf[1024];
            size_t want = sz;
            if (ver == 4 && (fh[9] & 0x01) && want >= 4) {   /* data length indicator */
                unsigned char dl[4];
                if (fread(dl, 1, 4, f) != 4)
                    return 0;
                want -= 4;
            }
            if (want > sizeof buf)
                want = sizeof buf;
            size_t got = fread(buf, 1, want, f);
            return st_text(buf, got, o, on);
        }
        if (fseek(f, (long) sz, SEEK_CUR))
            return 0;
    }
    return 0;
}
/* RIFF/WAVE or FORM/AIFF: the "id3 " chunk, else (WAV) LIST/INFO INAM. */
static int st_chunks(FILE *f, int riff, char *o, size_t on) {
    unsigned char ch[8];
    char inam[256] = "";
    for (int n = 0; n < 64 && fread(ch, 1, 8, f) == 8; n++) {
        uint32_t sz = riff ? st_le32(ch + 4) : st_be32(ch + 4);
        long next = ftell(f) + (long) sz + (sz & 1);
        if (!memcmp(ch, "id3 ", 4) || !memcmp(ch, "ID3 ", 4)) {
            if (st_id3(f, o, on))
                return 1;
        } else if (riff && !memcmp(ch, "LIST", 4) && sz >= 4) {
            unsigned char t[4];
            if (fread(t, 1, 4, f) == 4 && !memcmp(t, "INFO", 4)) {
                long lend = next;
                unsigned char sh[8];
                while (ftell(f) + 8 <= lend && fread(sh, 1, 8, f) == 8) {
                    uint32_t ss = st_le32(sh + 4);
                    long snext = ftell(f) + (long) ss + (ss & 1);
                    if (!memcmp(sh, "INAM", 4) && ss) {
                        unsigned char b[255];
                        size_t got = fread(b, 1, ss < sizeof b ? ss : sizeof b, f);
                        unsigned char z[256];
                        z[0] = 3;                   /* treat as UTF-8 */
                        memcpy(z + 1, b, got);
                        st_text(z, got + 1, inam, sizeof inam);
                    }
                    if (fseek(f, snext, SEEK_SET))
                        break;
                }
            }
        }
        if (fseek(f, next, SEEK_SET))
            break;
    }
    if (inam[0]) {
        snprintf(o, on, "%s", inam);
        return 1;
    }
    return 0;
}
/* The title tag of the file at path; 0 when there is none we can read. */
static int st_file_title(const char *path, char *o, size_t on) {
    FILE *f = fopen(path, "rbe");
    if (!f)
        return 0;
    unsigned char h[12];
    int ok = 0;
    if (fread(h, 1, 12, f) == 12) {
        if (!memcmp(h, "ID3", 3)) {
            rewind(f);
            ok = st_id3(f, o, on);
        } else if (!memcmp(h, "RIFF", 4) && !memcmp(h + 8, "WAVE", 4))
            ok = st_chunks(f, 1, o, on);
        else if (!memcmp(h, "FORM", 4) && (!memcmp(h + 8, "AIFF", 4) || !memcmp(h + 8, "AIFC", 4)))
            ok = st_chunks(f, 0, o, on);
    }
    fclose(f);
    return ok;
}
/* Text folded for comparing: lower case, accents dropped, so a precomposed "é" (as in
 * source.json and most tags) equals a decomposed "e" + accent (as file names are often
 * stored); line breaks and repeated or trailing spaces do not count. */
static const char st_latin1_base[65] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
static void st_fold(const char *in, char *o, size_t on) {
    size_t at = 0;
    const unsigned char *s = (const unsigned char *) in;
    while (*s && at + 1 < on) {
        uint32_t c = *s;
        int k = 1;
        if (c >= 0xc0 && c < 0xe0 && (s[1] & 0xc0) == 0x80) { c = (c & 0x1f) << 6 | (s[1] & 0x3f); k = 2; }
        else if (c >= 0xe0 && c < 0xf0 && (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80) { c = (c & 0x0f) << 12 | (uint32_t) (s[1] & 0x3f) << 6 | (s[2] & 0x3f); k = 3; }
        if (c >= 0x300 && c < 0x370) { s += k; continue; }      /* combining accent */
        if (c >= 0xc0 && c < 0x100) c = (unsigned char) st_latin1_base[c - 0xc0];
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';           /* line breaks in tags */
        if (c == ' ' && (!at || o[at - 1] == ' ')) { s += k; continue; }   /* one space only */
        if (c < 0x80) o[at++] = (char) (c >= 'A' && c <= 'Z' ? c + 32 : c);
        else { for (int i = 0; i < k && at + 1 < on; i++) o[at++] = (char) s[i]; }
        s += k;
    }
    while (at && (o[at - 1] == ' ' || o[at - 1] == '\n' || o[at - 1] == '\t')) at--;
    o[at] = 0;
}
static int st_same(const char *a, const char *b) {
    char fa[512], fb[512];
    st_fold(a, fa, sizeof fa);
    st_fold(b, fb, sizeof fb);
    return fa[0] && !strcmp(fa, fb);
}
/* root/rel on the stick, each name matched exactly or else by st_same (decomposed accents). */
static int st_resolve(const char *root, const char *rel, char *o, size_t on) {
    snprintf(o, on, "%s/%s", root, rel);
    struct stat st;
    if (!stat(o, &st))
        return 1;
    char cur[900], part[300];
    snprintf(cur, sizeof cur, "%s", root);
    const char *p = rel;
    while (*p) {
        const char *e = strchr(p, '/');
        size_t n = e ? (size_t) (e - p) : strlen(p);
        if (n >= sizeof part)
            return 0;
        memcpy(part, p, n);
        part[n] = 0;
        char next[900];
        snprintf(next, sizeof next, "%s/%s", cur, part);
        if (stat(next, &st)) {
            DIR *d = opendir(cur);
            if (!d)
                return 0;
            struct dirent *de;
            int found = 0;
            while ((de = readdir(d)))
                if (de->d_name[0] != '.' && st_same(de->d_name, part)) {
                    snprintf(next, sizeof next, "%s/%s", cur, de->d_name);
                    found = 1;
                    break;
                }
            closedir(d);
            if (!found)
                return 0;
        }
        snprintf(cur, sizeof cur, "%s", next);
        p += n;
        if (*p == '/')
            p++;
    }
    snprintf(o, on, "%s", cur);
    return 1;
}
/* <stick root>/<file from source.json> of the STEMS folder dir; 0 without one. */
static int st_source_path(const char *root, const char *dir, char *o, size_t on) {
    char p[700];
    snprintf(p, sizeof p, "%s/source.json", dir);
    FILE *f = fopen(p, "re");
    if (!f)
        return 0;
    char buf[2048];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    const char *k = strstr(buf, "\"file\"");
    if (!k)
        return 0;
    k = strchr(k + 6, ':');
    if (!k)
        return 0;
    k = strchr(k, '"');
    char rel[600];
    if (!k || !json_str(k + 1, rel, sizeof rel) || !rel[0] || strstr(rel, ".."))
        return 0;
    return st_resolve(root, rel[0] == '/' ? rel + 1 : rel, o, on);
}
#pragma GCC diagnostic pop
#endif
