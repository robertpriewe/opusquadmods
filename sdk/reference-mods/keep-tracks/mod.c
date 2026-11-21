/* Keep Tracks On Eject: when a USB stick is ejected from the SOURCE list, the decks keep
 * the tracks loaded from it (as the XDJ-AZ does). EP136 has the whole track in memory
 * already; on an on-screen eject it unloads the paused decks from that stick before
 * releasing it (playing decks it leaves alone). This mod skips that one unload call:
 *   SOURCE eject request: UsecaseStorageDeviceController (vt 0x236dc60 slot 3, 0x991860)
 *   the eject's unload:   TrackLoadFacade as ITrackUnloadable (vt 0x24bfea8 slot 2,
 *                         0x114a958), called from 0x8782ec (deck step 0x878268)
 * Only calls from that return address within 15 s of an eject request are skipped; every
 * other unload runs as usual. Found with Call Trace on R122 (hwtest-r122). */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "oqmod.h"

static const struct oqmod_api *api;
static _Atomic unsigned on;
static unsigned hooks, kept, passed;
static _Atomic uint64_t eject_ms;
static uint64_t (*eject_orig)(void *, void *, void *, void *, void *, void *, void *, void *);
static uint64_t (*unload_orig)(void *, void *, void *, void *, void *, void *, void *, void *);

static uint64_t eject_hook(void *a, void *b, void *c, void *d, void *e, void *f, void *g, void *h) {
    __atomic_store_n(&eject_ms, api->now_ms(), __ATOMIC_RELEASE);
    return eject_orig(a, b, c, d, e, f, g, h);
}
static uint64_t unload_hook(void *a, void *b, void *c, void *d, void *e, void *f, void *g, void *h) {
    uintptr_t from = (uintptr_t)__builtin_return_address(0);
    uint64_t since = api->now_ms() - __atomic_load_n(&eject_ms, __ATOMIC_ACQUIRE);
    if (__atomic_load_n(&on, __ATOMIC_ACQUIRE) && from == 0x8782ecul && since < 15000) {
        __atomic_fetch_add(&kept, 1, __ATOMIC_RELAXED);
        return 0;                       /* the caller ignores the result */
    }
    __atomic_fetch_add(&passed, 1, __ATOMIC_RELAXED);
    return unload_orig(a, b, c, d, e, f, g, h);
}
static void tick(void) {
    static unsigned shown = ~0u;
    unsigned now = hooks * 100000 + kept;
    if (now == shown) return;
    shown = now;
    char s[120];
    if (hooks != 3) snprintf(s, sizeof s, "Could not attach (%u of 2). Switch Call Trace off and restart.", (hooks & 1) + (hooks >> 1 & 1));
    else snprintf(s, sizeof s, "Ready. Tracks kept on eject: %u.", kept);
    api->set_status(s);
}
static void enabled(int yes) { __atomic_store_n(&on, yes ? 1u : 0u, __ATOMIC_RELEASE); }
int oqmod_init(const struct oqmod_api *a, struct oqmod_info *info) {
    if (!a || a->version < OQMOD_API_VERSION || a->size < offsetof(struct oqmod_api, led_set)) return 1;
    api = a;
    if (api->ep136_ok && api->ep136_ok() && api->patch) {
        if (api->patch(0x236dc60u + 3 * 8, 0x991860u, (void *)eject_hook, (void **)&eject_orig) == 1) hooks |= 1;
        if (api->patch(0x24bfea8u + 2 * 8, 0x114a958u, (void *)unload_hook, (void **)&unload_orig) == 1) hooks |= 2;
    }
    info->api_version = OQMOD_API_VERSION;
    info->name = "Keep Tracks On Eject";
    info->description = "Ejecting a USB in SOURCE keeps the loaded tracks on the decks.";
    info->version = "1.0";
    info->tick = tick;
    info->enabled = enabled;
    return 0;
}
