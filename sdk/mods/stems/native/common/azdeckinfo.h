/* SPDX-License-Identifier: GPL-3.0-only */
/* azdeckinfo.h: what azstem publishes about each deck for the community mod services
 * (azmod's deck_info / deck_grid / on_deck_event).
 *
 * azstem already keeps, per deck and by identity, the track EP147 loaded (copied inside
 * EP147's own update, under a sequence lock), the playhead its audio hook observes, and the
 * beat grid it reads for Drum Roll. azmod looks these two functions up with dlsym and never
 * reads EP147's memory for them. Both are safe on any thread and never block. */
#ifndef AZDECKINFO_H
#define AZDECKINFO_H

#define AZSNAP_STR 128

struct azstem_deck_snap {
    int size;               /* = sizeof(struct azstem_deck_snap), set by the caller */
    int loaded;             /* 1 = a track is loaded, 0 = the deck is empty, -1 = unknown */
    unsigned content_id;    /* the rekordbox track id (TrackID + 0x08), 0 = unknown */
    long long playhead;     /* source samples at the deck engine rate, -1 = never observed */
    double pos_s;           /* the playhead in seconds, -1 = unknown */
    int has_stems;          /* 1 = stems are decoded for this deck's track */
    int stems_on;           /* 1 = the stems engine plays this deck */
    char title[AZSNAP_STR]; /* "" = unknown */
    char artist[AZSNAP_STR];
};

/* 0 = filled, -1 = no such deck (or a caller struct too small). */
typedef int (*azstem_deck_snapshot_fn)(int deck, struct azstem_deck_snap *out);

/* The playhead's position within the current beat, 0..1. 0 = filled, -1 = no usable grid
 * (empty deck, mid-load, a grid whose tempo and spacing disagree) or no playhead. The first
 * call asks azstem's flusher to keep every deck's grid current, so it can return -1 for a
 * moment after the first request. */
typedef int (*azstem_deck_beat_phase_fn)(int deck, double *phase);

#endif
