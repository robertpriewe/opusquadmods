/* Party Mode, as an add-on mod: MODS/party/mod.so. Moved out of the core in R104.
 * The jog rings light up with one of six effects at one of four speeds, the two players
 * in opposite phase. It uses the SDK's LED API (led_set on OQMOD_LED_JOG_RING), so the
 * core's Slip indicators and LED diagnostics keep priority, and switching the mod off
 * gives the rings back to the deck at once. The effects are the core's former
 * party_led_policy.h, unchanged: channels 0..16, scaled to 0..255 for led_set. */
#include <stdint.h>
#include <stdio.h>
#include "oqmod.h"

static const struct oqmod_api *api;
static unsigned on;
static const char *const styles[] = {"Rainbow", "Color Steps", "Breathe", "Police", "Color Flash", "Ring Chase"};
static const char *const speeds[] = {"Slow", "Medium", "Fast", "Turbo"};

static unsigned period(unsigned style, unsigned speed) {
    static const unsigned periods[] = {6144, 3072, 1536, 768};
    unsigned p = periods[speed < 4 ? speed : 2];
    return (style == 3 || style == 4) && p < 1536 ? 1536 : p;
}
/* One side's ring colour, channels 0..16 (the former party_apply_config). */
static void effect(unsigned side, unsigned style, unsigned speed, uint64_t ms, unsigned char rgb[3]) {
    unsigned p = period(style, speed);
    unsigned hue = (unsigned)((ms % p) * 1536u / p);
    hue = (hue + side * 768u) % 1536u;
    if (style == 1) hue = (hue / 256) * 256;
    unsigned sector = hue / 256u, up = ((hue % 256u) * 16u + 127u) / 255u, down = 16u - up;
    unsigned r = 0, g = 0, b = 0;
    switch (sector) {
    case 0: r = 16; g = up; break;
    case 1: r = down; g = 16; break;
    case 2: g = 16; b = up; break;
    case 3: g = down; b = 16; break;
    case 4: r = up; b = 16; break;
    default: r = 16; b = down; break;
    }
    if (style == 2) {
        unsigned pulse = hue < 768 ? hue : 1535 - hue, level = 1 + pulse * 15 / 767;
        r = r * level / 16; g = g * level / 16; b = b * level / 16;
    } else if (style == 3) {
        unsigned phase = hue / 384; r = phase == 0 ? 16 : 0; g = 0; b = phase == 2 ? 16 : 0;
    } else if (style == 4) {
        unsigned phase = hue / 384, color = (unsigned)(ms / p + side * 3) % 6;
        r = (phase & 1) ? 0 : (color == 0 || color == 4 || color == 5 ? 16 : 0);
        g = (phase & 1) ? 0 : (color == 1 || color == 3 || color == 5 ? 16 : 0);
        b = (phase & 1) ? 0 : (color == 2 || color == 3 || color == 4 ? 16 : 0);
    } else if (style == 5 && hue >= 768) r = g = b = 0;
    rgb[0] = (unsigned char)r; rgb[1] = (unsigned char)g; rgb[2] = (unsigned char)b;
}
static uint32_t to_rgb(const unsigned char c[3]) {
    return (uint32_t)(c[0] * 255u / 16u) << 16 | (uint32_t)(c[1] * 255u / 16u) << 8 | (uint32_t)(c[2] * 255u / 16u);
}
static void tick(void) {
    if (!on) return;
    unsigned style = (unsigned)api->setting_get("style", 0), speed = (unsigned)api->setting_get("speed", 2);
    if (style >= 6) style = 0;
    if (speed >= 4) speed = 2;
    static unsigned shown = ~0u;
    if (style != shown) {   /* the old Party Mode page's warning, for the flashing effects */
        shown = style;
        api->set_status(style >= 3 ? "This effect flashes. Avoid it if you are sensitive to flashing lights." : "");
    }
    uint64_t ms = api->now_ms();
    for (unsigned side = 0; side < 2; side++) {
        unsigned char c[3];
        effect(side, style, speed, ms, c);
        api->led_set(side, OQMOD_LED_JOG_RING, to_rgb(c), 250);   /* refreshed ~30x a second */
    }
}
static void enabled(int yes) {
    on = yes ? 1u : 0u;
    if (!yes) api->led_clear();
    api->log(yes ? "switched on" : "switched off");
}
int oqmod_init(const struct oqmod_api *a, struct oqmod_info *info) {
    if (!a || a->version < OQMOD_API_VERSION || a->size < sizeof *a) return 1;   /* needs the R104 API */
    api = a;
    api->choice("style", "Effect", styles, 6, 0);
    api->choice("speed", "Speed", speeds, 4, 2);
    info->api_version = OQMOD_API_VERSION;
    info->name = "Party Mode";
    info->description = "Six jog-ring effects at four speeds.\nSlip indicators keep priority; off gives the rings back.";
    info->version = "2.0";
    info->tick = tick; info->enabled = enabled;
    return 0;
}
