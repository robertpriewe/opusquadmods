/* Jog Ring Brightness: dims both jog rings to 75, 50, 25 or 10 percent of what they show
 * (deck colour, Party Mode, other mods' lights). Uses the R121 ring_brightness API;
 * switching the mod off (or removing the USB) gives full brightness back. */
#include <stddef.h>
#include <stdint.h>
#include "oqmod.h"

static const struct oqmod_api *api;
static unsigned on, applied, missing;
static const char *const levels[] = {"100%", "75%", "50%", "25%", "10%"};
static const unsigned percent[] = {100, 75, 50, 25, 10};

static int supported(void) {
    return api->size >= offsetof(struct oqmod_api, ring_brightness) + sizeof api->ring_brightness && api->ring_brightness;
}
static void tick(void) {
    if (!on) return;
    if (!supported()) {
        if (!missing) { missing = 1; api->set_status("Needs firmware R121 or later."); }
        return;
    }
    int level = api->setting_get("level", 2);
    if (level < 0 || level > 4) level = 2;
    unsigned want = percent[level];
    if (want == applied) return;
    if (api->ring_brightness(want)) {
        applied = want;
        api->set_status(want == 100 ? "Jog rings at full brightness." : "");
    } else {
        api->set_status("Another mod is controlling the jog ring brightness.");
    }
}
static void enabled(int yes) {
    on = yes ? 1u : 0u;
    applied = 0;   /* tick() applies the level; switching off returns the rings to 100 % */
    missing = 0;
}
int oqmod_init(const struct oqmod_api *a, struct oqmod_info *info) {
    if (!a || a->version < OQMOD_API_VERSION || a->size < offsetof(struct oqmod_api, led_set)) return 1;
    api = a;
    api->choice("level", "Brightness", levels, 5, 2);
    info->api_version = OQMOD_API_VERSION;
    info->name = "Jog Ring Brightness";
    info->description = "Dim both jog rings: 75, 50, 25 or 10 percent.\nWorks with deck colours and Party Mode.";
    info->version = "1.0";
    info->tick = tick;
    info->enabled = enabled;
    return 0;
}
