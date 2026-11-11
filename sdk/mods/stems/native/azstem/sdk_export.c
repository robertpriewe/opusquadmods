/* SPDX-License-Identifier: GPL-3.0-only */
/* sdk_export.c: per-deck state for the community mod services (see ../common/azdeckinfo.h).
 * #included last by azstem.c: it reads the track copy (deck_track_map.c), the observed
 * playhead (key_shift.c), the stems state (g_dk) and the beat grid (drumroll.c). Everything
 * here is a few atomic loads or one sequence-locked copy: any thread, never blocking. */
#include "../common/azdeckinfo.h"

int azstem_deck_snapshot(int deck, struct azstem_deck_snap *out);
int azstem_deck_snapshot(int deck, struct azstem_deck_snap *out) {
    if (!out || out->size < (int) sizeof *out || deck < 0 || deck >= NDECK)
        return -1;
    int size = out->size;
    memset(out, 0, sizeof *out);
    out->size = size;
    char st = atomic_load_explicit(&g_deck_state[deck], memory_order_relaxed);
    out->loaded = st == 'L' ? 1 : st == 'E' ? 0
                                            : -1;
    if (out->loaded == 1) {
        unsigned char id[16];
        char ti[LTI_STR], ar[LTI_STR];
        if (g_lti_on && lti_read(deck, id, ti, sizeof ti, ar, sizeof ar) && !lti_id_zero(id)) {
            memcpy(&out->content_id, id + 8, 4); /* trackid::TrackID + 0x08 */
            snprintf(out->title, sizeof out->title, "%s", ti);
            snprintf(out->artist, sizeof out->artist, "%s", ar);
        }
    }
    long long S = atomic_load_explicit(&g_play_obs[deck], memory_order_relaxed);
    out->playhead = S >= 0 ? S : -1;
    out->pos_s = S >= 0 ? (double) S / (double) AZ_EP147_RATE : -1.0;
    long frames = g_dk[deck].stem_frames;
    out->has_stems = frames > 0;
    out->stems_on = frames > 0 && (atomic_load_explicit(&g_dk[deck].ctrl, memory_order_relaxed) & 0x80u);
    return 0;
}

int azstem_deck_beat_phase(int deck, double *phase);
int azstem_deck_beat_phase(int deck, double *phase) {
    if (!phase || deck < 0 || deck >= NDECK || deck >= DRUM_SIDES)
        return -1;
    if (!atomic_load_explicit(&g_grid_demand, memory_order_relaxed))
        atomic_store_explicit(&g_grid_demand, 1, memory_order_relaxed);
    drum_grid *g = &g_drum_grid[deck];
    unsigned s1 = atomic_load_explicit(&g->seq, memory_order_acquire);
    if (s1 & 1u)
        return -1;
    long long anchor = atomic_load_explicit(&g->anchor_src, memory_order_relaxed);
    double beat = atomic_load_explicit(&g->beat_src, memory_order_relaxed);
    int valid = atomic_load_explicit(&g->valid, memory_order_relaxed);
    atomic_thread_fence(memory_order_acquire); /* the fields before the second look at seq */
    if (atomic_load_explicit(&g->seq, memory_order_relaxed) != s1 || !valid || beat < 1.0)
        return -1;
    long long S = atomic_load_explicit(&g_play_obs[deck], memory_order_relaxed);
    if (S < 0)
        return -1;
    double ph = fmod((double) (S - anchor), beat);
    if (ph < 0)
        ph += beat;
    *phase = ph / beat;
    return 0;
}
