#ifndef OQMOD_EVENTS_H
#define OQMOD_EVENTS_H
/* OQ Mods SDK: named events for add-on mods (SDK 1.1, works with API version 1).
 *
 * Instead of polling the decks yourself, fill in the handlers you need and call
 * oq_events_poll() from your tick(). It compares each deck with how it was on the last
 * tick and calls your handlers for what changed:
 *
 *     static void loaded(unsigned deck, const struct oqmod_deck *d) { ... }
 *     static const struct oq_events events = { .track_loaded = loaded, .play = started };
 *     static void tick(void) { oq_events_poll(api, &events); }
 *
 * Handlers run on the tick thread, inside your tick(): the same rules apply (keep them
 * short, never block). Unset handlers are skipped. Decks are numbered 1..4. The first
 * poll reports what is already loaded and playing, so a mod switched on mid-set sees the
 * current state. Only one oq_events_poll() per mod: it keeps its state in this file.
 *
 * Also here: oq_track_path(), the full path of the audio file loaded on a deck, and
 * oq_mod_dir(), the mod's own folder on the USB. */
#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "oqmod.h"

struct oq_events {
    /* a track appeared on the deck, or the deck changed to another track */
    void (*track_loaded)(unsigned deck, const struct oqmod_deck *now);
    /* the deck has no track any more */
    void (*track_unloaded)(unsigned deck);
    /* the deck started / stopped playing */
    void (*play)(unsigned deck, const struct oqmod_deck *now);
    void (*pause)(unsigned deck, const struct oqmod_deck *now);
    /* the playing BPM (pitch included) changed by at least 0.05; values are BPM x 100 */
    void (*bpm_changed)(unsigned deck, uint32_t old_bpm_x100, uint32_t new_bpm_x100);
    /* the key shown for the deck's track changed ("" when unknown) */
    void (*key_changed)(unsigned deck, const char *key);
    /* the deck passed its last `seconds_left` seconds (set near_end_seconds; 0 = off) */
    void (*near_end)(unsigned deck, const struct oqmod_deck *now);
    unsigned near_end_seconds;
    /* the screen changed: layout() value, 0 off the WAVEFORM screen, 1..3 deck layouts */
    void (*layout_changed)(int old_layout, int new_layout);
};

static struct {
    int started, layout;
    struct {int known, loaded, playing, near_end; uint32_t bpm, length_ms; char title[128], artist[128], key[8];} deck[5];
} oq_ev_state;

/* A deck counts as "another track" when its title, artist or length changes. */
static int oq_ev_same_track(const struct oqmod_deck *a, const char *title, const char *artist, uint32_t length_ms) {
    return !strcmp(a->title, title) && !strcmp(a->artist, artist) && a->length_ms == length_ms;
}

/* Call once per tick(). */
static void oq_events_poll(const struct oqmod_api *api, const struct oq_events *ev) {
    if (!api || !ev) return;
    int first = !oq_ev_state.started;
    oq_ev_state.started = 1;
    for (unsigned n = 1; n <= 4; n++) {
        struct oqmod_deck d;
        memset(&d, 0, sizeof d);
        if (!api->deck(n, &d)) d.loaded = 0;
        d.title[sizeof d.title - 1] = 0; d.artist[sizeof d.artist - 1] = 0; d.key[sizeof d.key - 1] = 0;
        __typeof__(oq_ev_state.deck[0]) *s = &oq_ev_state.deck[n];
        int was_loaded = s->known && s->loaded;
        if (d.loaded && (!was_loaded || !oq_ev_same_track(&d, s->title, s->artist, s->length_ms))) {
            if (was_loaded && s->playing && ev->pause) ev->pause(n, &d);   /* the old track stopped */
            s->playing = 0; s->near_end = 0; s->bpm = d.bpm_x100; memcpy(s->key, d.key, sizeof s->key);
            if (ev->track_loaded) ev->track_loaded(n, &d);
        } else if (!d.loaded && was_loaded) {
            if (s->playing && ev->pause) ev->pause(n, &d);
            s->playing = 0;
            if (ev->track_unloaded) ev->track_unloaded(n);
        }
        if (d.loaded) {
            if (d.playing && !s->playing && ev->play) ev->play(n, &d);
            if (!d.playing && s->playing && ev->pause) ev->pause(n, &d);
            uint32_t diff = d.bpm_x100 > s->bpm ? d.bpm_x100 - s->bpm : s->bpm - d.bpm_x100;
            if (diff >= 5 && ev->bpm_changed) ev->bpm_changed(n, s->bpm, d.bpm_x100);
            if (diff >= 5 || !d.bpm_x100) s->bpm = d.bpm_x100;
            if (strcmp(d.key, s->key)) { memcpy(s->key, d.key, sizeof s->key); if (ev->key_changed && !first) ev->key_changed(n, d.key); }
            if (ev->near_end && ev->near_end_seconds && d.length_ms) {
                int in_end = d.elapsed_ms + ev->near_end_seconds * 1000u >= d.length_ms;
                if (in_end && !s->near_end && d.playing) ev->near_end(n, &d);
                s->near_end = in_end;
            }
        }
        s->known = 1; s->loaded = d.loaded; s->playing = d.loaded && d.playing; s->length_ms = d.length_ms;
        memcpy(s->title, d.title, sizeof s->title); memcpy(s->artist, d.artist, sizeof s->artist);
    }
    int layout = api->layout ? api->layout() : 0;
    if (!first && layout != oq_ev_state.layout && ev->layout_changed) ev->layout_changed(oq_ev_state.layout, layout);
    oq_ev_state.layout = layout;
}

/* The full path of the audio file on deck 1..4 into out (e.g. "/media/usb/sda1/Contents/..."),
 * 1 if known. Known for tracks loaded from a rekordbox library on USB; folder-browsed files
 * and streaming services give 0 for now. Any thread. Needs firmware R85 or later. */
static int oq_track_path(unsigned deck, char *out, size_t n) {
    static int (*fn)(int, char *, size_t);
    static int looked;
    if (!out || !n) return 0;
    out[0] = 0;
    if (deck < 1 || deck > 4) return 0;
    /* (void *)0 is RTLD_DEFAULT: search the whole app, where the firmware exports it */
    if (!looked) { fn = (int (*)(int, char *, size_t))dlsym((void *)0, "opusmod_deck_track_path"); looked = 1; }
    return fn ? fn((int)deck - 1, out, n) : 0;
}

/* This mod's own folder on the settings USB (".../MODS/<folder>") into out, 1 if found:
 * the place for files the mod writes. Any thread. */
static int oq_mod_dir(char *out, size_t n) {
    if (!out || !n) return 0;
    out[0] = 0;
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[512];
    uintptr_t me = (uintptr_t)&oq_mod_dir;
    int ok = 0;
    while (fgets(line, sizeof line, f)) {
        unsigned long lo, hi;
        char *path = strchr(line, '/');
        if (sscanf(line, "%lx-%lx", &lo, &hi) != 2 || me < lo || me >= hi || !path) continue;
        path[strcspn(path, "\n")] = 0;
        char *slash = strrchr(path, '/');
        if (slash) { *slash = 0; snprintf(out, n, "%s", path); ok = 1; }
        break;
    }
    fclose(f);
    return ok;
}
#endif
