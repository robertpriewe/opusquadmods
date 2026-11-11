/* Pro DJ Link (Experimental), as an add-on mod: MODS/prodjlink/mod.so.
 * Switched on, the OPUS joins the DJ Link network on its LAN port as player 4 and
 * follows the other players (prodjlink_link.h); their BPM, pitch, beat and play state
 * show under the mod's switch on Mod Menu > Add-on Mods. Nothing is sent to the mixer.
 * Option "offer": the link probe (prodjlink_probe.h) announces a USB and logs what other
 * players then ask for, to link-probe.txt beside mod.so. */
#include <stdio.h>
#include "prodjlink_link.h"
#include "oqmod.h"

static const struct oqmod_api *api;
static unsigned ticks;

static void tick(void) {
    __atomic_store_n(&lp_offer, api->setting_get("offer", 0) ? 1u : 0u, __ATOMIC_RELEASE);
    if (ticks++ % 15) return;                      /* about twice a second */
    char text[512];
    lk_summary(text, sizeof text);
    api->set_status(text);
}
static void enabled(int on) {
    lk_set_enabled(on);
    ticks = 0;
    api->set_status(on ? "Joining the link ..." : "");
    api->log(on ? "joined as player 4" : "left the link");
}
int oqmod_init(const struct oqmod_api *a, struct oqmod_info *info) {
    if (!a || a->version < OQMOD_API_VERSION || a->size < sizeof *a) return 1;
    api = a;
    api->option("offer", "Offer this OPUS's USB to other players (test)", 0);
    info->api_version = OQMOD_API_VERSION;
    info->name = "Pro DJ Link (Experimental)";
    info->description = "Joins the DJ Link network on the LAN port as player 4.";
    info->version = "1.0";
    info->tick = tick; info->enabled = enabled;
    return 0;
}
