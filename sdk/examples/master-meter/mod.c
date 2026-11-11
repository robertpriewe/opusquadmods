/* Example add-on mod: a master level meter (firmware R118 and later).
 *
 * Uses the read-only master feed from oqmod_master_audio.h: a copy of the final stereo
 * mix, as the deck's USB recorder receives it. The callback runs on the audio thread, so
 * it only keeps the loudest sample of each side; tick() turns that into a level and
 * draw() paints two bars with peak hold. On older firmware it says the feed is missing. */
#include <math.h>
#include <stdio.h>
#include "oqmod.h"
#include "oqmod_master_audio.h"

static const struct oqmod_api *api;
static const struct oq_master_audio_api *master;
static int tapped;
/* Sum of squares (in 1/65536 units) and sample count, written by the audio thread. The
 * meter shows the average (RMS) level: on R117/R118 hardware the feed's sample peaks sit
 * at full scale whenever music plays, so a peak meter stays pinned; RMS follows the
 * faders and the master. */
static _Atomic uint64_t sum_l, sum_r, frames_total;
static float level_l, level_r, hold_l, hold_r;   /* 0..1, GUI side */
static uint64_t hold_ms_l, hold_ms_r;

/* Audio thread: no locks, allocation, I/O or SDK calls. The block is a copy. */
static void on_block(const struct oq_master_audio_block *b, void *user) {
    (void)user;
    double l = 0, r = 0;
    for (uint32_t i = 0; i < b->frames; i++) {
        double x = b->interleaved[2 * i], y = b->interleaved[2 * i + 1];
        l += x * x; r += y * y;
    }
    __atomic_fetch_add(&sum_l, (uint64_t)(l * 65536.0), __ATOMIC_RELAXED);
    __atomic_fetch_add(&sum_r, (uint64_t)(r * 65536.0), __ATOMIC_RELAXED);
    __atomic_fetch_add(&frames_total, b->frames, __ATOMIC_RELAXED);
}

/* RMS dBFS -48..0 to 0..1 */
static float meter(double sum, uint64_t frames) {
    if (!frames) return 0.f;
    double rms = sqrt(sum / 65536.0 / (double)frames);
    float db = rms > 1e-6 ? 20.f * (float)log10(rms) : -120.f;
    return db <= -48.f ? 0.f : db >= 0.f ? 1.f : (db + 48.f) / 48.f;
}
static void tick(void) {
    uint64_t n = __atomic_exchange_n(&frames_total, 0, __ATOMIC_RELAXED);
    double sl = (double)__atomic_exchange_n(&sum_l, 0, __ATOMIC_RELAXED), sr = (double)__atomic_exchange_n(&sum_r, 0, __ATOMIC_RELAXED);
    float l = meter(sl, n), r = meter(sr, n);
    /* Readings for checking the feed: 2-second average level in dBFS, on the mod's page
     * and in the runtime report. */
    static double acc_l, acc_r; static uint64_t acc_n, last_report;
    acc_l += sl; acc_r += sr; acc_n += n;
    if (tapped && api->now_ms() - last_report >= 2000) {
        last_report = api->now_ms();
        double dl = acc_n ? 10.0 * log10(acc_l / 65536.0 / (double)acc_n + 1e-12) : -120.0;
        double dr = acc_n ? 10.0 * log10(acc_r / 65536.0 / (double)acc_n + 1e-12) : -120.0;
        char line[120];
        snprintf(line, sizeof line, "at %llu s: L %.1f dB, R %.1f dB", (unsigned long long)(last_report / 1000), dl, dr);
        api->set_status(line); api->log(line);
        acc_l = acc_r = 0; acc_n = 0;
    }
    level_l = l > level_l ? l : level_l * 0.85f;      /* fast attack, smooth fall */
    level_r = r > level_r ? r : level_r * 0.85f;
    uint64_t now = api->now_ms();
    if (level_l >= hold_l || now - hold_ms_l > 1500) {hold_l = level_l; hold_ms_l = now;}
    if (level_r >= hold_r || now - hold_ms_r > 1500) {hold_r = level_r; hold_ms_r = now;}
    if (tapped) api->redraw();
}
static void bar(struct oqmod_canvas *c, int y, int w, int h, float v, float hold) {
    int x0 = 18, len = w - x0 - 4, n = (int)(v * len), hx = x0 + (int)(hold * len);
    api->fill(c, x0, y, len, h, 0x202020);
    int green = len * 2 / 3, amber = len * 9 / 10;
    if (n > 0) api->fill(c, x0, y, n < green ? n : green, h, 0x2ecc40);
    if (n > green) api->fill(c, x0 + green, y, (n < amber ? n : amber) - green, h, 0xffb000);
    if (n > amber) api->fill(c, x0 + amber, y, n - amber, h, 0xff3020);
    if (hold > 0.01f) api->fill(c, hx - 1, y, 2, h, 0xffffff);
}
static void draw(struct oqmod_canvas *c, int w, int h) {
    api->fill(c, 0, 0, w, h, 0x0c0c0c);
    if (!tapped) {api->text(c, 4, 0, w - 8, h, "NO MASTER FEED", 12, 0x808080); return;}
    int bh = (h - 6) / 2;
    api->text(c, 2, 2, 14, bh, "L", 11, 0xb0b0b0);
    api->text(c, 2, 4 + bh, 14, bh, "R", 11, 0xb0b0b0);
    bar(c, 2, w, bh, level_l, hold_l);
    bar(c, 4 + bh, w, bh, level_r, hold_r);
}
static void enabled(int on) {
    if (on && !tapped) {
        master = oq_master_audio_discover_tap();
        if (!master) {api->set_status("This firmware has no master feed (needs R118 or later)."); api->redraw(); return;}
        int r = oq_master_audio_tap_install(master, on_block, NULL);
        tapped = r == OQ_MASTER_AUDIO_OK;
        api->set_status(tapped ? "Master feed connected (read-only)." : "Master feed busy: four mods already use it.");
    } else if (!on && tapped) {
        tapped = oq_master_audio_tap_remove(master, on_block, NULL) != OQ_MASTER_AUDIO_OK;
        level_l = level_r = hold_l = hold_r = 0;
    }
    api->redraw();
}

int oqmod_init(const struct oqmod_api *a, struct oqmod_info *info) {
    if (!a || a->version < OQMOD_API_VERSION || a->size < sizeof *a) return 1;
    api = a;
    info->api_version = OQMOD_API_VERSION;
    info->name = "Master Meter";
    info->description = "Left/right average level of the master output, with\npeak hold. Needs firmware R118.";
    info->version = "1.2";
    info->x = 740; info->y = 6; info->w = 170; info->h = 32;
    info->tick = tick;
    info->draw = draw;
    info->enabled = enabled;
    return 0;
}
