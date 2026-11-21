/* Fader Start: channel faders and the crossfader start and pause the decks, as on CDJ/DJM
 * setups. Opening a channel fader from the bottom plays its deck; closing it to the
 * bottom pauses it. Only the movement through the bottom acts, so PLAY with the fader
 * down still works. Crossfader start: leaving one end starts the deck(s) on the side
 * being opened, returning to that end pauses them. Needs firmware R122 (mixer(),
 * deck_pause()). Channel n = deck n. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "oqmod.h"

static const struct oqmod_api *api;
static unsigned on, missing;
#define CLOSED 24    /* 10-bit: at or below = fader at the bottom / crossfader at the end */
#define OPEN 56      /* at or above = clearly moved away (hysteresis) */
static int ch_open[4] = {-1, -1, -1, -1}, xf_left = -1, xf_right = -1;
static unsigned starts, pauses;
static const char *const xf_choices[] = {"Off", "Left 1, right 2", "Left 3, right 4", "Left 1+3, right 2+4"};

static int supported(void) {
    return api->size >= offsetof(struct oqmod_api, deck_pause) + sizeof api->deck_pause && api->mixer && api->deck_pause;
}
static char last[160];
/* Each action is logged with the deck state and the result (diagnostics: addon_log). */
static void start(unsigned deck) {
    struct oqmod_deck d; memset(&d, 0, sizeof d);
    int known = api->deck(deck, &d), r = -1;
    if (known && d.loaded && !d.playing) { r = api->deck_play(deck); if (r) starts++; }
    snprintf(last, sizeof last, "open deck %u: known=%d loaded=%d playing=%d play=%d", deck, known, d.loaded, d.playing, r);
    api->log(last);
}
static void stop(unsigned deck) {
    struct oqmod_deck d; memset(&d, 0, sizeof d);
    int known = api->deck(deck, &d), r = -1;
    if (known && d.loaded && d.playing) { r = api->deck_pause(deck); if (r) pauses++; }
    snprintf(last, sizeof last, "close deck %u: known=%d loaded=%d playing=%d pause=%d", deck, known, d.loaded, d.playing, r);
    api->log(last);
}
/* Edge state of one fader: 1 open, 0 closed, -1 not known yet. Returns +1 opened, -1 closed. */
static int edge(int *state, unsigned v) {
    int now = v <= CLOSED ? 0 : v >= OPEN ? 1 : *state;
    int was = *state; *state = now;
    if (was < 0 || now < 0 || was == now) return 0;
    return now ? 1 : -1;
}
static void side(unsigned mask, int opened) {
    for (unsigned d = 1; d <= 4; d++) if (mask >> (d - 1) & 1) { if (opened) start(d); else stop(d); }
}
static void tick(void) {
    if (!on) return;
    if (!supported()) { if (!missing) { missing = 1; api->set_status("Needs firmware R122 or later."); } return; }
    struct oqmod_mixer m; m.size = sizeof m;
    if (!api->mixer(&m) || !m.valid) return;
    if (api->setting_get("channel", 1))
        for (unsigned c = 0; c < 4; c++) {
            int e = edge(&ch_open[c], m.fader[c]);
            if (e > 0) start(c + 1); else if (e < 0) stop(c + 1);
        }
    else for (unsigned c = 0; c < 4; c++) ch_open[c] = -1;
    int xf = api->setting_get("xfader", 0);
    static const unsigned left[] = {0, 1, 4, 5}, right[] = {0, 2, 8, 10};   /* deck bit masks */
    if (xf > 0 && xf < 4) {
        /* away from the left end opens the right side; away from the right end opens the left */
        int el = edge(&xf_left, m.crossfader), er = edge(&xf_right, 1023u - m.crossfader);
        if (el) side(right[xf], el > 0);
        if (er) side(left[xf], er > 0);
    } else xf_left = xf_right = -1;
    static uint64_t shown_ms;
    uint64_t now = api->now_ms();
    if (now - shown_ms >= 300) {   /* live values, so a test shows what the mod sees */
        shown_ms = now;
        char s[400];
        snprintf(s, sizeof s, "Faders %u %u %u %u, crossfader %u. Starts %u, pauses %u.\nLast: %s",
                 m.fader[0], m.fader[1], m.fader[2], m.fader[3], m.crossfader, starts, pauses, last[0] ? last : "-");
        api->set_status(s);
    }
}
static void enabled(int yes) {
    on = yes ? 1u : 0u;
    for (unsigned c = 0; c < 4; c++) ch_open[c] = -1;   /* the first frame only learns positions */
    xf_left = xf_right = -1; missing = 0;
}
int oqmod_init(const struct oqmod_api *a, struct oqmod_info *info) {
    if (!a || a->version < OQMOD_API_VERSION || a->size < offsetof(struct oqmod_api, led_set)) return 1;
    api = a;
    api->option("channel", "Channel fader start", 1);
    api->choice("xfader", "Crossfader start", xf_choices, 4, 0);
    info->api_version = OQMOD_API_VERSION;
    info->name = "Fader Start";
    info->description = "Channel faders and the crossfader start and pause the decks.\nNeeds firmware R122 or later.";
    info->version = "1.1";
    info->tick = tick;
    info->enabled = enabled;
    return 0;
}
