/* Gated Hot Cues, as an add-on mod: MODS/gated-cues/mod.so.
 * On a paused deck, a hot cue plays only while its pad is held and returns to the cue on
 * release; PLAY while holding keeps it playing (native_gated_cue.h). Three vtable slots
 * of EP136 are patched the first time the mod is switched on; switched off they pass
 * every press straight to the stock handlers. A button in the Beat FX box switches the
 * mod on and off (option "Show the button in the Beat FX box"). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "oqmod.h"

static const struct oqmod_api *api;
/* the engine's patch_slot: the slot must still hold what gc_install just checked */
static int patch_slot(uintptr_t slot, void *fn, void **orig) {
    return api->patch(slot, *(const uintptr_t *)slot, fn, orig) ? 1 : -1;
}
#include "native_gated_cue.h"

static unsigned installed, shown_started = ~0u, shown_finished = ~0u;

static void tick(void) {
    unsigned started = __atomic_load_n(&gc_started, __ATOMIC_ACQUIRE);
    unsigned finished = __atomic_load_n(&gc_finished, __ATOMIC_ACQUIRE);
    if (started == shown_started && finished == shown_finished) return;
    shown_started = started; shown_finished = finished;
    char line[120];
    snprintf(line, sizeof line, "gated started=%u finished=%u stock=%u refused=%u latched=%u returned=%u",
             started, finished, gc_passed, gc_rejected, gc_latched, gc_returned);
    api->log(line);
}
static void enabled(int on) {
    if (on && !installed) {
        if (!api->ep136_ok()) {
            api->set_status("Not available on this deck software.");
            return;
        }
        installed = 1;
        gc_install();
        char line[64];
        snprintf(line, sizeof line, "hooks=%u play=%u", gc_hooks, gc_play_hook);
        api->log(line);
        if (gc_hooks != 3 || !gc_play_hook)
            api->set_status("Could not attach to the cue buttons; cues stay stock.");
    }
    gc_pump(on);
}
int oqmod_init(const struct oqmod_api *a, struct oqmod_info *info) {
    if (!a || a->version < OQMOD_API_VERSION || a->size < sizeof *a) return 1;
    api = a;
    api->option("quick", "Show the button in the Beat FX box", 1);
    api->quick_toggle("GATE CUES  ON", "NORMAL CUES", "quick");
    info->api_version = OQMOD_API_VERSION;
    info->name = "Gated Hot Cues";
    info->description = "On a paused deck, hot cues play while held and return on release.";
    info->version = "1.0";
    info->tick = tick; info->enabled = enabled;
    return 0;
}
