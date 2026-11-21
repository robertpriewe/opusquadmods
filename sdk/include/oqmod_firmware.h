#ifndef OQMOD_FIRMWARE_H
#define OQMOD_FIRMWARE_H
/* Which firmware the player runs, from the size of the API it hands to oqmod_init(), and
 * a polite refusal for older firmware.
 *
 * Put the minimum in MODS/<mod>/min-firmware.txt as well (build.sh writes it into the
 * build folder). Firmware R124 and later read that file and do not load the mod at all.
 * Older firmware does not know the file: there, start oqmod_init() with
 *
 *     if (!OQMOD_HAS_R123(a)) return oqmod_needs_firmware(a, info, "My Mod", "1.0", "R123");
 *
 * The mod then loads but does nothing; switched on, its page says which firmware it needs. */
#include <stddef.h>
#include "oqmod.h"
#define OQMOD_HAS_CALL(a, call) ((a)->size >= offsetof(struct oqmod_api, call) + sizeof((a)->call))
#define OQMOD_HAS_R121(a) OQMOD_HAS_CALL(a, ring_brightness)
#define OQMOD_HAS_R122(a) OQMOD_HAS_CALL(a, deck_pause)
#define OQMOD_HAS_R123(a) OQMOD_HAS_CALL(a, led_state)

static const struct oqmod_api *oqmod_fw_api;
static char oqmod_fw_text[160];
static void oqmod_fw_enabled(int on) {
    if (oqmod_fw_api && oqmod_fw_api->set_status) oqmod_fw_api->set_status(on ? oqmod_fw_text : "");
}
static inline int oqmod_needs_firmware(const struct oqmod_api *a, struct oqmod_info *info,
                                       const char *name, const char *version, const char *release) {
    if (!a || !info || a->version < OQMOD_API_VERSION) return 1;
    oqmod_fw_api = a;
    const char *p = "Needs firmware ", *q = " or later.\nUpdate the OPUS with the OQ Mods Patcher.";
    size_t n = 0;
    for (const char *s = p; *s && n < sizeof oqmod_fw_text - 1; s++) oqmod_fw_text[n++] = *s;
    for (const char *s = release; s && *s && n < sizeof oqmod_fw_text - 1; s++) oqmod_fw_text[n++] = *s;
    for (const char *s = q; *s && n < sizeof oqmod_fw_text - 1; s++) oqmod_fw_text[n++] = *s;
    oqmod_fw_text[n] = 0;
    info->api_version = OQMOD_API_VERSION;
    info->name = name;
    info->description = oqmod_fw_text;
    info->version = version;
    info->enabled = oqmod_fw_enabled;
    return 0;
}
#endif
