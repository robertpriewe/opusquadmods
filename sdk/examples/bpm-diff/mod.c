/* Example add-on mod: a badge (top left) on the WAVEFORM screen with the BPM difference between
 * two decks. Tap it to switch between decks 1-2 and 3-4 (remembered on the USB). */
#include <stdio.h>
#include "oqmod.h"

static const struct oqmod_api *api;
static int pair;                 /* 0: decks 1-2, 1: decks 3-4 */
static uint32_t shown_a, shown_b;
static int on;

static void tick(void) {
    struct oqmod_deck a, b;
    uint32_t bpm_a = api->deck(pair ? 3 : 1, &a) && a.loaded ? a.bpm_x100 : 0;
    uint32_t bpm_b = api->deck(pair ? 4 : 2, &b) && b.loaded ? b.bpm_x100 : 0;
    if (bpm_a != shown_a || bpm_b != shown_b) {   /* repaint only when it changes */
        shown_a = bpm_a; shown_b = bpm_b;
        api->redraw();
    }
}
static void draw(struct oqmod_canvas *c, int w, int h) {
    char line[48];
    if (!shown_a || !shown_b) {
        snprintf(line, sizeof line, "%d-%d: --", pair ? 3 : 1, pair ? 4 : 2);
    } else {
        int diff = (int)shown_a - (int)shown_b, mag = diff < 0 ? -diff : diff;
        snprintf(line, sizeof line, "%d-%d: %c%d.%02d BPM", pair ? 3 : 1, pair ? 4 : 2,
                 diff < 0 ? '-' : '+', mag / 100, mag % 100);
    }
    int close = shown_a && shown_b && (shown_a > shown_b ? shown_a - shown_b : shown_b - shown_a) <= 5;
    api->fill(c, 0, 0, w, h, 0x181818);
    api->outline(c, 0, 0, w, h, 2, close ? 0x20c040 : 0xff7323);
    api->text(c, 0, 0, w, h, line, 16, 0xffffff);
}
static int touch(int x, int y) {
    (void)x; (void)y;
    pair = !pair;
    api->setting_set("pair", pair);
    shown_a = shown_b = 0;
    api->redraw();
    return 1;
}
static void enabled(int yes) {
    on = yes;
    api->log(yes ? "switched on" : "switched off");
}
int oqmod_init(const struct oqmod_api *a, struct oqmod_info *info) {
    if (!a || a->version < OQMOD_API_VERSION || a->size < sizeof *a) return 1;
    api = a;
    pair = api->setting_get("pair", 0) ? 1 : 0;
    info->api_version = OQMOD_API_VERSION;
    info->name = "BPM Difference";
    info->description = "Badge with the BPM difference between two decks.";
    info->version = "1.0";
    info->x = 4; info->y = 8; info->w = 170; info->h = 40;   /* top left, clear of the USB buttons */
    info->tick = tick; info->draw = draw; info->touch = touch; info->enabled = enabled;
    return 0;
}
